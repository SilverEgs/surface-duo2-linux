#!/usr/bin/env python3
"""Patch dr_mode="otg" -> "peripheral" in a .dtb binary (FDT), no dtc needed.
The value is inline in the structure block; we re-serialize the structure block
with the longer value appended and fix up the header offsets/sizes."""
import struct, sys

def patch(in_path, out_path):
    data = open(in_path, 'rb').read()
    (magic, totalsize, off_struct, off_strings, off_rsv, version,
     last_comp, boot_cpu, size_strings, size_struct) = struct.unpack('>10I', data[:40])
    assert magic == 0xd00dfeed, f"bad magic {hex(magic)}"
    struct_blk = data[off_struct:off_struct + size_struct]
    strings_blk = data[off_strings:off_strings + size_strings]

    out = bytearray()
    i = 0
    npatched = 0
    while i < len(struct_blk):
        token = struct.unpack('>I', struct_blk[i:i+4])[0]
        if token == 0x1:  # BEGIN_NODE
            out += struct_blk[i:i+4]; i += 4
            j = struct_blk.index(b'\0', i)
            nm_end = j + 1
            pad = (4 - ((nm_end - i) % 4)) % 4
            out += struct_blk[i:nm_end + pad]
            i = nm_end + pad
        elif token == 0x2:  # END_NODE
            out += struct_blk[i:i+4]; i += 4
        elif token == 0x3:  # PROP
            length = struct.unpack('>I', struct_blk[i+4:i+8])[0]
            nameoff = struct.unpack('>I', struct_blk[i+8:i+12])[0]
            name_end = strings_blk.index(b'\0', nameoff)
            name = strings_blk[nameoff:name_end].decode()
            val = struct_blk[i+12:i+12+length]
            pad = (4 - (length % 4)) % 4
            i_next = i + 12 + length + pad
            if name == 'dr_mode' and val == b'otg\0':
                newval = b'peripheral\0'
                newpad = (4 - (len(newval) % 4)) % 4
                out += struct.pack('>III', 0x3, len(newval), nameoff)
                out += newval + (b'\0' * newpad)
                npatched += 1
            else:
                out += struct_blk[i:i_next]
            i = i_next
        elif token == 0x4:  # NOP
            out += struct_blk[i:i+4]; i += 4
        elif token == 0x9:  # END
            out += struct_blk[i:i+4]; i += 4
            break
        else:
            raise ValueError(f"unknown token {token} at offset {i}")

    new_size_struct = len(out)
    new_off_strings = off_struct + new_size_struct
    new_data = bytearray(data[:off_struct])      # header + rsvmap
    new_data += out                              # new struct block
    new_data += data[off_strings:]               # strings block (unchanged)
    new_totalsize = len(new_data)

    # patch header: totalsize@4, off_dt_strings@12, size_dt_struct@36
    new_data[4:8]   = struct.pack('>I', new_totalsize)
    new_data[12:16] = struct.pack('>I', new_off_strings)
    new_data[36:40] = struct.pack('>I', new_size_struct)

    open(out_path, 'wb').write(bytes(new_data))
    return npatched, new_totalsize

if __name__ == '__main__':
    for p in sys.argv[1:]:
        op = p.replace('.dtb', '.periph.dtb')
        n, t = patch(p, op)
        print(f"{p}: patched {n} dr_mode, new size {t} -> {op}")