#!/usr/bin/env python3
"""Reference results for test/unit/test_lzma.cpp, as test/unit/xz.data.

Hand-run, like the other publishers here; no build step calls it. The oracle is
the host's own liblzma, which must be the release vendored in src/lzma/: the
encoder's output is liblzma's, and a different release may well make different
bytes. It is reached two ways.

Through Python's lzma module, for the encoder cases: for each, the stream the
host makes of one of the inputs below at one preset, check and format, or
through one filter chain. braam::lzma is the same C, so it must make the same
stream, and the test compares them by length and CRC-32.

Through ctypes, for the decoder: every file of xz's own test corpus
(tests/files/ in the xz distribution) but the one large TIFF, stored whole, with
what the host's lzma_auto_decoder makes of it under LZMA_CONCATENATED and
LZMA_FINISH -- the last lzma_ret, and the length and CRC-32 of the output --
and the last lzma_ret when it is fed and drained a byte at a time, LZMA_FINISH
coming with the last byte. The two differ: a .lzma of known size that also has
an end marker is refused unless LZMA_FINISH comes with the marker.

The inputs are generated, not stored: the same LCG and word list are in the
test, and INPUT_SUMS pins what they made here.

  tools/mkxzdata.py xz-5.8.4/tests/files > test/unit/xz.data
"""

import ctypes
import ctypes.util
import lzma
import os
import sys
import zlib

VERSION_WANTED = "5.8.4"

_lib = ctypes.CDLL(ctypes.util.find_library("lzma") or "liblzma.so.5")
_lib.lzma_version_string.restype = ctypes.c_char_p
VERSION = _lib.lzma_version_string().decode()
if VERSION != VERSION_WANTED:
    sys.exit(f"mkxzdata: liblzma {VERSION}, not {VERSION_WANTED}")

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
    text(2, 250000),
    noise(3, 20000),
    runs(4, 40000),
    periodic(6, 13, 60000),
    bytes(120000),
    text(5, 5000),
]

EXTREME = 0x80000000

# (name, python filters). The name is lzma_str_to_filters' spelling, which the
# test hands it; check CRC64 throughout.
CHAINS = [
    ("delta:dist=4 lzma2:preset=1",
     [{"id": lzma.FILTER_DELTA, "dist": 4}, {"id": lzma.FILTER_LZMA2, "preset": 1}]),
    ("x86 lzma2:preset=0",
     [{"id": lzma.FILTER_X86}, {"id": lzma.FILTER_LZMA2, "preset": 0}]),
    ("sparc lzma2:preset=0",
     [{"id": lzma.FILTER_SPARC}, {"id": lzma.FILTER_LZMA2, "preset": 0}]),
    ("lzma2:dict=64KiB,lc=0,lp=2,pb=2,mf=hc4,mode=fast,nice=16",
     [{"id": lzma.FILTER_LZMA2, "dict_size": 65536, "lc": 0, "lp": 2, "pb": 2,
       "mf": lzma.MF_HC4, "mode": lzma.MODE_FAST, "nice_len": 16}]),
]

FORMAT_XZ, FORMAT_LZMA = 0, 1


def encoder_cases():
    """(input, preset, check, format, chain) with chain -1 for a preset."""
    cases = []
    for i in range(len(INPUTS)):
        for p in range(7):
            cases.append((i, p, lzma.CHECK_CRC64, FORMAT_XZ, -1))
        for p in (1, 6):
            cases.append((i, p | EXTREME, lzma.CHECK_CRC64, FORMAT_XZ, -1))
        for p in (0, 1, 6):
            cases.append((i, p, lzma.CHECK_NONE, FORMAT_LZMA, -1))
    for i in (2, 3, 7):
        for check in (lzma.CHECK_NONE, lzma.CHECK_CRC32, lzma.CHECK_SHA256):
            cases.append((i, 1, check, FORMAT_XZ, -1))
    for i in (3, 5, 7):
        for c in range(len(CHAINS)):
            cases.append((i, 0, lzma.CHECK_CRC64, FORMAT_XZ, c))
    return cases


def encode(i, preset, check, fmt, chain):
    data = INPUTS[i]
    if chain >= 0:
        return lzma.compress(data, format=lzma.FORMAT_XZ, check=check,
                             filters=CHAINS[chain][1])
    if fmt == FORMAT_LZMA:
        return lzma.compress(data, format=lzma.FORMAT_ALONE, preset=preset)
    return lzma.compress(data, format=lzma.FORMAT_XZ, check=check, preset=preset)


# --------------------------------------------- the decoder, through ctypes


class LzmaStream(ctypes.Structure):
    _fields_ = [
        ("next_in", ctypes.c_void_p), ("avail_in", ctypes.c_size_t),
        ("total_in", ctypes.c_uint64),
        ("next_out", ctypes.c_void_p), ("avail_out", ctypes.c_size_t),
        ("total_out", ctypes.c_uint64),
        ("allocator", ctypes.c_void_p), ("internal", ctypes.c_void_p),
        ("reserved_ptr1", ctypes.c_void_p), ("reserved_ptr2", ctypes.c_void_p),
        ("reserved_ptr3", ctypes.c_void_p), ("reserved_ptr4", ctypes.c_void_p),
        ("seek_pos", ctypes.c_uint64), ("reserved_int2", ctypes.c_uint64),
        ("reserved_int3", ctypes.c_size_t), ("reserved_int4", ctypes.c_size_t),
        ("reserved_enum1", ctypes.c_int), ("reserved_enum2", ctypes.c_int),
    ]


