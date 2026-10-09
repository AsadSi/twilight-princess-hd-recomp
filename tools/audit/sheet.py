# Contact sheet -> <dir>/sheet.png: build/venv/bin/python tools/audit/sheet.py build/audit/<route> (a route's shots,
# by time) or tools/audit/sheet.py <dir> '<glob>' (the audits: one shot a place, e.g. '*_b.png', by name).
import sys, glob, os
from PIL import Image, ImageDraw
d = sys.argv[1]
if len(sys.argv) > 2:
    files = sorted(glob.glob(os.path.join(d, sys.argv[2])))
    w, h, cols = 320, 200, 5
else:
    files = sorted((f for f in glob.glob(os.path.join(d, 't*.png')) if '_' not in os.path.basename(f)),
                   key=lambda f: float(os.path.basename(f)[1:-4]))
    w, h, cols = 480, 280, 3
if not files: sys.exit()
rows = (len(files) + cols - 1) // cols
sheet = Image.new('RGB', (cols * w, rows * h), 'white')
for i, f in enumerate(files):
    im = Image.open(f).convert('RGB'); im.thumbnail((w, h - 20))
    x, y = (i % cols) * w, (i // cols) * h
    sheet.paste(im, (x, y + 20))
    name = os.path.basename(f)[:-4]
    ImageDraw.Draw(sheet).text((x + 4, y + 4), name[:-2] if len(sys.argv) > 2 else name, fill='black')
sheet.save(os.path.join(d, 'sheet.png'))
