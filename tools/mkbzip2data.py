#!/usr/bin/env python3
"""Reference streams for test/unit/test_bzip2.cpp, as test/unit/bzip2.data.

Hand-run, like the other publishers here; no build step calls it. The oracle is
the host's own libbzip2 through Python's bz2 module: for each case, the stream
it makes of one of the inputs below at one block size. braam::bzip2 is a
translation of the same code, so it must make the same stream, and the test
compares them byte for byte by length and CRC -- the streams themselves would
be most of the file. The CRC is bzip2's own, so the test needs nothing else.

The inputs are generated, not stored: the same LCG and word list are in the
test, and INPUT_SUMS pins what they made here, so the two cannot drift apart
unnoticed.

Two streams are stored whole. SAMPLE3 is sample3.bz2 from the bzip2 1.0.8
distribution, written by bzip2 itself rather than by either side of this test.
RANDOMISED is a block of the kind bzip2 0.9.0 wrote and nothing since has: the
text is XORed with 0.9.0's mask and compressed, then the block's randomised bit
is set and both CRCs are made the text's. The host's libbzip2 has to give the
text back before it is written out.

  tools/mkbzip2data.py bzip2-1.0.8/sample3.bz2 > test/unit/bzip2.data
"""

import bz2
import ctypes
import sys

import _bz2

_version = ctypes.CDLL(_bz2.__file__).BZ2_bzlibVersion
_version.restype = ctypes.c_char_p
VERSION = _version().decode()
if not VERSION.startswith("1.0."):
    sys.exit(f"mkbzip2data: libbzip2 {VERSION}, not 1.0.x")

WORDS = [b"the", b"of", b"and", b"deflate", b"window", b"a", b"stream", b"to",
         b"in", b"block", b"Huffman", b"is", b"that", b"match", b"code", b"for",
         b"length", b"distance", b"with", b"as", b"literal", b"tree", b"bits",
         b"on", b"by", b"hash", b"chain", b"lazy", b"it", b"be", b"zlib", b"at"]


class Lcg:
    def __init__(self, seed):
        self.x = seed

    def next(self):
        self.x = (self.x * 1103515245 + 12345) & 0xffffffff
        return (self.x >> 16) & 0x7fff


def text(seed, n):
    r = Lcg(seed)
    out = bytearray()
    while len(out) < n:
        out += WORDS[r.next() % len(WORDS)]
        k = r.next() % 16
        out += b"\n" if k == 0 else b", " if k == 1 else b" "
    return bytes(out[:n])


def noise(seed, n):
    r = Lcg(seed)
    return bytes(r.next() & 0xff for _ in range(n))


def runs(seed, n):
    r = Lcg(seed)
    out = bytearray()
    while len(out) < n:
        out += bytes([r.next() & 0xff]) * (1 + r.next() % 300)
    return bytes(out[:n])


