#!/usr/bin/env python3
"""Build a throwaway sandbox to run the launcher without any real content:
the repository's db.json (or another one), an empty file for every ROM,
placeholder logos / screenshots / box covers, and the test core copied
under every core name db.json uses.

    make_sandbox.py <repo> <out_dir> <test_core.so> [db.json]

Needs Pillow (pip install pillow)."""
import hashlib
import json
import os
import random
import shutil
import sys

from PIL import Image, ImageDraw, ImageFont

REPO, OUT, CORE = sys.argv[1], sys.argv[2], sys.argv[3]
DB_SRC = sys.argv[4] if len(sys.argv) > 4 else os.path.join(REPO, "db.json")

EXT = {"NES": "nes", "SNES": "sfc", "SNES (Satellaview)": "sfc", "GBC": "gbc", "GB": "gb",
       "GBC (Fan Remake)": "gbc", "GBA": "gba", "N64": "z64", "CD-i": "cue"}
SHOT_SIZE = {"nes": (256, 240), "sfc": (256, 224), "gb": (160, 144), "gbc": (160, 144),
             "gba": (240, 160), "z64": (320, 240), "cue": (384, 280)}

os.makedirs(OUT, exist_ok=True)
for d in ("imgs", "sounds"):
    shutil.copytree(os.path.join(REPO, d), os.path.join(OUT, d), dirs_exist_ok=True)
for f in ("alagard.ttf", "branding.json"):
    shutil.copy(os.path.join(REPO, f), OUT)
shutil.copy(DB_SRC, os.path.join(OUT, "db.json"))
db = json.load(open(DB_SRC, encoding="utf-8"))
FONT = os.path.join(REPO, "alagard.ttf")


def color_for(s):
    h = hashlib.md5(s.encode()).digest()
    return (60 + h[0] % 150, 60 + h[1] % 150, 60 + h[2] % 150)


def target(path):
    full = os.path.join(OUT, path)
    if os.path.exists(full):
        return None
    os.makedirs(os.path.dirname(full), exist_ok=True)
    return full


def make_logo(path, text, sub=None):
    full = target(path)
    if not full:
        return
    W, H = 640, 240
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    words = text.split()
    lines = [text]
    if len(text) > 16 and len(words) > 1:
        mid = len(words) // 2
        lines = [" ".join(words[:mid]), " ".join(words[mid:])]
    size = 80 if len(lines) == 1 else 64
    fnt = ImageFont.truetype(FONT, size)
    y = (H - len(lines) * (size + 6)) // 2 - (14 if sub else 0)
    for ln in lines:
        tw = d.textbbox((0, 0), ln, font=fnt)[2]
        d.text(((W - tw) // 2, y), ln, font=fnt, fill=(242, 200, 60, 255), stroke_width=5,
               stroke_fill=(40, 25, 5, 255))
        y += size + 6
    if sub:
        f2 = ImageFont.truetype(FONT, 34)
        tw = d.textbbox((0, 0), sub, font=f2)[2]
        d.text(((W - tw) // 2, y + 4), sub, font=f2, fill=(160, 230, 190, 255), stroke_width=3,
               stroke_fill=(10, 30, 20, 255))
    img.crop(img.getbbox()).save(full)


def make_shot(path, label, ext, idx):
    full = target(path)
    if not full:
        return
    w, h = SHOT_SIZE.get(ext, (256, 224))
    base = color_for(path)
    img = Image.new("RGB", (w, h), base)
    d = ImageDraw.Draw(img)
    rnd = random.Random(path)
    t = 16 if w > 200 else 8
    for ty in range(0, h, t):
        for tx in range(0, w, t):
            if rnd.random() < 0.25:
                c = tuple(max(0, min(255, v + rnd.randint(-50, 50))) for v in base)
                d.rectangle([tx, ty, tx + t - 1, ty + t - 1], fill=c)
    px, py = rnd.randint(t, w - 2 * t), rnd.randint(t, h - 2 * t)
    d.rectangle([px, py, px + t - 1, py + t - 1], fill=(40, 170, 60))
    f = ImageFont.truetype(FONT, 16 if w > 200 else 10)
    d.rectangle([0, 0, w, 20 if w > 200 else 13], fill=(0, 0, 0))
    d.text((4, 2), f"{label} #{idx + 1}", font=f, fill=(255, 255, 255))
    img.save(full)


def make_cover(path, title):
    """A portrait box-front scan stand-in: gradient, title, "BOX ART"."""
    full = target(path)
    if not full:
        return
    w, h = 480, 640
    top, bot = color_for(path), color_for(path[::-1])
    img = Image.new("RGB", (w, h))
    d = ImageDraw.Draw(img)
    for y in range(h):
        t = y / (h - 1)
        d.line([(0, y), (w, y)], fill=tuple(int(a + (b - a) * t) for a, b in zip(top, bot)))
    f = ImageFont.truetype(FONT, 56)
    for i, ln in enumerate(("BOX ART", title[:18])):
        tw = d.textbbox((0, 0), ln, font=f)[2]
        d.text(((w - tw) // 2, 220 + i * 80), ln, font=f, fill=(245, 225, 170), stroke_width=3,
               stroke_fill=(30, 15, 10))
    img.save(full)


cores = set(info.get("core", "") for info in db.get("extensions", {}).values())
for key, g in db["games"].items():
    title = g.get("title", key)
    logo = g.get("logo")
    if logo:
        make_logo(logo, title)
    if g.get("cover"):
        make_cover(g["cover"], title)
    if g.get("core"):
        cores.add(g["core"])
    folder = os.path.dirname(logo) if logo else "roms/misc"
    for e in g.get("entries", []):
        ext = EXT.get(e.get("platform", ""), "sfc")
        stem = e.get("stem")
        if e.get("core"):
            cores.add(e["core"])
        if stem and "external" not in e:
            rom = os.path.join(OUT, folder, stem + "." + ext)
            os.makedirs(os.path.dirname(rom), exist_ok=True)
            open(rom, "wb").close()
        if e.get("logo"):
            make_logo(e["logo"], title, e.get("title"))
        if e.get("cover"):
            make_cover(e["cover"], e.get("title", title))
        for i, s in enumerate(e.get("screenshots", [])):
            make_shot(s, e.get("title", stem or "shot"), ext, i)

os.makedirs(os.path.join(OUT, "cores"), exist_ok=True)
for c in sorted(x for x in cores if x):
    shutil.copy(CORE, os.path.join(OUT, "cores", c))
print("sandbox ready at", OUT, "-", len(db["games"]), "games,", len(cores), "cores")
