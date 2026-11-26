// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "draw.h"
#include "live.h"
#include "store.h"

#include <malloc.h>
#include <math.h>
#include <ogc/conf.h>
#include <string.h>

typedef struct {
    u32 code;
    u16 x, y;
    u8 width, height;
    s8 left, top, advance;
} Glyph;

#include "store_assets.h"

static void *framebuffers[2];
static unsigned front;
static int screen_width;
static bool interlaced;
static GXTexObj font_texture, brand_texture, category_texture, pointer_texture;
static u8 fifo[128 * 1024] ATTRIBUTE_ALIGN(32);
static const float curve_x[] = {1.0f, 0.9659f, 0.8660f, 0.7071f, 0.5000f, 0.2588f, 0.0f};

bool DrawInit(void) {
    VIDEO_Init();
    GXRModeObj *mode = VIDEO_GetPreferredMode(NULL);
    for (unsigned index = 0; index < 2; ++index) {
        void *buffer = SYS_AllocateFramebuffer(mode);
        if (!buffer) return false;
        framebuffers[index] = MEM_K0_TO_K1(buffer);
    }
    interlaced = (mode->viTVMode & 3u) == VI_INTERLACE;
    screen_width = CONF_GetAspectRatio() == CONF_ASPECT_16_9 ? 854 : 640;
    VIDEO_Configure(mode);
    VIDEO_SetNextFramebuffer(framebuffers[0]);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (!interlaced) VIDEO_WaitVSync();

    GX_Init(fifo, sizeof(fifo));
    GX_SetCopyClear((GXColor){0, 0, 0, 255}, GX_MAX_Z24);
    GX_SetViewport(0, 0, mode->fbWidth, mode->efbHeight, 0, 1);
    GX_SetScissor(0, 0, mode->fbWidth, mode->efbHeight);
    const f32 scale = GX_GetYScaleFactor(mode->efbHeight, mode->xfbHeight);
    GX_SetDispCopySrc(0, 0, mode->fbWidth, mode->efbHeight);
    GX_SetDispCopyDst(mode->fbWidth, GX_SetDispCopyYScale(scale));
    GX_SetDispCopyGamma(GX_GM_1_0);
    // Keep the field deflicker filter for interlaced output. Progressive text can stay sharp.
    GX_SetCopyFilter(mode->aa, mode->sample_pattern, interlaced ? GX_TRUE : GX_FALSE, mode->vfilter);
    GX_SetFieldMode(mode->field_rendering, mode->viHeight == 2 * mode->xfbHeight);
    GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);
    GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GX_SetColorUpdate(GX_TRUE);
    GX_SetCullMode(GX_CULL_NONE);
    GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GX_SetNumChans(1);
    GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, 0, GX_DF_NONE, GX_AF_NONE);
    GX_SetNumTevStages(1);
    GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);

    Mtx44 projection;
    guOrtho(projection, 0, 480, 0, screen_width, 0, 100);
    GX_LoadProjectionMtx(projection, GX_ORTHOGRAPHIC);
    Mtx model;
    guMtxIdentity(model);
    GX_LoadPosMtxImm(model, GX_PNMTX0);
    GX_SetCurrentMtx(GX_PNMTX0);

    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GX_InitTexObj(&font_texture, (void *)font_pixels, FONT_WIDTH, FONT_HEIGHT, GX_TF_IA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObj(&brand_texture, (void *)brand_pixels, BRAND_WIDTH, BRAND_HEIGHT, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObj(&category_texture, (void *)category_pixels, CATEGORY_CELL * 4, CATEGORY_CELL * 4, GX_TF_RGBA8,
                  GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObj(&pointer_texture, (void *)pointer_pixels, POINTER_SIZE, POINTER_SIZE, GX_TF_RGBA8,
                  GX_CLAMP, GX_CLAMP, GX_FALSE);
    return true;
}

int ScreenWidth(void) { return screen_width; }

void DrawEnd(void) {
    front ^= 1;
    GX_CopyDisp(framebuffers[front], GX_TRUE);
    GX_DrawDone();
    VIDEO_SetNextFramebuffer(framebuffers[front]);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
}

static void Material(GXTexObj *texture) {
    GX_SetNumTexGens(texture != NULL);
    if (texture) {
        GX_LoadTexObj(texture, GX_TEXMAP0);
        GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
        GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
    } else {
        GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
        GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    }
}

static void Vertex(float x, float y, u32 color, float u, float v) {
    GX_Position2f32(x, y);
    GX_Color1u32(color);
    GX_TexCoord2f32(u, v);
}

static void Quad(float x, float y, float width, float height, u32 top, u32 bottom,
                 float u0, float v0, float u1, float v1) {
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    Vertex(x, y, top, u0, v0);
    Vertex(x + width, y, top, u1, v0);
    Vertex(x + width, y + height, bottom, u1, v1);
    Vertex(x, y + height, bottom, u0, v1);
    GX_End();
}

void Gradient(float x, float y, float width, float height, u32 top, u32 bottom) {
    Material(NULL);
    Quad(x, y, width, height, top, bottom, 0, 0, 0, 0);
}

void Box(float x, float y, float width, float height, u32 color) {
    Gradient(x, y, width, height, color, color);
}

static u32 Mix(u32 top, u32 bottom, float fraction) {
    u32 color = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        const int first = (top >> shift) & 255;
        const int last = (bottom >> shift) & 255;
        color |= (u32)(first + (last - first) * fraction) << shift;
    }
    return color;
}

static void RoundShape(float x, float y, float width, float height, float radius, u32 top, u32 bottom,
                       float u_span, float v_span) {
    const float u_start = (1 - u_span) / 2;
    const float v_start = (1 - v_span) / 2;
    const float u_per_pixel = u_span / width;
    const float inverse_height = 1 / height;
    GX_Begin(GX_TRIANGLEFAN, GX_VTXFMT0, 30);
    Vertex(x + width / 2, y + height / 2, Mix(top, bottom, 0.5f), 0.5f, 0.5f);
    for (unsigned corner = 0; corner < 4; ++corner) {
        const bool right = corner == 0 || corner == 1;
        const bool down = corner == 1 || corner == 2;
        const float center_x = x + (right ? width - radius : radius);
        const float center_y = y + (down ? height - radius : radius);
        for (unsigned step = 0; step < 7; ++step) {
            const float turn_x = curve_x[down == right ? step : 6 - step];
            const float turn_y = curve_x[down == right ? 6 - step : step];
            const float vx = center_x + (right ? turn_x : -turn_x) * radius;
            const float vy = center_y + (down ? turn_y : -turn_y) * radius;
            const float fraction_y = (vy - y) * inverse_height;
            Vertex(vx, vy, Mix(top, bottom, fraction_y),
                   u_start + (vx - x) * u_per_pixel, v_start + fraction_y * v_span);
        }
    }
    Vertex(x + width - radius, y, top, u_start + (width - radius) * u_per_pixel, v_start);
    GX_End();
}

void Panel(float x, float y, float width, float height, float radius, u32 top, u32 bottom) {
    Material(NULL);
    RoundShape(x, y, width, height, radius, top, bottom, 0, 0);
}

void Triangle(float x1, float y1, float x2, float y2, float x3, float y3, u32 color) {
    Material(NULL);
    GX_Begin(GX_TRIANGLES, GX_VTXFMT0, 3);
    Vertex(x1, y1, color, 0, 0);
    Vertex(x2, y2, color, 0, 0);
    Vertex(x3, y3, color, 0, 0);
    GX_End();
}

void Wave(float base, float amplitude, float height, float phase, u32 top, u32 bottom) {
    const unsigned segments = 48;
    Material(NULL);
    GX_Begin(GX_TRIANGLESTRIP, GX_VTXFMT0, (segments + 1) * 2);
    for (unsigned segment = 0; segment <= segments; ++segment) {
        const float fraction = (float)segment / segments;
        const float x = screen_width * fraction;
        const float y = base + sinf(phase + fraction * 6.2831853f) * amplitude;
        Vertex(x, y, top, 0, 0);
        Vertex(x, y + height, bottom, 0, 0);
    }
    GX_End();
}

void Brand(float x, float y) {
    Material(&brand_texture);
    Quad(x, y, BRAND_WIDTH, BRAND_HEIGHT, 0xffffffff, 0xffffffff, 0, 0, 1, 1);
}

void DrawPointer(float x, float y, u32 tint) {
    const float size = 42;
    const float scale = size / POINTER_SIZE;
    Material(&pointer_texture);
    Quad(x - POINTER_TIP_X * scale, y - POINTER_TIP_Y * scale, size, size,
         tint, tint, 0, 0, 1, 1);
}

void LoadingRing(float x, float y, float radius, u32 milliseconds) {
    static const float circle[][2] = {
        {0, -1}, {.5f, -.866f}, {.866f, -.5f}, {1, 0}, {.866f, .5f}, {.5f, .866f},
        {0, 1}, {-.5f, .866f}, {-.866f, .5f}, {-1, 0}, {-.866f, -.5f}, {-.5f, -.866f}
    };
    const unsigned head = (milliseconds / 65) % 12;
    for (unsigned dot = 0; dot < 12; ++dot) {
        const unsigned age = (head + 12 - dot) % 12;
        const float strength = (11 - age) / 11.0f;
        const float size = radius * (.13f + .10f * strength);
        const u32 color = Mix(0xabcfdca0, 0x008dbaff, strength);
        const float cx = x + circle[dot][0] * radius;
        const float cy = y + circle[dot][1] * radius;
        Panel(cx - size / 2, cy - size / 2, size, size, size / 2, color, color);
    }
}

void CategoryIcon(unsigned category, float x, float y, float size) {
    const float u = (category % 4) * .25f;
    const float v = (category / 4) * .25f;
    Material(&category_texture);
    Quad(x, y, size, size, 0xffffffff, 0xffffffff, u, v, u + .25f, v + .25f);
}

void AppIcon(unsigned icon, float x, float y, float width, float height, float radius) {
    enum { WIDTH = 128, HEIGHT = 48 };
    const u8 *pixels = LiveIconPixels(icon);
    if (!pixels) return;
    GXTexObj texture;
    GX_InitTexObj(&texture, (void *)pixels, WIDTH, HEIGHT, GX_TF_RGB5A3, GX_CLAMP, GX_CLAMP, GX_FALSE);
    Material(&texture);
    // Center-crop to fill the inset without stretching the artwork.
    const float aspect_ratio = (width * HEIGHT) / (height * WIDTH);
    RoundShape(x, y, width, height, radius, 0xffffffff, 0xffffffff,
               fminf(1, aspect_ratio), fminf(1, 1 / aspect_ratio));
}

static const Glyph *ReadGlyph(const char **text) {
    const unsigned char *bytes = (const unsigned char *)*text;
    u32 code = *bytes++;
    if (code >= 0xc0) {
        unsigned more = code < 0xe0 ? 1 : code < 0xf0 ? 2 : 3;
        code &= 0x7f >> (more + 1);
        while (more-- && (*bytes & 0xc0) == 0x80) code = (code << 6) | (*bytes++ & 0x3f);
    }
    *text = (const char *)bytes;
    unsigned first = 0;
    unsigned last = sizeof(glyphs) / sizeof(*glyphs);
    while (first < last) {
        const unsigned mid = first + (last - first) / 2;
        if (glyphs[mid].code == code) return &glyphs[mid];
        if (glyphs[mid].code < code) first = mid + 1;
        else last = mid;
    }
    return &glyphs['?' - ' '];
}

float TextWidth(const char *text, float size) {
    unsigned units = 0;
    while (*text) units += ReadGlyph(&text)->advance;
    return units * size / FONT_SIZE;
}

static void DrawGlyph(const Glyph *glyph, float x, float baseline, float scale, u32 color) {
    if (!glyph->width || !glyph->height) return;
    Quad(x + glyph->left * scale, baseline + glyph->top * scale,
         glyph->width * scale, glyph->height * scale, color, color,
         (float)glyph->x / FONT_WIDTH, (float)glyph->y / FONT_HEIGHT,
         (float)(glyph->x + glyph->width) / FONT_WIDTH, (float)(glyph->y + glyph->height) / FONT_HEIGHT);
}

void Text(const char *text, float x, float baseline, float size, float limit, u32 color) {
    const float scale = size / FONT_SIZE;
    const bool truncate = TextWidth(text, size) > limit;
    const char *ellipsis = "…";
    const Glyph *end = ReadGlyph(&ellipsis);
    const float right = x + limit - (truncate ? end->advance * scale : 0);
    Material(&font_texture);
    while (*text) {
        const Glyph *glyph = ReadGlyph(&text);
        const float advance = glyph->advance * scale;
        if (x + advance > right + 0.01f) break;
        DrawGlyph(glyph, x, baseline, scale, color);
        x += advance;
    }
    if (truncate) DrawGlyph(end, x, baseline, scale, color);
}

void Paragraph(const char *text, float x, float baseline, float size, float width, unsigned lines, u32 color) {
    while (*text && lines) {
        char line[192];
        const char *cursor = text;
        const char *break_at = NULL;
        const char *end = text;
        float used = 0;
        while (*cursor && cursor - text < (int)sizeof(line) - 4) {
            const char *start = cursor;
            const Glyph *glyph = ReadGlyph(&cursor);
            used += glyph->advance * size / FONT_SIZE;
            if (*start == ' ') break_at = start;
            if (used > width && end != text) break;
            end = cursor;
        }
        if (*end && break_at && break_at > text && break_at <= end) end = break_at;
        size_t length = end - text;
        memcpy(line, text, length);
        if (lines == 1 && *end) {
            memcpy(line + length, "…", sizeof("…"));
        } else {
            line[length] = '\0';
        }
        Text(line, x, baseline, size, width, color);
        text = end;
        while (*text == ' ') ++text;
        baseline += size * 1.35f;
        --lines;
    }
}
