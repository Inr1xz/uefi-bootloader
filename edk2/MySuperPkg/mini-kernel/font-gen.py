#!/usr/bin/env python3

from string import ascii_letters, digits, punctuation
from subprocess import CalledProcessError, run

from pprint import pprint
from math import ceil


def letter_bits(letter, width, height, font):
    if letter == "\\":
        letter = "\\\\"

    cmd = rf"convert -resize {width}x{height}! -font {font} -pointsize 10 label:{letter} xbm:".split()

    try:
        result = run(cmd, capture_output=True, check=True)
    except CalledProcessError as e:
        print(e.stderr.decode())
        raise

    output = result.stdout.decode().splitlines()

    output = "".join(output[3:])

    output = [s.strip() for s in output.split(", ")][:-1]

    bits = [int(v, 16) for v in output]

    return bits


def visuzlize(bits, width, height):
    bits = "".join(f"{b:08b}"[::-1] for b in bits).replace("0", " ").replace("1", "*")
    for i in range(height):
        print(bits[i * width : (i + 1) * width])


def main():
    w, h = 16, 24

    printable = ascii_letters + digits + punctuation

    print(f"#pragma once")
    print()
    print(f"#define FONT_WIDTH  {w}")
    print(f"#define FONT_HEIGHT {h}")
    print()
    print(f"static u8 font[][{ceil(w * h / 8)}] = {{")

    for c in range(0, 128):
        if chr(c) in printable:
            bits = letter_bits(chr(c), w, h, "DejaVu-Sans-Mono")
        else:
            bits = [0] * (w * h // 8)

        b = ', '.join(f"0x{b:02x}" for b in bits)
        print(f'  {{ {b} }},')


    print("};")
    print()


if __name__ == "__main__":
    main()
