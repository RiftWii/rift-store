# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Original cyan badges and white pictograms for the store's category tiles."""

from PIL import Image, ImageDraw


CELL = 48
SCALE = 4


def xy(values):
    return tuple(round(value * SCALE) for value in values)


class Mark:
    white = "#f9fdff"
    blue = "#129bc3"

    def __init__(self, image):
        self.draw = ImageDraw.Draw(image)

    def box(self, x, y, width, height, radius=0, *, cut=False):
        self.draw.rounded_rectangle(xy((x, y, x + width, y + height)), round(radius * SCALE),
                                    fill=self.blue if cut else self.white)

    def dot(self, x, y, radius, *, cut=False):
        self.draw.ellipse(xy((x - radius, y - radius, x + radius, y + radius)),
                          fill=self.blue if cut else self.white)

    def line(self, points, width, *, cut=False):
        self.draw.line([xy(point) for point in points], fill=self.blue if cut else self.white,
                       width=round(width * SCALE), joint="curve")
        self.dot(*points[0], width / 2, cut=cut)
        self.dot(*points[-1], width / 2, cut=cut)

    def polygon(self, points, *, cut=False):
        self.draw.polygon([xy(point) for point in points], fill=self.blue if cut else self.white)

    def arc(self, bounds, start, end, width):
        self.draw.arc(xy(bounds), start, end, fill=self.white, width=round(width * SCALE))


def games(m):
    m.box(9, 17, 30, 16, 7)
    m.line([(13, 25), (21, 25)], 2.2, cut=True)
    m.line([(17, 21), (17, 29)], 2.2, cut=True)
    m.dot(29, 27.5, 1.8, cut=True)
    m.dot(33, 22.5, 1.8, cut=True)


def emulators(m):
    m.box(17, 12, 20, 16, 3)
    m.box(19.5, 14.5, 15, 10, 1, cut=True)
    m.box(10, 19, 21, 16, 3, cut=True)
    m.box(10, 20, 19, 16, 3)
    m.box(12.5, 22.5, 14, 9, 1, cut=True)


def loaders(m):
    m.dot(24, 24, 13)
    m.dot(24, 24, 4, cut=True)
    m.line([(18, 19), (21, 17)], 1.7, cut=True)
    m.line([(27, 31), (30, 28.5)], 1.7, cut=True)


def system(m):
    for x, y in ((15, 20), (24, 29), (33, 17)):
        m.line([(x, 12), (x, 36)], 2.2)
        m.box(x - 3.5, y - 3, 7, 6, 1.8, cut=True)
        m.box(x - 3.5, y - 2, 7, 4, 1.6)


def game_tools(m):
    m.line([(15, 34), (27, 22)], 6)
    m.dot(30, 18, 8)
    m.polygon([(30, 15), (37, 10), (40, 14), (34, 22), (30, 21)], cut=True)
    m.dot(15, 34, 1.4, cut=True)


def media(m):
    m.box(10, 14, 28, 20, 4)
    m.polygon([(21, 18.5), (30.5, 24), (21, 29.5)], cut=True)


def channels(m):
    for row in range(2):
        for column in range(3):
            m.box(10.5 + column * 9.5, 16 + row * 9.5, 7.5, 7, 1.5)


def controllers(m):
    m.box(12, 10, 13, 28, 4)
    m.line([(15.5, 19), (21.5, 19)], 1.7, cut=True)
    m.line([(18.5, 16), (18.5, 22)], 1.7, cut=True)
    m.dot(18.5, 27, 1.65, cut=True)
    m.dot(17, 33.5, 0.75, cut=True)
    m.dot(20, 33.5, 0.75, cut=True)
    m.arc((22, 15, 34, 29), 302, 58, 2.2)
    m.arc((22, 11, 39, 33), 304, 56, 2.2)


def creative(m):
    m.line([(23, 24), (34, 13)], 4.5)
    m.polygon([(21, 24), (26, 28), (21, 33), (13, 36), (15, 29)])
    m.line([(21.5, 23.5), (26.5, 28)], 1.8, cut=True)
    m.dot(13, 20, 1.8)
    m.dot(29, 34, 1.4)


def toys(m):
    m.polygon([(22, 13), (25, 20), (32, 22.5), (25, 25.5), (22, 33), (19, 25.5), (12, 22.5), (19, 20)])
    m.line([(33, 12), (33, 18)], 1.8)
    m.line([(30, 15), (36, 15)], 1.8)
    m.dot(32, 32, 1.6)


def internet(m):
    m.arc((11, 11, 37, 37), 0, 360, 2.3)
    m.arc((18, 11, 30, 37), 0, 360, 1.8)
    m.line([(12.5, 24), (35.5, 24)], 1.7)


def online(m):
    for radius in (8, 13, 18):
        m.arc((24 - radius, 32 - radius, 24 + radius, 32 + radius), 225, 315, 2.8)
    m.dot(24, 32, 2.6)


def more(m):
    for x in (15, 24, 33):
        m.dot(x, 24, 2.7)


# This order matches the categories in source/main.c.
PICTOGRAMS = (games, emulators, loaders, system, game_tools, media, channels, controllers,
              creative, toys, internet, online, more)


def category_atlas():
    square = CELL * SCALE
    badge = Image.new("RGBA", (square, square))
    mask = Image.new("L", badge.size)
    ImageDraw.Draw(mask).rounded_rectangle(xy((2, 2, 46, 46)), 11 * SCALE, fill=255)
    painter = ImageDraw.Draw(badge)
    for y in range(square):
        fraction = y / (square - 1)
        color = tuple(round(top + (bottom - top) * fraction)
                      for top, bottom in zip((49, 197, 225), (0, 126, 174)))
        painter.line((0, y, square - 1, y), fill=(*color, 255))
    badge.putalpha(mask)
    painter.rounded_rectangle(xy((2.5, 2.5, 45.5, 45.5)), 10 * SCALE, outline="#087ca6", width=SCALE)
    painter.arc(xy((4.2, 4.2, 21, 21)), 181, 270, fill="#8ee5f5", width=SCALE)
    painter.line(xy((12.5, 4.2, 35.5, 4.2)), fill="#8ee5f5", width=SCALE)
    painter.arc(xy((27, 4.2, 43.8, 21)), 270, 360, fill="#8ee5f5", width=SCALE)

    atlas = Image.new("RGBA", (CELL * 4, CELL * 4))
    for index, draw_mark in enumerate(PICTOGRAMS):
        sprite = badge.copy()
        draw_mark(Mark(sprite))
        atlas.paste(sprite.resize((CELL, CELL), Image.Resampling.LANCZOS), (index % 4 * CELL, index // 4 * CELL))
    return atlas
