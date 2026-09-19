#!/usr/bin/env python3
"""Reference results for test/unit/test_zstd.cpp, as test/unit/zstd.data.

Hand-run, like the other publishers here; no build step calls it. The oracle is
libzstd itself, the very release vendored in src/zstd/lib/: the host's own
libzstd is whatever release its package manager has, and a different release
may well make different bytes. So this tool compiles src/zstd/lib with the
host's cc, under the defines src/zstd/CMakeLists.txt gives it, into a shared
library in a temporary directory, and drives it through ctypes.

That proves the build, not the algorithms: sys/, the defines, the 32-bit
size_t, the heap. Those are this port's own contribution. The host's library is
64-bit and braam::zstd is not, so a case may only be recorded here if zstd takes
no MEM_32bits() branch for it; every one below agreed.

For the encoder: for each case, the frame ZSTD_compress2 makes of one of the
inputs below at one level under one set of parameters, and optionally with a
dictionary; the test compares them by length and CRC-32.

For the decoder: every file of zstd's tests/golden-decompression and
tests/golden-decompression-errors but block-128k.zst, stored whole, with what
ZSTD_decompressStream makes of it -- the ZSTD_ErrorCode it ended on (0 for
none), and the length and CRC-32 of the output. And the two small inputs of
tests/golden-compression, with golden-dictionaries' one dictionary, as inputs.

The generated inputs are the same LCG and word list as the test's, and
INPUT_SUMS pins what they made here.

  tools/mkzstddata.py zstd-1.6.0 > test/unit/zstd.data
"""

import ctypes
import glob
import os
import subprocess
import sys
import tempfile
import zlib

VERSION_WANTED = 10600

HERE = os.path.dirname(os.path.abspath(__file__))
LIB = os.path.join(HERE, "..", "src", "zstd", "lib")
DEFINES = ["-DDEBUGLEVEL=0", "-DZSTD_LEGACY_SUPPORT=0", "-DZSTD_DISABLE_ASM",
           "-DZSTD_TRACE=0", "-DXXH_NO_STDLIB"]

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