LZMA_OK, LZMA_STREAM_END = 0, 1
LZMA_RUN, LZMA_FINISH = 0, 3
LZMA_BUF_ERROR = 10
LZMA_CONCATENATED = 0x08
OUT_CHUNK = 65536

_lib.lzma_auto_decoder.argtypes = [ctypes.POINTER(LzmaStream), ctypes.c_uint64,
                                   ctypes.c_uint32]
_lib.lzma_code.argtypes = [ctypes.POINTER(LzmaStream), ctypes.c_int]
_lib.lzma_end.argtypes = [ctypes.POINTER(LzmaStream)]


def decode(data):
    """(last lzma_ret, output) from lzma_auto_decoder, as the test drives it."""
    s = LzmaStream()
    ret = _lib.lzma_auto_decoder(ctypes.byref(s), (1 << 64) - 1, LZMA_CONCATENATED)
    if ret != LZMA_OK:
        sys.exit(f"mkxzdata: lzma_auto_decoder returned {ret}")
    inbuf = ctypes.create_string_buffer(data, len(data))
    outbuf = ctypes.create_string_buffer(OUT_CHUNK)
    s.next_in = ctypes.addressof(inbuf)
    s.avail_in = len(data)
    out = bytearray()
    while True:
        s.next_out = ctypes.addressof(outbuf)
        s.avail_out = OUT_CHUNK
        ret = _lib.lzma_code(ctypes.byref(s), LZMA_FINISH)
        out += outbuf.raw[:OUT_CHUNK - s.avail_out]
        if ret != LZMA_OK:
            break
    _lib.lzma_end(ctypes.byref(s))
    return ret, bytes(out)


def decode_stepped(data):
    """The last lzma_ret, a byte in and a byte out, as the test's decode_by."""
    s = LzmaStream()
    _lib.lzma_auto_decoder(ctypes.byref(s), (1 << 64) - 1, LZMA_CONCATENATED)
    inbuf = ctypes.create_string_buffer(data, len(data))
    outbuf = ctypes.create_string_buffer(1)
    at = 0
    while True:
        n = min(1, len(data) - at)
        last = at + n == len(data)
        s.next_in = ctypes.addressof(inbuf) + at
        s.avail_in = n
        s.next_out = ctypes.addressof(outbuf)
        s.avail_out = 1
        ret = _lib.lzma_code(ctypes.byref(s), LZMA_FINISH if last else LZMA_RUN)
        at += n - s.avail_in
        if ret != LZMA_OK and not (ret == LZMA_BUF_ERROR and not last):
            break
    _lib.lzma_end(ctypes.byref(s))
    return ret


def c_bytes(data, indent="    "):
    lines = []
    for i in range(0, len(data), 16):
        lines.append(indent + ", ".join(f"0x{b:02x}" for b in data[i:i + 16]) + ",")
    return "\n".join(lines)


def crc(data):
    return zlib.crc32(data) & 0xffffffff


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: mkxzdata.py xz-5.8.4/tests/files > test/unit/xz.data")
    root = sys.argv[1]
    names = sorted(n for n in os.listdir(root)
                   if n.endswith((".xz", ".lzma", ".lz")) and "tiff" not in n)

    print("// Generated by tools/mkxzdata.py. Do not edit.")
    print(f"// The host's liblzma {VERSION}: for each encoder case, the length and")
    print("// CRC-32 of the stream it made; for each file of xz's test corpus, what")
    print("// lzma_auto_decoder made of it.")
    print()
    print("static const XzInputSum INPUT_SUMS[] = {")
    for data in INPUTS:
        print(f"    {{ {len(data)}, 0x{crc(data):08x} }},")
    print("};")
    print()
    print("static const char *const XZ_CHAINS[] = {")
    for name, _ in CHAINS:
        print(f'    "{name}",')
    print("};")
    print()
    print("static const XzCase XZ_CASES[] = {")
    for i, preset, check, fmt, chain in encoder_cases():
        s = encode(i, preset, check, fmt, chain)
        print(f"    {{ {i}, 0x{preset:08x}, {check}, {fmt}, {chain}, {len(s)}, "
              f"0x{crc(s):08x} }},")
    print("};")
    print()
    for k, name in enumerate(names):
        with open(os.path.join(root, name), "rb") as f:
            data = f.read()
        print(f"static const u8 XZ_FILE_{k}[] = {{ // {name}")
        print(c_bytes(data))
        print("};")
    print()
    print("static const XzFile XZ_FILES[] = {")
    for k, name in enumerate(names):
        with open(os.path.join(root, name), "rb") as f:
            data = f.read()
        ret, out = decode(data)
        print(f'    {{ "{name}", XZ_FILE_{k}, sizeof XZ_FILE_{k}, {ret}, {len(out)}, '
              f"0x{crc(out):08x}, {decode_stepped(data)} }},")
    print("};")


if __name__ == "__main__":
    main()
