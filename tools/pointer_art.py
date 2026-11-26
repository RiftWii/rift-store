# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Draw an original pointing glove with its hotspot at the index fingertip."""

from PIL import Image, ImageDraw


SIZE = 48
HOTSPOT = (17, 4)
SCALE = 4


def outline(start, segments):
    points = [start]
    for control_a, control_b, end in segments:
        begin = points[-1]
        for step in range(1, 13):
            t = step / 12
            s = 1 - t
            points.append(tuple(s**3 * a + 3 * s**2 * t * b + 3 * s * t**2 * c + t**3 * d
                                for a, b, c, d in zip(begin, control_a, control_b, end)))
    return points


def pixels(points, offset=(0, 0)):
    return [(round((x + offset[0]) * SCALE), round((y + offset[1]) * SCALE)) for x, y in points]


def pointer_image():
    shape = outline(HOTSPOT, (
        ((20, 3.2), (22, 5.5), (22.5, 8.2)),
        ((23.3, 11.5), (24.2, 15.7), (24.7, 18.3)),
        ((27, 15), (31.6, 15.6), (33, 19.4)),
        ((35.7, 17.7), (39.2, 19), (40, 22.2)),
        ((44.5, 21.5), (45.4, 26.5), (43.8, 31.6)),
        ((43, 35.6), (40.7, 37.2), (39.5, 39)),
        ((39, 40.4), (39.4, 42.9), (38.4, 43.5)),
        ((33.8, 45), (24, 44.7), (19.1, 43.8)),
        ((17.6, 43.2), (18.8, 40.7), (17.8, 39)),
        ((15.2, 35.1), (11.7, 31.7), (8.8, 28.8)),
        ((3.2, 23), (8.8, 17.7), (13.3, 21.2)),
        ((15.2, 22.7), (17, 24.6), (18.8, 26.1)),
        ((16.7, 19.4), (15.1, 13.5), (13.7, 10.6)),
        ((12.2, 7.7), (14, 4.7), HOTSPOT),
    ))
    art = Image.new("RGBA", (SIZE * SCALE, SIZE * SCALE))
    draw = ImageDraw.Draw(art)
    draw.polygon(pixels(shape, (1.5, 2)), fill="#17384e66")
    draw.polygon(pixels(shape), fill="#fcfeff")

    shade = outline((9, 27.5), (
        ((12, 29.5), (14, 30.7), (16.9, 32.2)),
        ((25, 36.5), (35.8, 36.4), (41.5, 33.4)),
        ((41.3, 35.8), (39.6, 37.8), (39.5, 39.3)),
        ((39.4, 40.9), (39.1, 43), (38, 43.6)),
        ((31, 44.7), (25, 44.4), (19.4, 43.8)),
        ((18.6, 43.6), (18.4, 41.4), (18, 39)),
        ((15.5, 34.6), (12.3, 31.1), (9, 27.5)),
    ))
    draw.polygon(pixels(shade), fill="#bfe7f6")
    draw.line(pixels(outline((18.2, 39.1), (((24, 40.8), (34, 40.8), (39.3, 39.1)),))),
              fill="#72b7d5", width=5)
    for start, end in (((25.1, 20), (26.4, 24)), ((33.2, 21), (34, 24.2)), ((40, 23.4), (40.4, 25.6))):
        draw.line(pixels((start, end)), fill="#749cb1", width=5)
    draw.line(pixels(shape), fill="#233d50", width=10, joint="curve")
    return art.resize((SIZE, SIZE), Image.Resampling.LANCZOS)