# Index, as the test numbers them. 10 and 11 are golden-compression's.
GENERATED = [
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
GOLDEN_INPUTS = ["http", "huffman-compressed-larger"]
GOLDEN_DICT = "http-dict-missing-symbols"

# ZSTD_cParameter values, as zstd.h numbers them.
C_LEVEL, C_WINDOWLOG, C_STRATEGY = 100, 101, 107
C_LDM, C_CHECKSUM, C_CONTENTSIZE = 160, 201, 200
C_TARGETCBLOCKSIZE = 130

# (name, [(parameter, value)]). The level is set first, then these.
PARAMS = [
    ("defaults", []),
    ("checksum", [(C_CHECKSUM, 1)]),
    ("no content size", [(C_CONTENTSIZE, 0)]),
    ("window 2^10", [(C_WINDOWLOG, 10)]),
    ("long distance matching", [(C_LDM, 1), (C_WINDOWLOG, 20)]),
    ("btultra2 at any level", [(C_STRATEGY, 9)]),
    ("target block size 1340", [(C_TARGETCBLOCKSIZE, 1340)]),
]

# One per strategy and then some: fast, dfast, greedy, lazy, lazy2, btlazy2,
# btopt, btultra, btultra2.
LEVELS = [-5, -1, 1, 2, 3, 4, 5, 6, 7, 9, 12, 13, 16, 17, 18, 19]

# Dictionaries: none, input 9's first 2 KiB as raw content, golden's.
DICT_NONE, DICT_RAW, DICT_GOLDEN = 0, 1, 2


def build(tmp):
    out = os.path.join(tmp, "libzstd.so")
    sources = sorted(glob.glob(os.path.join(LIB, "*", "*.c")))
    cc = os.environ.get("CC", "cc")
    subprocess.run([cc, "-O2", "-shared", "-fPIC", "-o", out, *DEFINES, *sources],
                   check=True)
    return ctypes.CDLL(out)


def load(z):
    vp, sz = ctypes.c_void_p, ctypes.c_size_t
    z.ZSTD_versionNumber.restype = ctypes.c_uint
    z.ZSTD_createCCtx.restype = vp
    z.ZSTD_createDCtx.restype = vp
    z.ZSTD_freeCCtx.argtypes = [vp]
    z.ZSTD_freeDCtx.argtypes = [vp]
    z.ZSTD_CCtx_setParameter.argtypes = [vp, ctypes.c_int, ctypes.c_int]
    z.ZSTD_CCtx_setParameter.restype = sz
    z.ZSTD_CCtx_loadDictionary.argtypes = [vp, ctypes.c_char_p, sz]
    z.ZSTD_CCtx_loadDictionary.restype = sz
    z.ZSTD_DCtx_loadDictionary.argtypes = [vp, ctypes.c_char_p, sz]
    z.ZSTD_DCtx_loadDictionary.restype = sz
    z.ZSTD_compressBound.argtypes = [sz]
    z.ZSTD_compressBound.restype = sz
    z.ZSTD_compress2.argtypes = [vp, vp, sz, ctypes.c_char_p, sz]
    z.ZSTD_compress2.restype = sz
    z.ZSTD_isError.argtypes = [sz]
    z.ZSTD_isError.restype = ctypes.c_uint
    z.ZSTD_getErrorCode.argtypes = [sz]
    z.ZSTD_getErrorCode.restype = ctypes.c_int
    z.ZSTD_decompressStream.argtypes = [vp, vp, vp]
    z.ZSTD_decompressStream.restype = sz
    if z.ZSTD_versionNumber() != VERSION_WANTED:
        sys.exit(f"mkzstddata: src/zstd/lib is {z.ZSTD_versionNumber()}, "
                 f"not {VERSION_WANTED}")


def check(z, r, what):
    if z.ZSTD_isError(r):
        sys.exit(f"mkzstddata: {what}: error {z.ZSTD_getErrorCode(r)}")
    return r


def compress(z, data, level, params, dictionary):
    c = z.ZSTD_createCCtx()
    check(z, z.ZSTD_CCtx_setParameter(c, C_LEVEL, level), "level")
    for p, v in params:
        check(z, z.ZSTD_CCtx_setParameter(c, p, v), f"parameter {p}")
    if dictionary:
        check(z, z.ZSTD_CCtx_loadDictionary(c, dictionary, len(dictionary)), "dictionary")
    cap = z.ZSTD_compressBound(len(data))
    buf = ctypes.create_string_buffer(cap)
    n = check(z, z.ZSTD_compress2(c, buf, cap, data, len(data)), "compress2")
    z.ZSTD_freeCCtx(c)
    return buf.raw[:n]


class Buffer(ctypes.Structure):
    _fields_ = [("ptr", ctypes.c_void_p), ("size", ctypes.c_size_t),
                ("pos", ctypes.c_size_t)]


def decompress(z, data):
    """(ZSTD_ErrorCode, output) from ZSTD_decompressStream, as the test drives
    it: all of the input offered, 64 KiB of room at a time, until a call ends a
    frame with the input consumed, or consumes it without filling the room.
    That last is a frame left unfinished, srcSize_wrong, 72: called again, a
    finished frame's hint would be the next one's header."""
    d = z.ZSTD_createDCtx()
    inbuf = ctypes.create_string_buffer(data, len(data))
    room = 65536
    outbuf = ctypes.create_string_buffer(room)
    src = Buffer(ctypes.addressof(inbuf), len(data), 0)
    out = bytearray()
    code, hint = 0, 0
    while True:
        dst = Buffer(ctypes.addressof(outbuf), room, 0)
        hint = z.ZSTD_decompressStream(d, ctypes.byref(dst), ctypes.byref(src))
        if z.ZSTD_isError(hint):
            code = z.ZSTD_getErrorCode(hint)
            break
        out += outbuf.raw[:dst.pos]
        if src.pos == src.size and (hint == 0 or dst.pos < dst.size):
            break
    z.ZSTD_freeDCtx(d)
    if code == 0 and hint != 0:
        code = 72
    return code, bytes(out)


def encoder_cases():
    """(input, level, params, dictionary)."""
    cases = []
    for i in range(len(GENERATED)):
        for level in LEVELS:
            cases.append((i, level, 0, DICT_NONE))
    for i in (2, 3, 6, 9):
        for p in range(1, len(PARAMS)):
            for level in (1, 3, 7, 19):
                cases.append((i, level, p, DICT_NONE))
    for i in (2, 3):
        for level in (1, 3, 19):
            cases.append((i, level, 0, DICT_RAW))
            cases.append((i, level, 1, DICT_RAW))
    for i in (10, 11):
        for level in (1, 3, 19):
            cases.append((i, level, 0, DICT_NONE))
            cases.append((i, level, 0, DICT_GOLDEN))
    return cases


def c_bytes(data, indent="    "):
    lines = []
    for i in range(0, len(data), 16):
        lines.append(indent + ", ".join(f"0x{b:02x}" for b in data[i:i + 16]) + ",")
    return "\n".join(lines)


def crc(data):
    return zlib.crc32(data) & 0xffffffff


def read(path):
    with open(path, "rb") as f:
        return f.read()


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: mkzstddata.py zstd-1.6.0 > test/unit/zstd.data")
    tests = os.path.join(sys.argv[1], "tests")
    inputs = GENERATED + [read(os.path.join(tests, "golden-compression", n))
                          for n in GOLDEN_INPUTS]
    dicts = [None, GENERATED[9][:2048],
             read(os.path.join(tests, "golden-dictionaries", GOLDEN_DICT))]
    files = []
    for d in ("golden-decompression", "golden-decompression-errors"):
        for n in sorted(os.listdir(os.path.join(tests, d))):
            if n.endswith(".zst") and n != "block-128k.zst":
                files.append((f"{d}/{n}", read(os.path.join(tests, d, n))))

    with tempfile.TemporaryDirectory() as tmp:
        z = build(tmp)
        load(z)

        print("// Generated by tools/mkzstddata.py. Do not edit.")
        print("// src/zstd/lib built for the host: for each encoder case, the length")
        print("// and CRC-32 of the frame it made; for each golden file of zstd's own")
        print("// tests, what ZSTD_decompressStream made of it.")
        print()
        print("static const ZstdInputSum INPUT_SUMS[] = {")
        for data in GENERATED:
            print(f"    {{ {len(data)}, 0x{crc(data):08x} }},")
        print("};")
        print()
        for k, n in enumerate(GOLDEN_INPUTS):
            print(f"static const u8 ZSTD_GOLDEN_INPUT_{k}[] = {{ // golden-compression/{n}")
            print(c_bytes(inputs[len(GENERATED) + k]))
            print("};")
        print(f"static const u8 ZSTD_GOLDEN_DICT[] = {{ // golden-dictionaries/{GOLDEN_DICT}")
        print(c_bytes(dicts[DICT_GOLDEN]))
        print("};")
        print()
        print("static const ZstdParams ZSTD_PARAMS[] = {")
        for name, ps in PARAMS:
            pairs = ", ".join(f"{{ {p}, {v} }}" for p, v in ps)
            print(f'    {{ "{name}", {len(ps)}, {{ {pairs} }} }},')
        print("};")
        print()
        print("static const ZstdCase ZSTD_CASES[] = {")
        for i, level, p, d in encoder_cases():
            s = compress(z, inputs[i], level, PARAMS[p][1], dicts[d])
            print(f"    {{ {i}, {level}, {p}, {d}, {len(s)}, 0x{crc(s):08x} }},")
        print("};")
        print()
        for k, (name, data) in enumerate(files):
            print(f"static const u8 ZSTD_FILE_{k}[] = {{ // {name}")
            print(c_bytes(data))
            print("};")
        print()
        print("static const ZstdFile ZSTD_FILES[] = {")
        for k, (name, data) in enumerate(files):
            code, out = decompress(z, data)
            print(f'    {{ "{name}", ZSTD_FILE_{k}, sizeof ZSTD_FILE_{k}, {code}, '
                  f"{len(out)}, 0x{crc(out):08x} }},")
        print("};")


if __name__ == "__main__":
    main()
