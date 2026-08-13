#!/usr/bin/env python3
"""Generate 1-bit BMPs for the Stack Wallet e-ink panel (480x800 portrait).

Produces BMP files in the exact format the on-device GUI_ReadBmp() loader
expects: a standard Windows BMP, 1 bit per pixel, with a 2-entry monochrome
palette. Pillow's own BMP writer for mode "1" images already matches this
format, so this script just composes artwork onto a 480x800 canvas and
dithers it down to 1-bit before saving.

The canvas is portrait (480 wide, 800 tall) to match the firmware's default
DISPLAY_ROTATE (config.h) - GUI_ReadBmp() auto-selects its rotation from a
BMP's dimensions, and 480x800 is what triggers the rotate-90 branch that
matches the rest of the UI. If you change DISPLAY_ROTATE in the firmware,
regenerate assets to match (see config.h's comment for why rotation and
BMP dimensions have to agree).

Examples:
  make_bmp.py image starbucks-logo.png cards/starbucks.bmp
  make_bmp.py barcode 012345678905 cards/starbucks.bmp --type ean13 --show-text
  make_bmp.py qr "00020101021226..." qr/duitnow.bmp --label "Scan to pay"
  make_bmp.py card card-back.png qr/card.bmp --qr-data "BEGIN:VCARD..."
"""
import argparse
import io
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

PANEL_WIDTH = 480
PANEL_HEIGHT = 800


def save_1bit_bmp(canvas: Image.Image, out_path: Path) -> None:
    if canvas.size != (PANEL_WIDTH, PANEL_HEIGHT):
        raise ValueError(f"canvas must be {PANEL_WIDTH}x{PANEL_HEIGHT}, got {canvas.size}")
    out_path.parent.mkdir(parents=True, exist_ok=True)
    mono = canvas.convert("L").convert("1")  # Floyd-Steinberg dither to 1-bit
    mono.save(out_path, format="BMP")
    print(f"wrote {out_path} ({PANEL_WIDTH}x{PANEL_HEIGHT}, 1-bit)")


def blank_canvas() -> Image.Image:
    return Image.new("L", (PANEL_WIDTH, PANEL_HEIGHT), color=255)


def fit_image(canvas: Image.Image, art: Image.Image, margin: int, top: int = 0,
              bottom: int = 0) -> None:
    """Scales `art` to fit within the margins and pastes it centered.

    `top`/`bottom` reserve extra white space at the top/bottom of the panel
    (used for the DuitNow screen, where the firmware composites a title and
    a scan hint over the reserved bands).
    """
    max_w = PANEL_WIDTH - 2 * margin
    max_h = PANEL_HEIGHT - top - bottom - 2 * margin
    art = art.convert("L")
    art.thumbnail((max_w, max_h), Image.LANCZOS)
    x = (PANEL_WIDTH - art.width) // 2
    y = top + (PANEL_HEIGHT - top - bottom - art.height) // 2
    canvas.paste(art, (x, y))


def load_font(size: int) -> ImageFont.ImageFont:
    try:
        return ImageFont.truetype("DejaVuSans-Bold.ttf", size)
    except OSError:
        return ImageFont.load_default()


def draw_centered_label(canvas: Image.Image, text: str, size: int, y: int) -> None:
    draw = ImageDraw.Draw(canvas)
    font = load_font(size)
    bbox = draw.textbbox((0, 0), text, font=font)
    w = bbox[2] - bbox[0]
    draw.text(((PANEL_WIDTH - w) / 2, y), text, fill=0, font=font)


def cmd_image(args):
    canvas = blank_canvas()
    art = Image.open(args.input)
    if args.crop:
        art = art.crop(tuple(args.crop))
    fit_image(canvas, art, margin=args.margin, top=args.top, bottom=args.bottom)
    save_1bit_bmp(canvas, Path(args.output))


def cmd_qr(args):
    import qrcode

    qr = qrcode.QRCode(border=2, error_correction=qrcode.constants.ERROR_CORRECT_M)
    qr.add_data(args.data)
    qr.make(fit=True)

    buf = io.BytesIO()
    qr.make_image(fill_color="black", back_color="white").save(buf, format="PNG")
    buf.seek(0)
    art = Image.open(buf)

    canvas = blank_canvas()
    fit_image(canvas, art, margin=args.margin)
    if args.label:
        draw_centered_label(canvas, args.label, args.label_size,
                             PANEL_HEIGHT - args.margin - args.label_size - 6)

    save_1bit_bmp(canvas, Path(args.output))


