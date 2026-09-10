#!/usr/bin/env python3
"""Extract Device Tree Blob(s) from Surface Duo 2 partition dumps.

Scans raw images for the Flattened Device Tree magic (0xd00dfeed, big-endian,
on-disk bytes d0 0d fe ed), then carves each blob out using the totalsize field
at offset 4. Emits each as a separate .dtb with a JSON index.
"""
from __future__ import annotations
import struct, sys, os, json, glob

FDT_MAGIC = 0xd00dfeed
OUT = "/home/emile/workspace/duo2-port/artifacts/reference"

def carve(path: str, outdir: str) -> list[dict]:
    with open(path, "rb") as f:
        data = f.read()
    hits: list[dict] = []
    i = 0
    n = len(data)
    # scan every byte (blobs are 8-byte aligned but cheap to scan) — fast enough for 24 MB
    while i <= n - 8:
        if data[i:i+4] == b"\xd0\x0d\xfe\xed":
            if i + 4 + 4 <= n:
                (totalsize,) = struct.unpack(">I", data[i+4:i+8])
                if 0x40 <= totalsize <= 0x400000 and i + totalsize <= n:
                    blob = data[i:i+totalsize]
                    # sanity: check version field makes sense (lastcompat/version offsets 32/36 region)
                    hits.append({"offset": i, "totalsize": totalsize, "blob": blob})
                    i += totalsize
                    continue
        i += 4
    # de-duplicate identical adjacent (in case of overlap)
    out = []
    for h in hits:
        if out and out[-1]["offset"] + out[-1]["totalsize"] > h["offset"]:
            # overlapping duplicate; keep first
            continue
        out.append(h)
    base = os.path.basename(path)
    idx = []
    for k, h in enumerate(out):
        fname = f"{base}.dtb{k}.dtb"
        with open(os.path.join(outdir, fname), "wb") as f:
            f.write(h["blob"])
        idx.append({"file": fname, "offset": h["offset"], "totalsize": h["totalsize"]})
    return idx


def main():
    os.makedirs(OUT, exist_ok=True)
    priv = "/home/emile/workspace/duo2-port/artifacts/private/0F00Q6U213800A"
    targets = ["dtbo_b.img", "dtbo_a.img", "boot_b.img", "boot_a.img",
               "vendor_boot_b.img", "vendor_boot_a.img"]
    summary = {}
    for t in targets:
        p = os.path.join(priv, t)
        if not os.path.exists(p):
            continue
        idx = carve(p, OUT)
        summary[t] = idx
        print(f"{t}: {len(idx)} blob(s)")
        for e in idx:
            print(f"    {e['file']}  off=0x{e['offset']:x}  size={e['totalsize']}")
    with open(os.path.join(OUT, "dtb-index.json"), "w") as f:
        json.dump(summary, f, indent=2)
    print("\nindex ->", os.path.join(OUT, "dtb-index.json"))

if __name__ == "__main__":
    main()