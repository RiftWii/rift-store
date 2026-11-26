# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Draw Rift Store's bag and two-tone wordmark for the Wii header."""

from PIL import Image, ImageDraw, ImageFont


SCALE = 4
WIDTH, HEIGHT = 308, 56


def px(points):
    return tuple(round(value * SCALE) for value in points)


def brand_image(font_path):
    image = Image.new("RGBA", (WIDTH * SCALE, HEIGHT * SCALE))
    painter = ImageDraw.Draw(image)

    painter.ellipse(px((2, 47, 57, 55)), fill="#16465635")
    # A rear lip, a folded side and a face with hardware for the handle.
    painter.polygon([px(point) for point in [(12, 13), (46, 13), (52, 18), (53, 46), (43, 53), (5, 50), (8, 19)]],
                    fill="#0b587e")
    painter.polygon([px(point) for point in [(41, 21), (50, 19), (51, 45), (43, 51)]], fill="#16779d")
    painter.polygon([px(point) for point in [(47, 21), (50, 19), (51, 45), (47, 48)]], fill="#094970")
    painter.line([px((47, 24)), px((47, 42)), px((43, 47))], fill="#4bbdd9", width=SCALE)
    painter.polygon([px(point) for point in [(12, 15), (45, 15), (49, 18), (41, 22), (9, 19)]], fill="#114b70")
    painter.polygon([px(point) for point in [(9, 19), (40, 21), (42, 50), (6, 48)]], fill="#0b9ecb")
    painter.polygon([px(point) for point in [(9, 19), (40, 21), (41, 33), (7, 37)]], fill="#40c6e3")
    painter.line([px((10, 20)), px((39, 22))], fill="#b7f2ff", width=SCALE)
    painter.line([px((7, 47)), px((41, 49))], fill="#077ca6", width=SCALE)

    handle = [(15, 25), (15, 15), (16, 9), (20, 5), (24, 4), (29, 5), (33, 9), (34, 15), (34, 26)]
    painter.ellipse(px((12, 22, 18, 28)), fill="#0b6089")
    painter.ellipse(px((31, 23, 37, 29)), fill="#0b6089")
    painter.line([px(point) for point in handle], fill="#143d5b", width=5 * SCALE, joint="curve")
    painter.line([px((x, y - .35)) for x, y in handle], fill="#ccf8ff", width=2 * SCALE, joint="curve")
    crack = [(25, 26), (17, 36), (24, 36), (20, 46), (33, 33), (26, 33), (30, 26)]
    painter.polygon([px(point) for point in crack], fill="#f1fbff")

    wordmark = Image.new("RGBA", ((WIDTH - 64) * SCALE, HEIGHT * SCALE))
    letters = ImageDraw.Draw(wordmark)
    primary = ImageFont.truetype(font_path, 43 * SCALE)
    secondary = ImageFont.truetype(font_path, 30 * SCALE)
    letters.text(px((0, 44)), "RIFT", font=primary, fill="#18394e", anchor="ls")
    store_x = 137
    for letter in "STORE":
        letters.text(px((store_x, 44)), letter, font=secondary, fill="#087daa", anchor="ls")
        store_x += secondary.getlength(letter) / SCALE + 1.2
    letters.line([px((139, 50)), px((store_x - 2, 50))], fill="#6ccee7", width=2 * SCALE)
    wordmark = wordmark.transform(wordmark.size, Image.Transform.AFFINE, (1, .11, -6 * SCALE, 0, 1, 0),
                                  resample=Image.Resampling.BICUBIC)
    image.alpha_composite(wordmark, px((64, 1)))

    # The bolt sits in the gap like an enamel badge with a beveled edge.
    bolt = [(115, 9), (131, 9), (121, 24), (131, 24), (104, 50), (111, 32), (102, 32)]
    painter.polygon([px((x + 65, y + 1.5)) for x, y in bolt], fill="#13537334")
    painter.polygon([px((x + 64, y)) for x, y in bolt], fill="#1194c2", outline="#126a98", width=SCALE)
    glint = [(116, 11), (127, 11), (117, 26), (125, 26), (115, 35), (114, 30), (106, 30)]
    painter.polygon([px((x + 64, y)) for x, y in glint], fill="#72daf1")
    painter.line([px((180, 11)), px((190, 11))], fill="#e2fbff", width=SCALE)
    return image.resize((WIDTH, HEIGHT), Image.Resampling.LANCZOS)
