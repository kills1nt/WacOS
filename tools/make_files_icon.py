# Files icon for the WacOS Dock (Finder slot): Liquid Glass squircle with two document sheets.
# python make_files_icon.py <out.png>
import sys
from PIL import Image, ImageDraw, ImageFilter, ImageChops

S = 1024
def squircle_mask(size, box, n=5.0):
    m = Image.new('L', (size, size), 0)
    x0, y0, x1, y1 = box
    cx, cy, a, b = (x0 + x1) / 2, (y0 + y1) / 2, (x1 - x0) / 2, (y1 - y0) / 2
    px = m.load()
    for y in range(int(y0), int(y1) + 1):
        for x in range(int(x0), int(x1) + 1):
            v = abs((x - cx) / a) ** n + abs((y - cy) / b) ** n
            if v <= 1: px[x, y] = 255
    return m.filter(ImageFilter.GaussianBlur(0.8))

def vgrad(size, top, bot):
    g = Image.new('RGBA', (1, size))
    for y in range(size):
        t = y / (size - 1)
        g.putpixel((0, y), tuple(int(top[i] + (bot[i] - top[i]) * t) for i in range(4)))
    return g.resize((size, size))

M = 100  # the Mac icon grid: 824 of 1024
body = (M, M, S - M, S - M)
mask = squircle_mask(S, body)
img = Image.new('RGBA', (S, S), (0, 0, 0, 0))

# drop shadow
sh = Image.new('RGBA', (S, S), (0, 0, 0, 0))
sh.putalpha(mask.point(lambda v: v * 0.45))
sh = sh.filter(ImageFilter.GaussianBlur(22))
img.alpha_composite(sh, (0, 14))

# body: deep indigo -> azure glass
bg = vgrad(S, (78, 160, 255, 255), (36, 72, 214, 255))
# soft radial glow lower-left
glow = Image.new('L', (S, S), 0)
ImageDraw.Draw(glow).ellipse((60, 520, 760, 1180), fill=150)
glow = glow.filter(ImageFilter.GaussianBlur(120))
bg = Image.composite(Image.new('RGBA', (S, S), (110, 220, 255, 255)), bg, glow)
layer = Image.new('RGBA', (S, S), (0, 0, 0, 0))
layer.paste(bg, (0, 0), mask)
img.alpha_composite(layer)

def sheet(box, r, fold):
    x0, y0, x1, y1 = box
    m = Image.new('L', (S, S), 0)
    d = ImageDraw.Draw(m)
    d.rounded_rectangle(box, r, fill=255)
    d.polygon([(x1 - fold, y0 - 2), (x1 + 2, y0 - 2), (x1 + 2, y0 + fold)], fill=0)  # cut corner
    return m

# back sheet (tilted feel: offset up-right, translucent)
back = (390, 230, 760, 700)
bm = sheet(back, 40, 120)
bs = Image.new('RGBA', (S, S), (255, 255, 255, 0)); bs.putalpha(bm.point(lambda v: v * 0.42))
img.alpha_composite(bs)

# front sheet
front = (270, 330, 650, 810)
fm = sheet(front, 44, 130)
fsh = Image.new('RGBA', (S, S), (8, 20, 80, 0)); fsh.putalpha(fm.point(lambda v: v * 0.35))
img.alpha_composite(fsh.filter(ImageFilter.GaussianBlur(18)), (0, 16))
fg = vgrad(S, (255, 255, 255, 255), (226, 236, 255, 255))
fl = Image.new('RGBA', (S, S), (0, 0, 0, 0)); fl.paste(fg, (0, 0), fm)
img.alpha_composite(fl)
# folded corner
x1, y0 = front[2], front[1]
cm = Image.new('L', (S, S), 0)
ImageDraw.Draw(cm).rounded_rectangle((x1 - 130, y0, x1, y0 + 130), 26, fill=255)
cm = ImageChops.multiply(cm, Image.eval(fm, lambda v: 255))  # inside bbox only
tri = Image.new('L', (S, S), 0)
ImageDraw.Draw(tri).polygon([(x1 - 130, y0), (x1, y0 + 130), (x1 - 130, y0 + 130)], fill=255)
corner = Image.new('RGBA', (S, S), (170, 196, 245, 255)); cl = Image.new('RGBA', (S, S), (0, 0, 0, 0)); cl.paste(corner, (0, 0), tri)
img.alpha_composite(cl)
# text lines
d = ImageDraw.Draw(img)
for i, w in enumerate([250, 280, 220, 260]):
    y = 520 + i * 62
    d.rounded_rectangle((front[0] + 55, y, front[0] + 55 + w, y + 22), 11, fill=(120, 150, 220, 255))

# glass: top sheen + specular rim
sheen = Image.new('L', (S, S), 0)
ImageDraw.Draw(sheen).ellipse((M - 300, M - 700, S - M + 300, 520), fill=60)
sheen = ImageChops.multiply(sheen.filter(ImageFilter.GaussianBlur(140)), mask)
img.alpha_composite(Image.merge('RGBA', (Image.new('L', (S, S), 255),) * 3 + (sheen,)))
rim = ImageChops.subtract(mask, squircle_mask(S, (M + 5, M + 5, S - M - 5, S - M - 5)))
rimg = vgrad(S, (255, 255, 255, 200), (255, 255, 255, 50))
rl = Image.new('RGBA', (S, S), (0, 0, 0, 0)); rl.paste(rimg, (0, 0), rim)
img.alpha_composite(rl)

img.resize((512, 512), Image.LANCZOS).save(sys.argv[1])
print('ok', sys.argv[1])
