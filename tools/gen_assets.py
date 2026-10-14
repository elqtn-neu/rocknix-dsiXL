#!/usr/bin/env python3
"""gen_assets.py: draws assets/boot_logo.png, the logo of the boot screen, in the layout of the DSi's
health and safety screen (NINTENDO, the two screens, DS, the i), at 1:1 DS pixels.

It's an approximation drawn from M PLUS 1p (tools/fonts). To have the real one, replace assets/boot_logo.png with
your own picture (transparent PNG, about 216 x 32, black on transparent): the menu centres whatever is there.

    python3 tools/gen_assets.py      (needs Pillow)
"""
import os
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'assets')
S = 8                                    # drawn at 8x, then box-filtered down: smooth, but DS-sized


def font(name, px):
    return ImageFont.truetype(os.path.join(HERE, 'fonts', name), px * S)


def word(text, f, stretch, fill=255):
    """the text as an alpha mask, stretched horizontally"""
    bb = f.getbbox(text)
    im = Image.new('L', (bb[2] - bb[0] + 4 * S, bb[3] - bb[1] + 4 * S), 0)
    ImageDraw.Draw(im).text((2 * S - bb[0], 2 * S - bb[1]), text, font=f, fill=fill)
    im = im.crop(im.getbbox())
    return im.resize((int(im.width * stretch), im.height), Image.LANCZOS)


def boot_logo():
    W, H = 220 * S, 34 * S
    alpha = Image.new('L', (W, H), 0)
    grey = Image.new('L', (W, H), 0)              # where the grey (lower screen) goes
    x = 1 * S
    nin = word('NINTENDO', font('MPLUS1p-Bold.ttf', 15), 1.36)
    nin = nin.resize((nin.width, 12 * S), Image.LANCZOS)
    alpha.paste(nin, (x, 4 * S), nin)
    x += nin.width + 1 * S
    # the two screens: the upper one black, the lower one grey
    d, g = ImageDraw.Draw(alpha), ImageDraw.Draw(grey)
    sq = 16 * S
    d.rounded_rectangle((x, 2 * S, x + sq, 16 * S), radius=3 * S, outline=255, width=int(2.4 * S))
    g.rounded_rectangle((x, 18 * S, x + sq, 32 * S), radius=3 * S, outline=255, width=int(2.6 * S))
    x += sq + 1 * S
    ds = word('DS', font('MPLUS1p-Black.ttf', 40), 1.30)
    ds = ds.resize((ds.width, 31 * S), Image.LANCZOS)
    alpha.paste(ds, (x, 1 * S), ds)
    x += ds.width + 4 * S
    # the i in a circle
    r = 7 * S
    d.ellipse((x, 1 * S, x + 2 * r, 1 * S + 2 * r), fill=255)
    cut = Image.new('L', (W, H), 0)
    c = ImageDraw.Draw(cut)
    cx = x + r
    c.ellipse((cx - int(1.4 * S), 3 * S, cx + int(1.4 * S), int(5.8 * S)), fill=255)
    c.rectangle((cx - int(1.3 * S), int(6.8 * S), cx + int(1.3 * S), 13 * S), fill=255)
    alpha = Image.composite(Image.new('L', (W, H), 0), alpha, cut)
    x += 2 * r
    w = x + 1 * S
    rgba = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    black = Image.new('RGBA', (W, H), (0, 0, 0, 255))
    rgba.paste(black, (0, 0), alpha)
    rgba.paste(Image.new('RGBA', (W, H), (160, 160, 160, 255)), (0, 0), grey)
    rgba = rgba.crop((0, 0, w, H)).crop(rgba.crop((0, 0, w, H)).getbbox())
    out = rgba.resize((max(1, rgba.width // S), max(1, rgba.height // S)), Image.BOX)
    out.save(os.path.join(OUT, 'boot_logo.png'))
    print('boot_logo.png', out.size)


if __name__ == '__main__':
    os.makedirs(OUT, exist_ok=True)
    boot_logo()
