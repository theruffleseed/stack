#!/usr/bin/env python3
"""Pixel-accurate home-screen mockups for the PaperDeck rebrand.

Parses the firmware's own bitmap font tables (font12/16/20/24.cpp) and the
same geometric icon language as ui.cpp drawIcon(), then renders 480x800
1-bit panels exactly like the e-ink would show them. Output: design/*.png
(2x scale for easy viewing) plus a side-by-side sheet.

Usage: python tools/mockup_home.py
"""

import re
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
FW = ROOT / "firmware" / "StackWallet"
OUT = ROOT / "design"
W, H = 480, 800  # panel, portrait


# ---------------------------------------------------------------------------
# Firmware font tables -> pixel bitmaps
# ---------------------------------------------------------------------------

def load_font(path):
    src = path.read_text(encoding="utf-8", errors="replace")
    m = re.search(r"const uint8_t \w+_Table\s*\[\s*\]\s*=\s*\{(.*?)\n\};", src, re.S)
    body = re.sub(r"//[^\n]*", "", m.group(1))
    data = [int(b, 16) for b in re.findall(r"0x([0-9A-Fa-f]{2})", body)]
    sm = re.search(r"sFONT \w+ = \{\s*\w+,\s*(\d+),\s*/\* Width \*/\s*(\d+),\s*/\* Height \*/", src)
    w, h = int(sm.group(1)), int(sm.group(2))
    bpr = (w + 7) // 8  # bytes per row, MSB first
    stride = bpr * h
    assert len(data) == stride * 95, f"{path.name}: {len(data)} != {stride * 95}"
    return {"data": data, "w": w, "h": h, "bpr": bpr, "stride": stride}


FONTS = {n: load_font(FW / f"font{n}.cpp") for n in (12, 16, 20, 24)}


