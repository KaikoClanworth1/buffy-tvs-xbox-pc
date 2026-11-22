"""A picture of what is inside .ico files: each one's own 256, 48, 32 and 16 px images at actual
size, on a light and on a dark background (and the 16 and 32 px ones again, enlarged 4 times).

    python port/icon/ico_sheet.py <out.png> "<label>=<file.ico>" ["<label>=<file.ico>" ...]

The images are the ones stored in the file, not rescaled here. Write the sheet to port/runs/shots
(ignored): an .ico made from a disc is game material and neither it nor its picture is committed.
"""
import io
import os
import struct
import sys

from PIL import Image, ImageDraw, ImageFont


def images(path):
    d = open(path, 'rb').read()
    out = {}
    for i in range(struct.unpack_from('<H', d, 4)[0]):
        w, _, _, _, _, _, n, off = struct.unpack_from('<BBBBHHII', d, 6 + 16 * i)
        w = w or 256
        b = d[off:off + n]
        if b[:4] == b'\x89PNG':
            out[w] = Image.open(io.BytesIO(b)).convert('RGBA')
        else:                                   # 32-bit bitmap, bottom row first
            out[w] = Image.frombytes('RGBA', (w, w), b[40:40 + w * w * 4], 'raw', 'BGRA', 0, -1)
    return out


def main():
    if len(sys.argv) < 3:
        sys.stderr.write(__doc__)
        return 2
    rows = [a.split('=', 1) for a in sys.argv[2:]]
    show = (256, 48, 32, 16)
    pad, label_h = 24, 26
    half_w = pad + sum(s + pad for s in show) + 128 + pad + 64 + pad
    row_h = 256 + pad * 2 + label_h
    im = Image.new('RGB', (half_w * 2, row_h * len(rows)), (255, 255, 255))
    d = ImageDraw.Draw(im)
    try:
        small = ImageFont.truetype('segoeui.ttf', 13)
    except OSError:
        small = ImageFont.load_default()
    for r, (label, path) in enumerate(rows):
        pics = images(path)
        y0 = r * row_h
        for h, bg, fg in ((0, (243, 243, 243), (30, 30, 30)), (1, (32, 32, 32), (230, 230, 230))):
            d.rectangle([h * half_w, y0, (h + 1) * half_w, y0 + row_h], fill=bg)
            d.text((h * half_w + pad, y0 + 4), label, fill=fg, font=small)
            x = h * half_w + pad
            base = y0 + label_h + pad + 256
            for s in show:
                im.paste(pics[s], (x, base - s), pics[s])
                d.text((x, base + 4), str(s), fill=fg, font=small)
                x += s + pad
            for s in (32, 16):
                z = pics[s].resize((s * 4, s * 4), Image.NEAREST)
                im.paste(z, (x, base - s * 4), z)
                d.text((x, base + 4), '%d, 4x' % s, fill=fg, font=small)
                x += s * 4 + pad
    os.makedirs(os.path.dirname(os.path.abspath(sys.argv[1])), exist_ok=True)
    im.save(sys.argv[1])
    print('wrote', sys.argv[1])
    return 0


if __name__ == '__main__':
    sys.exit(main())
