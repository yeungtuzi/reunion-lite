#!/usr/bin/env python3
"""Builds an .iconset from the Reunion client icon (ico) if available."""
import os, sys
from PIL import Image

ico = sys.argv[1]
out = sys.argv[2]
img = Image.open(ico)
# pick the largest frame
frames = []
try:
    for i in range(getattr(img, "n_frames", 1)):
        img.seek(i)
        frames.append(img.copy())
except Exception:
    frames.append(img)
best = max(frames, key=lambda im: im.size[0] * im.size[1]).convert("RGBA")
os.makedirs(out, exist_ok=True)
for size in (16, 32, 64, 128, 256, 512, 1024):
    resized = best.resize((size, size), Image.LANCZOS)
    name = f"icon_{size}x{size}.png" if size <= 512 else "icon_512x512@2x.png"
    resized.save(os.path.join(out, name))
print(out)
