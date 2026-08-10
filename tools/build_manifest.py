#!/usr/bin/env python3
"""Scans an SD-card content folder and (re)generates manifest.json.

Run this after adding or removing files under cards/, tickets/, qr/, or
books/, then copy the whole folder - including the regenerated
manifest.json - onto the device's microSD card. See docs/CONTENT.md.

Item names are derived from filenames (underscores/hyphens -> spaces,
title-cased). Rename the files to get nicer on-screen names, or hand-edit
manifest.json afterward - just re-running this script will overwrite it.
"""
import argparse
import json
from pathlib import Path


def humanize(stem: str) -> str:
    return stem.replace("_", " ").replace("-", " ").strip().title()


def scan_images(folder: Path, rel_prefix: str):
    items = []
    if folder.is_dir():
        for f in sorted(folder.glob("*.bmp")):
            items.append({"name": humanize(f.stem), "image": f"{rel_prefix}/{f.name}"})
    return items


def scan_books(folder: Path, rel_prefix: str):
    items = []
    if folder.is_dir():
        for f in sorted(folder.glob("*.txt")):
            items.append({"name": humanize(f.stem), "file": f"{rel_prefix}/{f.name}"})
    return items


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("root", help="Path to the sdcard content folder (contains cards/, tickets/, qr/, books/)")
    args = parser.parse_args()
    root = Path(args.root)

    manifest = {
        "cards": scan_images(root / "cards", "cards"),
        "tickets": scan_images(root / "tickets", "tickets"),
        "books": scan_books(root / "books", "books"),
    }

    duitnow = root / "qr" / "duitnow.bmp"
    if duitnow.exists():
        manifest["duitnow_qr"] = "qr/duitnow.bmp"

    out_path = root / "manifest.json"
    out_path.write_text(json.dumps(manifest, indent=2) + "\n")

    print(f"wrote {out_path}")
    print(f"  cards:      {len(manifest['cards'])}")
    print(f"  tickets:    {len(manifest['tickets'])}")
    print(f"  books:      {len(manifest['books'])}")
    print(f"  duitnow_qr: {'yes' if 'duitnow_qr' in manifest else 'no'}")


if __name__ == "__main__":
    main()