def periodic(seed, period, n):
    unit = noise(seed, period)
    return (unit * (n // period + 1))[:n]


# Index, as the test numbers them.
INPUTS = [
    b"",
    b"a",
    b"hello, hello, hello world\n",
    text(1, 12000),
    text(2, 250000),         # several blocks at block sizes 1 and 2
    noise(3, 20000),
    runs(4, 40000),          # every run length the first coding has
    periodic(6, 13, 60000),  # too repetitive for the main sort
    bytes(120000),           # one byte: a block of runs
    text(5, 5000),           # under 10,000: the fallback sort only
]

RANDOMISED_SEED = 7
RANDOMISED_SIZE = 3000

CRC_TABLE = []
for i in range(256):
    c = i << 24
    for _ in range(8):
        c = ((c << 1) ^ 0x04c11db7) if c & 0x80000000 else c << 1
    CRC_TABLE.append(c & 0xffffffff)


def bzcrc(data):
    c = 0xffffffff
    for b in data:
        c = ((c << 8) & 0xffffffff) ^ CRC_TABLE[(c >> 24) ^ b]
    return c ^ 0xffffffff


R_NUMS = [
    619, 720, 127, 481, 931, 816, 813, 233, 566, 247, 985, 724, 205, 454, 863, 491,
    741, 242, 949, 214, 733, 859, 335, 708, 621, 574, 73, 654, 730, 472, 419, 436,
    278, 496, 867, 210, 399, 680, 480, 51, 878, 465, 811, 169, 869, 675, 611, 697,
    867, 561, 862, 687, 507, 283, 482, 129, 807, 591, 733, 623, 150, 238, 59, 379,
    684, 877, 625, 169, 643, 105, 170, 607, 520, 932, 727, 476, 693, 425, 174, 647,
    73, 122, 335, 530, 442, 853, 695, 249, 445, 515, 909, 545, 703, 919, 874, 474,
    882, 500, 594, 612, 641, 801, 220, 162, 819, 984, 589, 513, 495, 799, 161, 604,
    958, 533, 221, 400, 386, 867, 600, 782, 382, 596, 414, 171, 516, 375, 682, 485,
]  # the first 128 of randtable.c's 512: enough for RANDOMISED_SIZE bytes


def rand_mask(n):
    """0.9.0's mask over the first n bytes of a block, as the decoder has it."""
    out = []
    to_go, pos = 0, 0
    for _ in range(n):
        if to_go == 0:
            to_go = R_NUMS[pos]
            pos += 1
        to_go -= 1
        out.append(1 if to_go == 1 else 0)
    return out


def has_run_of_four(data):
    return any(data[i] == data[i + 1] == data[i + 2] == data[i + 3]
               for i in range(len(data) - 3))


def set_bits(stream, at, width, value):
    for k in range(width):
        bit = (value >> (width - 1 - k)) & 1
        byte, shift = divmod(at + k, 8)
        if bit:
            stream[byte] |= 0x80 >> shift
        else:
            stream[byte] &= ~(0x80 >> shift) & 0xff


def randomised():
    plain = text(RANDOMISED_SEED, RANDOMISED_SIZE)
    mask = rand_mask(len(plain))
    xored = bytes(b ^ m for b, m in zip(plain, mask))
    # The first coding must leave both alone, or the block is not the text.
    assert not has_run_of_four(plain) and not has_run_of_four(xored)
    assert sum(mask) > 0

    s = bytearray(bz2.compress(xored, 1))
    crc = bzcrc(plain)
    set_bits(s, 8 * 10, 32, crc)  # the block's CRC, after "BZh1" and the magic
    set_bits(s, 8 * 14, 1, 1)     # randomised
    bits = "".join(f"{b:08b}" for b in s)
    end = bits.rfind(f"{0x177245385090:048b}")
    assert end > 0
    set_bits(s, end + 48, 32, crc)  # the stream's CRC: one block, so the same
    s = bytes(s)
    if bz2.decompress(s) != plain:
        sys.exit("mkbzip2data: the host did not read the randomised block back")
    return s


def c_bytes(data, indent="    "):
    lines = []
    for i in range(0, len(data), 16):
        lines.append(indent + ", ".join(f"0x{b:02x}" for b in data[i:i + 16]) + ",")
    return "\n".join(lines)


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: mkbzip2data.py bzip2-1.0.8/sample3.bz2 > test/unit/bzip2.data")
    with open(sys.argv[1], "rb") as f:
        s3 = f.read()
    s3_plain = bz2.decompress(s3)
    rand = randomised()

    print("// Generated by tools/mkbzip2data.py. Do not edit.")
    print(f"// The host's libbzip2 {VERSION}: for each input and block size,")
    print("// the length and bzip2 CRC of the stream it made.")
    print()
    print("static const Bzip2InputSum INPUT_SUMS[] = {")
    for data in INPUTS:
        print(f"    {{ {len(data)}, 0x{bzcrc(data):08x} }},")
    print("};")
    print()
    print("static const Bzip2Case BZIP2_CASES[] = {")
    for i, data in enumerate(INPUTS):
        for level in range(1, 10):
            s = bz2.compress(data, level)
            print(f"    {{ {i}, {level}, {len(s)}, 0x{bzcrc(s):08x} }},")
    print("};")
    print()
    print("// sample3.bz2, and what it holds.")
    print(f"static const Bzip2InputSum SAMPLE3_PLAIN = {{ {len(s3_plain)}, "
          f"0x{bzcrc(s3_plain):08x} }};")
    print("static const u8 SAMPLE3[] = {")
    print(c_bytes(s3))
    print("};")
    print()
    print(f"// A randomised block of text({RANDOMISED_SEED}, {RANDOMISED_SIZE}).")
    print(f"static const u32 RANDOMISED_SEED = {RANDOMISED_SEED};")
    print(f"static const u32 RANDOMISED_SIZE = {RANDOMISED_SIZE};")
    print("static const u8 RANDOMISED[] = {")
    print(c_bytes(rand))
    print("};")


if __name__ == "__main__":
    main()
