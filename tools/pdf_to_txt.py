#!/usr/bin/env python3
"""Extracts text from a PDF into a plain .txt file for the on-device reader.

The ESP32-S3 can't reasonably render real PDF layout on an e-ink panel, so
PDFs/e-books go through this one-time conversion to plain text; the
firmware paginates the resulting .txt itself at display time. See
docs/CONTENT.md.
"""
import argparse
from pathlib import Path

from pypdf import PdfReader


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("input", help="Path to the source .pdf")
    parser.add_argument("output", nargs="?", help="Path to the output .txt (default: same name, .txt)")
    args = parser.parse_args()

    in_path = Path(args.input)
    out_path = Path(args.output) if args.output else in_path.with_suffix(".txt")

    reader = PdfReader(str(in_path))
    chunks = []
    for page in reader.pages:
        text = (page.extract_text() or "").strip()
        if text:
            chunks.append(text)

    out_path.write_text("\n\n".join(chunks) + "\n", encoding="utf-8")
    print(f"wrote {out_path} ({len(reader.pages)} PDF pages -> {out_path.stat().st_size} bytes of text)")


if __name__ == "__main__":
    main()
