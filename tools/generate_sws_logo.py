#!/usr/bin/env python3
"""Render the README logo (requires Pillow), preserving the custom launcher icon."""

from pathlib import Path

from PIL import Image, ImageDraw


APP = Path(__file__).resolve().parents[1] / "telink_sws"
ARTWORK = APP / "artwork"


def pixels(image, rows, x, y, scale=1, color=0):
    draw = ImageDraw.Draw(image)
    for row, bits in enumerate(rows):
        for col, bit in enumerate(bits):
            if bit == "#":
                left, top = x + col * scale, y + row * scale
                draw.rectangle((left, top, left + scale - 1, top + scale - 1), fill=color)


def main():
    ARTWORK.mkdir(parents=True, exist_ok=True)

    logo = Image.new("1", (64, 40), 1)
    draw = ImageDraw.Draw(logo)
    for x in (14, 22, 30, 38, 46):
        draw.rectangle((x, 0, x + 3, 39), fill=0)
    for y in (10, 18, 26):
        draw.rectangle((0, y, 63, y + 3), fill=0)
    draw.polygon([(10, 6), (53, 6), (55, 8), (55, 31),
                  (53, 33), (10, 33), (8, 31), (8, 8)], fill=0)
    s = [".####", "##...", "##...", ".###.", "...##", "...##", "####."]
    w = ["##...##", "##...##", "##...##", "##.#.##", "##.#.##", "#######", ".##.##."]
    pixels(logo, s, 13, 13, scale=2, color=1)
    pixels(logo, w, 25, 13, scale=2, color=1)
    pixels(logo, s, 41, 13, scale=2, color=1)
    logo.save(ARTWORK / "sws-logo.png")
    logo.resize((640, 400), Image.Resampling.NEAREST).save(ARTWORK / "sws-logo-preview.png")


if __name__ == "__main__":
    main()
