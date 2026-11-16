"""Checks an address against gta_sa.exe 1.0 US itself: disassembly, bytes, references. Needs no symbol file.

    set GTA_SA_EXE=<path to gta_sa.exe>            (or --exe <path> as the first arguments)
    python tools/re/exe.py 0x5E8030 [count]        disassemble count instructions (default 40)
    python tools/re/exe.py -b 0x8D2530 [n]         dump n bytes (default 64) with their u32 and float readings
    python tools/re/exe.py -x 0xB7CD98             code that refers to an address (as a 32-bit immediate)
    python tools/re/exe.py -c 0x608E20             call and jmp rel32 instructions that go to an address
    python tools/re/exe.py -s "text"               where an ASCII string is

docs/FORK.md lists the addresses the fork added and how each was checked with this.
"""
import os
import struct
import sys

from capstone import CS_ARCH_X86, CS_MODE_32, Cs

if len(sys.argv) > 2 and sys.argv[1] == "--exe":
    EXE = sys.argv[2]
    del sys.argv[1:3]
else:
    EXE = os.environ.get("GTA_SA_EXE", "")
if not EXE or not os.path.exists(EXE):
    sys.exit("error: set GTA_SA_EXE to the path of gta_sa.exe 1.0 US (14 383 616 bytes), or pass --exe <path>")
data = open(EXE, "rb").read()
if len(data) != 14383616:
    sys.exit(f"error: {EXE} is {len(data)} bytes, not the 1.0 US executable (14 383 616)")
pe = struct.unpack_from("<I", data, 0x3C)[0]
nsec = struct.unpack_from("<H", data, pe + 6)[0]
optsz = struct.unpack_from("<H", data, pe + 20)[0]
base = struct.unpack_from("<I", data, pe + 24 + 28)[0]
secs = []
for i in range(nsec):
    o = pe + 24 + optsz + i * 40
    name = data[o:o + 8].rstrip(b"\0").decode()
    vsize, va, rsize, raw = struct.unpack_from("<IIII", data, o + 8)
    secs.append((name, base + va, vsize, raw, rsize))


def off(va):
    for name, sva, vsize, raw, rsize in secs:
        if sva <= va < sva + max(vsize, rsize):
            if va - sva >= rsize:
                return None  # uninitialised (.bss part)
            return raw + (va - sva)
    return None


def va_of(o):
    for name, sva, vsize, raw, rsize in secs:
        if raw <= o < raw + rsize:
            return sva + (o - raw)
    return None


def calls_to(target):
    """Addresses of the call/jmp rel32 instructions in .text that go to target."""
    name, sva, vsize, raw, rsize = secs[0]
    out = []
    for i in range(raw, raw + rsize - 5):
        if data[i] in (0xE8, 0xE9):
            rel = struct.unpack_from("<i", data, i + 1)[0]
            if (sva + (i - raw) + 5 + rel) & 0xFFFFFFFF == target:
                out.append(sva + (i - raw))
    return out


def main():
    a = sys.argv[1:]
    if a[0] == "-b":
        va = int(a[1], 16)
        n = int(a[2]) if len(a) > 2 else 64
        o = off(va)
        if o is None:
            print("not in file (bss)")
            return
        for i in range(0, n, 4):
            b = data[o + i:o + i + 4]
            u = struct.unpack("<I", b)[0]
            f = struct.unpack("<f", b)[0]
            print(f"{va + i:08X}: {b.hex(' ')}  u32={u:<12} f={f:.6g}")
    elif a[0] == "-x":
        va = int(a[1], 16)
        pat = struct.pack("<I", va)
        name, sva, vsize, raw, rsize = secs[0]
        i = raw
        hits = []
        while True:
            i = data.find(pat, i, raw + rsize)
            if i < 0:
                break
            hits.append(va_of(i))
            i += 1
        print(f"{len(hits)} refs in {name}:", " ".join(f"{h:08X}" for h in hits[:200]))
    elif a[0] == "-c":
        hits = calls_to(int(a[1], 16))
        print(f"{len(hits)} calls or jumps:", " ".join(f"{h:08X}" for h in hits))
    elif a[0] == "-s":
        pat = a[1].encode()
        i = 0
        while True:
            i = data.find(pat, i)
            if i < 0:
                break
            v = va_of(i)
            print(f"{v:08X}" if v else f"file+{i:X}", data[i:i + 48].split(b"\0")[0])
            i += 1
    else:
        va = int(a[0], 16)
        n = int(a[1]) if len(a) > 1 else 40
        o = off(va)
        md = Cs(CS_ARCH_X86, CS_MODE_32)
        c = 0
        for ins in md.disasm(data[o:o + n * 12], va):
            print(f"{ins.address:08X}: {ins.bytes.hex():<24} {ins.mnemonic} {ins.op_str}")
            c += 1
            if c >= n:
                break


if __name__ == "__main__":
    main()
