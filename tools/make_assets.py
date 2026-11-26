# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Build Rift Store's fixed GX artwork and glyph atlas."""

from itertools import batched
from pathlib import Path
import sys

from PIL import Image, ImageDraw, ImageFont

from brand_art import brand_image
from category_art import CELL, category_atlas
from pointer_art import HOTSPOT, SIZE, pointer_image


def gx_rgba(image):
    """GX stores each 4x4 tile as an AR plane followed by a GB plane."""
    pixels = image.load()
    for tile_y in range(0, image.height, 4):
        for tile_x in range(0, image.width, 4):
            tile = [pixels[tile_x + x, tile_y + y] for y in range(4) for x in range(4)]
            for first, second in ((3, 0), (1, 2)):
                for pixel in tile:
                    yield pixel[first]
                    yield pixel[second]


def gx_white_ia8(mask):
    """Pack a white glyph with 8-bit coverage in GX's 4x4 IA8 tiles."""
    alpha = mask.load()
    for tile_y in range(0, mask.height, 4):
        for tile_x in range(0, mask.width, 4):
            for y in range(4):
                for x in range(4):
                    yield alpha[tile_x + x, tile_y + y]
                    yield 255


def texture(name, data):
    lines = [",".join(map(str, tile)) for tile in batched(data, 32)]
    return f"static const u8 {name}[] ATTRIBUTE_ALIGN(32) = {{\n" + ",\n".join(lines) + "\n};\n"


def font_atlas(font, letters):
    size = 32
    cell_width, cell_height = 40, 44
    columns = 12
    height = ((len(letters) + columns - 1) // columns) * cell_height
    mask = Image.new("L", (512, height))
    drawer = ImageDraw.Draw(mask)
    glyphs = []
    for number, char in enumerate(letters):
        left, top, right, bottom = font.getbbox(char, anchor="ls")
        advance = round(font.getlength(char))
        x = (number % columns) * cell_width + 1
        y = (number // columns) * cell_height + 1
        width, height = right - left, bottom - top
        if width >= cell_width - 2 or height >= cell_height - 2:
            raise ValueError(f"The font glyph for {char!r} is too large")
        drawer.text((x - left, y - top), char, font=font, fill=255, anchor="ls")
        glyphs.append(f"    {{{ord(char)}, {x}, {y}, {width}, {height}, {left}, {top}, {advance}}},")
    return mask, f"#define FONT_SIZE {size}\n" + "static const Glyph glyphs[] = {\n" + "\n".join(glyphs) + "\n};\n"


def main():
    root = Path(__file__).resolve().parents[1]
    output = Path(sys.argv[1])
    output.mkdir(parents=True, exist_ok=True)
    letters = set(chr(code) for code in range(32, 127))
    letters.update((root / "assets/glyphs.txt").read_text(encoding="utf-8").strip())
    font = ImageFont.truetype(root / "assets/rounded.ttf", 32)
    atlas, glyphs = font_atlas(font, sorted(letters))
    mark = Image.open(root / "assets/mark.png").convert("RGBA")
    brand = brand_image(root / "assets/rounded.ttf")
    icon = Image.new("RGBA", (128, 48))
    icon.alpha_composite(mark.resize((48, 48), Image.Resampling.LANCZOS), (1, 0))
    icon_font = ImageFont.truetype(root / "assets/rounded.ttf", 15)
    icon_draw = ImageDraw.Draw(icon)
    for offset, color in [((50, 24), "#202750"), ((49, 23), "#f0f7ff")]:
        icon_draw.text(offset, "Rift Store", font=icon_font, fill=color, anchor="lm")
    icon.save(output / "icon.png")
    with (output / "store_assets.h").open("w", encoding="utf-8", newline="\n") as header:
        header.write(f"#define FONT_WIDTH {atlas.width}\n#define FONT_HEIGHT {atlas.height}\n")
        header.write(f"#define BRAND_WIDTH {brand.width}\n#define BRAND_HEIGHT {brand.height}\n")
        header.write(f"#define CATEGORY_CELL {CELL}\n")
        header.write(f"#define POINTER_SIZE {SIZE}\n#define POINTER_TIP_X {HOTSPOT[0]}\n#define POINTER_TIP_Y {HOTSPOT[1]}\n")
        header.write(glyphs)
        header.write(texture("font_pixels", gx_white_ia8(atlas)))
        header.write(texture("brand_pixels", gx_rgba(brand)))
        header.write(texture("category_pixels", gx_rgba(category_atlas())))
        header.write(texture("pointer_pixels", gx_rgba(pointer_image())))


if __name__ == "__main__":
    main()
