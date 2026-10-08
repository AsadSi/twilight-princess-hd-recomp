# Contact sheet of a route's shots: build/venv/bin/python tools/audit/sheet.py build/audit/<route> -> sheet.png
import sys, glob, os
from PIL import Image, ImageDraw
d = sys.argv[1]
files = sorted((f for f in glob.glob(os.path.join(d, 't*.png')) if '_' not in os.path.basename(f)),
               key=lambda f: float(os.path.basename(f)[1:-4]))
ims = [Image.open(f) for f in files]
w, h = 480, 280
cols = 3
rows = (len(ims) + cols - 1) // cols
sheet = Image.new('RGB', (cols * w, rows * h), 'white')
for i, (f, im) in enumerate(zip(files, ims)):
    im = im.convert('RGB'); im.thumbnail((w, h - 20))
    x, y = (i % cols) * w, (i // cols) * h
    sheet.paste(im, (x, y + 20))
    ImageDraw.Draw(sheet).text((x + 4, y + 4), os.path.basename(f)[:-4], fill='black')
sheet.save(os.path.join(d, 'sheet.png'))
