#!/usr/bin/env python3
"""Pure-Python Flattened Device Tree (FDT) -> readable .dts dumper.

Self-contained (no dtc/flex/bison needed). Handles the standard big-endian
FDT v17 layout. Emits valid-ish DTS with values as strings where printable,
byte arrays as <hex ...> blocks otherwise.
"""
from __future__ import annotations
import struct, sys, os

FDT_BEGIN_NODE = 0x1
FDT_END_NODE = 0x2
FDT_PROP = 0x3
FDT_NOP = 0x4
FDT_END = 0x9


def _pad4(n: int) -> int:
    return (n + 3) & ~3


def parse(path: bytes | str) -> str:
    with open(path, "rb") as f:
        data = f.read()

    magic, totalsize, off_struct, off_strings, off_rsvmap, version, \
        last_comp, boot_cpu, size_strings, size_struct = struct.unpack(
            ">10I", data[:40])
    if magic != 0xd00dfeed:
        raise ValueError(f"bad FDT magic 0x{magic:08x}")

    # reservation map
    rsv = []
    if off_rsvmap:
        i = off_rsvmap
        while True:
            addr, sz = struct.unpack(">QQ", data[i:i+16])
            if addr == 0 and sz == 0:
                break
            rsv.append((addr, sz))
            i += 16

    strings = data[off_strings:off_strings + size_strings]

    def getstr(off: int) -> str:
        end = strings.find(b"\x00", off)
        return strings[off:end].decode("utf-8", "replace")

    out: list[str] = []
    indent = 0
    i = off_struct
    end = off_struct + size_struct
    pads = " " * 2

    while i < end:
        tok = struct.unpack_from(">I", data, i)[0]
        i += 4
        if tok == FDT_BEGIN_NODE:
            name = data[i:data.find(b"\x00", i)].decode("utf-8", "replace")
            i = _pad4(i + len(name.encode()) + 1)
            out.append(f"{pads*indent}{name} {{")
            indent += 1
        elif tok == FDT_END_NODE:
            indent -= 1
            out.append(f"{pads*indent}}};")
        elif tok == FDT_PROP:
            plen, nameoff = struct.unpack_from(">II", data, i)
            i += 8
            pname = getstr(nameoff)
            raw = data[i:i+plen]
            i = _pad4(i + plen)
            val = render_prop(raw)
            out.append(f"{pads*indent}{pname} = {val};")
        elif tok == FDT_NOP:
            continue
        elif tok == FDT_END:
            break
        else:
            # unknown token; stop
            break

    head = [
        "// Auto-extracted FDT -> DTS (pure-python dumper)",
        f"// totalsize={totalsize} version={version} last_comp={last_comp}",
        "/dts-v1/;",
        "",
    ]
    return "\n".join(head + out)


def render_prop(raw: bytes) -> str:
    if raw == b"":
        return "\"\""
    # try simple NUL-terminated or single printable string
    if b"\x00" in raw:
        s = raw.split(b"\x00", 1)[0]
        if all(32 <= c < 127 for c in s) and s:
            return f'"{s.decode()}"'
    if all(32 <= c < 127 for c in raw):
        return f'"{raw.decode()}"'
    # emit as hex cells
    cells = []
    for j in range(0, len(raw) - 3, 4):
        cells.append("0x%08x" % struct.unpack_from(">I", raw, j)[0])
    rem = len(raw) % 4
    if rem:
        tail = raw[len(raw)-rem:]
        b = "".join("%02x" % x for x in tail)
        cells.append("0x" + b)
    return "<" + " ".join(cells) + ">"


def main():
    ref = "/home/emile/workspace/duo2-port/artifacts/reference"
    targets = sys.argv[1:] or sorted(
        os.path.join(ref, f) for f in os.listdir(ref)
        if f.endswith(".dtb") and "vendor_boot" in f
    )
    for t in targets:
        t = os.path.abspath(t)
        out = t[:-4] + ".dts"
        dts = parse(t)
        with open(out, "w") as f:
            f.write(dts)
        print(f"{os.path.basename(t)} -> {os.path.basename(out)} ({len(dts)} bytes)")


if __name__ == "__main__":
    main()