// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <gccore.h>
#include <stdbool.h>

bool DrawInit(void);
int ScreenWidth(void);
void DrawContentSpace(bool inset);
void ScreenToContent(float *x, float *y);
void DrawEnd(void);
void Gradient(float x, float y, float width, float height, u32 top, u32 bottom);
void Box(float x, float y, float width, float height, u32 color);
void Panel(float x, float y, float width, float height, float radius, u32 top, u32 bottom);
void Triangle(float x1, float y1, float x2, float y2, float x3, float y3, u32 color);
void Wave(float base, float amplitude, float height, float phase, u32 top, u32 bottom);
void Brand(float x, float y);
void DrawPointer(float x, float y, u32 tint);
void LoadingRing(float x, float y, float radius, u32 milliseconds);
void CategoryIcon(unsigned category, float x, float y, float size);
void AppIcon(unsigned icon, float x, float y, float width, float height, float radius);
float TextWidth(const char *text, float size);
void Text(const char *text, float x, float baseline, float size, float limit, u32 color);
void Paragraph(const char *text, float x, float baseline, float size, float width, unsigned lines, u32 color);