def draw_text(img, xy, s, size, ink=0):
    """Monospaced text exactly like Paint_DrawString_EN (left-aligned)."""
    f = FONTS[size]
    x0, y0 = xy
    px = img.load()
    for ci, ch in enumerate(s):
        idx = ord(ch) - 32
        if not 0 <= idx < 95:
            idx = ord("?") - 32
        base = idx * f["stride"]
        for y in range(f["h"]):
            off = base + y * f["bpr"]
            for x in range(f["w"]):
                if f["data"][off + x // 8] & (0x80 >> (x % 8)):
                    px[x0 + ci * f["w"] + x, y0 + y] = ink


def text_w(s, size):
    return len(s) * FONTS[size]["w"]


# ---------------------------------------------------------------------------
# Primitives mirroring GUI_Paint (1px lines, inclusive rect ends)
# ---------------------------------------------------------------------------

def rect(d, x1, y1, x2, y2, fill=True, ink=0):
    d.rectangle([x1, y1, x2, y2], fill=ink if fill else None, outline=ink if not fill else None, width=1)


def line(d, x1, y1, x2, y2, ink=0):
    d.line([x1, y1, x2, y2], fill=ink, width=1)


# ---------------------------------------------------------------------------
# Icons: same geometric language as ui.cpp drawIcon(), 32x32 box at (x, y)
# ---------------------------------------------------------------------------

def icon(d, name, x, y, ink=0):
    bg = 255  # punch holes in fills
    if name == "qr":
        def finder(fx, fy):
            rect(d, fx, fy, fx + 7, fy + 7)
            rect(d, fx + 1, fy + 1, fx + 6, fy + 6, fill=False, ink=bg)
            rect(d, fx + 2, fy + 2, fx + 5, fy + 5, ink=bg)
            rect(d, fx + 2, fy + 2, fx + 5, fy + 5)
        finder(x + 2, y + 2)
        finder(x + 22, y + 2)
        finder(x + 2, y + 22)
        rect(d, x + 14, y + 14, x + 15, y + 15)
        rect(d, x + 19, y + 11, x + 20, y + 12)
        rect(d, x + 24, y + 24, x + 25, y + 25)
    elif name == "card":  # ID / business card
        rect(d, x + 2, y + 5, x + 29, y + 26, fill=False)
        rect(d, x + 6, y + 8, x + 13, y + 13)
        line(d, x + 5, y + 18, x + 26, y + 18)
        line(d, x + 8, y + 21, x + 14, y + 21)
    elif name == "gift":  # loyalty card (rewards)
        rect(d, x + 4, y + 11, x + 27, y + 27, fill=False)  # box
        line(d, x + 4, y + 15, x + 27, y + 15)              # lid seam
        rect(d, x + 14, y + 11, x + 17, y + 27)             # vertical ribbon
        line(d, x + 8, y + 4, x + 15, y + 10)               # bow left
        line(d, x + 16, y + 10, x + 23, y + 4)              # bow right
    elif name == "ticket":  # pass (boarding/event)
        rect(d, x + 2, y + 4, x + 29, y + 27, fill=False)
        line(d, x + 19, y + 4, x + 19, y + 27)
        rect(d, x + 17, y + 2, x + 21, y + 5)
        rect(d, x + 17, y + 26, x + 21, y + 29)
    elif name == "calendar":  # NEW: month grid, header band, one marked day
        rect(d, x + 2, y + 4, x + 29, y + 27, fill=False)
        rect(d, x + 2, y + 4, x + 29, y + 10)  # filled header band
        line(d, x + 2, y + 10, x + 29, y + 10)
        rect(d, x + 7, y + 1, x + 9, y + 6)   # binder pins
        rect(d, x + 22, y + 1, x + 24, y + 6)
        # week rows of day dots
        for gy in (14, 19, 24):
            for gx in range(x + 6, x + 28, 5):
                d.point((gx, y + gy), fill=0)
        rect(d, x + 11, y + 12, x + 15, y + 16)  # today: filled day box
    elif name == "todo":
        rect(d, x + 4, y + 4, x + 27, y + 27, fill=False)
        line(d, x + 7, y + 16, x + 13, y + 22)
        line(d, x + 13, y + 22, x + 24, y + 9)
    elif name == "book":
        rect(d, x + 3, y + 5, x + 28, y + 26, fill=False)
        line(d, x + 8, y + 5, x + 8, y + 26)
        line(d, x + 12, y + 10, x + 25, y + 10)
        line(d, x + 12, y + 14, x + 25, y + 14)
        line(d, x + 12, y + 18, x + 25, y + 18)
    elif name == "settings":
        line(d, x + 3, y + 8, x + 28, y + 8)
        rect(d, x + 11, y + 5, x + 16, y + 11)
        line(d, x + 3, y + 16, x + 28, y + 16)
        rect(d, x + 20, y + 13, x + 25, y + 19)
        line(d, x + 3, y + 24, x + 28, y + 24)
        rect(d, x + 7, y + 21, x + 12, y + 27)


# ---------------------------------------------------------------------------
# Carry Open logo mark: "[▯"  (C-bracket + rectangle)
# ---------------------------------------------------------------------------

def logo_mark(d, x, y, s=34):
    """Carry Open mark (from the reference image): a solid square with a slot
    cut out of the middle of the right edge - reads as a bold square 'C'
    opening right. Slot: ~60% of width, ~19% of height, vertically centered.
    Measured from the reference: notch x from 40.4%, h 18.9%."""
    rect(d, x, y, x + s - 1, y + s - 1)                # solid square
    nw = int(round(s * 0.60))                           # slot width
    nh = max(5, int(round(s * 0.19)))                   # slot height
    ny = y + (s - nh) // 2
    rect(d, x + s - nw, ny, x + s - 1, ny + nh - 1, fill=True, ink=255)  # punch out


# ---------------------------------------------------------------------------
# Shared chrome: battery, footer, owner banner
# ---------------------------------------------------------------------------

def battery(d, pct=60, charging=False):
    x, y, w, h = W - 30 - 12, 12, 30, 20
    rect(d, x + w, y + 6, x + w + 2, y + 13)  # terminal nub
    rect(d, x, y, x + w - 1, y + h - 1, fill=False)
    if not charging:
        segs = max(1, (pct + 24) // 25)
        for i in range(segs):
            rect(d, x + 2 + i * 6, y + 2, x + 2 + i * 6 + 4, y + h - 3)
    else:
        bx = x + w // 2 - 4
        line(d, bx + 7, y + 1, bx + 1, y + 8)
        line(d, bx + 1, y + 8, bx + 6, y + 8)
        line(d, bx + 6, y + 8, bx + 2, y + h - 2)


def footer(d, hint="UP/DOWN move   SELECT open"):
    fy = H - 26
    line(d, 0, fy, W, fy)
    draw_text(img_of(d), (14, fy + 5), hint, 12)


def owner_banner(d):
    box_h = 76
    by = H - 26 - box_h - 14
    rect(d, 10, by, W - 10, by + box_h, fill=False)
    cy = by + box_h // 2
    rect(d, 28, cy - 16, 36, cy + 16)
    rect(d, 20, cy - 6, 44, cy + 6)
    im = img_of(d)
    draw_text(im, (56, by + 10), "Property of Ken - if found, kindly", 16)
    draw_text(im, (56, by + 32), "contact 017 8088 700 - Bloodtype: B", 16)
    draw_text(im, (56, by + 54), "Emergency Contact: Sai 016 518 5081", 16)


_IMGS = {}


def img_of(d):
    return _IMGS[id(d)]


# ---------------------------------------------------------------------------
# Home screen variants
# ---------------------------------------------------------------------------

MENU = [
    ("DuitNow QR",          "Receive money",        "qr"),
    ("ID & Business Cards", "Scan to save contact", "card"),
    ("Loyalty Cards",       "Rewards & barcodes",   "gift"),
    ("Passes",              "Boarding & event passes", "ticket"),
    ("Calendar",            "Month at a glance",    "calendar"),
    ("To-Do / Checklists",  "Check things off",     "todo"),
    ("E-Books",             "Read from the card",   "book"),
    ("Settings",            "Wi-Fi, updates, info", "settings"),
]

STATUS = "Wi-Fi off    SD on    FW v0.2.22"


def render_home(variant, selected=0):
    """variant: 'A' Font24 label + Font12 caption, 'B' Font24 label only,
    'C' Font20 label + Font12 caption (final: 8 rows, rowH 72)."""
    img = Image.new("1", (W, H), 1)
    d = ImageDraw.Draw(img)
    _IMGS[id(d)] = img

    # --- Masthead: [▯ logo + PaperDeck, caption + status line, battery ---
    logo_mark(d, 16, 14)
    draw_text(img, (16 + 34 + 14, 14 + (34 - 24) // 2), "PaperDeck", 24)
    battery(d)

    draw_text(img, (16, 56), "by Carry Open", 12)
    draw_text(img, (W - 14 - text_w(STATUS, 12), 56), STATUS, 12)

    line(d, 24, 80, W - 24, 80)  # divider under masthead

    # --- Menu rows ---
    label_size = 24 if variant in ("A", "B") else 20
    row_h = 72
    y0 = 90
    for i, (label, caption, ic) in enumerate(MENU):
        y = y0 + i * row_h
        sel = i == selected
        if sel:
            rect(d, 8, y + 8, 8 + 12, y + row_h - 8)  # accent bar (kAccentW+4)
        icon(d, ic, 22, y + (row_h - 8 - 32) // 2)
        tx = 68
        ty = y + (row_h - 8 - (label_size + (14 if (caption and variant != "B") else 0))) // 2
        draw_text(img, (tx, ty), label, label_size)
        if caption and variant != "B":
            draw_text(img, (tx, ty + label_size + 4), caption, 12)
        cy = y + (row_h - 8) // 2  # chevron
        line(d, W - 34, cy - 6, W - 26, cy)
        line(d, W - 34, cy + 6, W - 26, cy)
        if i < len(MENU) - 1:
            line(d, 8, y + row_h - 4, W - 8, y + row_h - 4)

    owner_banner(d)
    footer(d)
    return img


def main():
    OUT.mkdir(exist_ok=True)
    names = {"A": "home-A-label-caption", "B": "home-B-label-only", "C": "home-C-compact"}
    imgs = {}
    for v, name in names.items():
        img = render_home(v)
        imgs[v] = img
        img.resize((W * 2, H * 2), Image.NEAREST).save(OUT / f"{name}@2x.png")
        print(f"wrote design/{name}@2x.png")

    # The chosen build (variant C, 8 rows incl. Passes) as the final preview
    imgs["C"].resize((W * 2, H * 2), Image.NEAREST).save(OUT / "home-final@2x.png")
    print("wrote design/home-final@2x.png")

    # Side-by-side sheet at 1x
    gap = 36
    sheet = Image.new("1", (3 * W + 4 * gap, H), 1)
    for k, v in enumerate("ABC"):
        sheet.paste(imgs[v], (gap + k * (W + gap), 0))
    ds = ImageDraw.Draw(sheet)
    for k, v in enumerate("ABC"):
        draw_text(sheet, (gap + k * (W + gap) + 8, 6), {"A": "A: big label + caption",
                                                        "B": "B: big label only",
                                                        "C": "C: compact (today's style)"}[v], 16)
    sheet.save(OUT / "home-variants-sheet.png")
    print("wrote design/home-variants-sheet.png")


if __name__ == "__main__":
    main()