def cmd_card(args):
    import qrcode

    card = Image.open(args.input).convert("L")
    card.thumbnail((PANEL_WIDTH - 2 * args.margin, 620), Image.LANCZOS)
    card_x = (PANEL_WIDTH - card.width) // 2
    card_y = args.top

    canvas = blank_canvas()
    canvas.paste(card, (card_x, card_y))

    # Divider under the card, then the caption and the contact QR below it.
    draw = ImageDraw.Draw(canvas)
    divider_y = card_y + card.height + args.divider_gap
    draw.line([(args.margin, divider_y), (PANEL_WIDTH - args.margin, divider_y)], fill=0,
              width=2)

    label_y = divider_y + args.label_gap
    if args.label:
        draw_centered_label(canvas, args.label, args.label_size, label_y)

    qr = qrcode.QRCode(border=4, error_correction=qrcode.constants.ERROR_CORRECT_M)
    qr.add_data(args.qr_data)
    qr.make(fit=True)
    buf = io.BytesIO()
    qr.make_image(fill_color="black", back_color="white").save(buf, format="PNG")
    buf.seek(0)
    qr_img = Image.open(buf)
    qr_img = qr_img.resize((args.qr_size, args.qr_size), Image.LANCZOS)
    canvas.paste(qr_img, ((PANEL_WIDTH - args.qr_size) // 2, args.qr_y))

    save_1bit_bmp(canvas, Path(args.output))


def cmd_barcode(args):
    import barcode
    from barcode.writer import ImageWriter

    bc_class = barcode.get_barcode_class(args.type)
    bc = bc_class(args.value, writer=ImageWriter())
    buf = io.BytesIO()
    bc.write(buf, options={"write_text": args.show_text, "quiet_zone": 4})
    buf.seek(0)
    art = Image.open(buf)

    canvas = blank_canvas()
    fit_image(canvas, art, margin=args.margin)
    if args.label:
        draw_centered_label(canvas, args.label, args.label_size, args.margin)

    save_1bit_bmp(canvas, Path(args.output))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)

    p_img = sub.add_parser("image", help="Fit a photo/logo onto the panel canvas")
    p_img.add_argument("input")
    p_img.add_argument("output")
    p_img.add_argument("--margin", type=int, default=20)
    p_img.add_argument("--top", type=int, default=0,
                       help="reserve white space at the top of the panel (px)")
    p_img.add_argument("--bottom", type=int, default=0,
                       help="reserve white space at the bottom of the panel (px)")
    p_img.add_argument("--crop", type=int, nargs=4, metavar=("X0", "Y0", "X1", "Y1"),
                       help="crop the source image to this box (x0 y0 x1 y1) before fitting")
    p_img.set_defaults(func=cmd_image)

    p_qr = sub.add_parser("qr", help="Render a QR code (e.g. a DuitNow payload)")
    p_qr.add_argument("data")
    p_qr.add_argument("output")
    p_qr.add_argument("--margin", type=int, default=40)
    p_qr.add_argument("--label", default="")
    p_qr.add_argument("--label-size", type=int, default=28)
    p_qr.set_defaults(func=cmd_qr)

    p_card = sub.add_parser("card",
                            help="Composite a business-card image with a contact QR below it")
    p_card.add_argument("input", help="path to the card image (any Pillow-readable format)")
    p_card.add_argument("output")
    p_card.add_argument("--qr-data", required=True,
                        help="QR payload (e.g. a vCard/VCARD or MECARD contact string)")
    p_card.add_argument("--qr-size", type=int, default=360,
                        help="scannable QR box in px (incl. quiet zone)")
    p_card.add_argument("--qr-y", type=int, default=350, help="top edge of the QR box")
    p_card.add_argument("--margin", type=int, default=24)
    p_card.add_argument("--top", type=int, default=20)
    p_card.add_argument("--divider-gap", type=int, default=18,
                        help="gap between the card bottom and the divider")
    p_card.add_argument("--label-gap", type=int, default=12,
                        help="gap between the divider and the caption")
    p_card.add_argument("--label", default="Scan the QR to save my contact")
    p_card.add_argument("--label-size", type=int, default=26)
    p_card.set_defaults(func=cmd_card)

    p_bc = sub.add_parser("barcode", help="Render a 1D barcode (loyalty cards)")
    p_bc.add_argument("value")
    p_bc.add_argument("output")
    p_bc.add_argument("--type", default="code128",
                       help="python-barcode symbology, e.g. code128, ean13, ean8")
    p_bc.add_argument("--margin", type=int, default=40)
    p_bc.add_argument("--label", default="")
    p_bc.add_argument("--label-size", type=int, default=28)
    p_bc.add_argument("--show-text", action="store_true",
                       help="Also render the human-readable value under the bars")
    p_bc.set_defaults(func=cmd_barcode)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
