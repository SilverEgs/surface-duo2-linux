#!/usr/bin/env python3
"""NOP-out a named property (token FDT_PROP -> FDT_NOP) in a .dtb, in place.
Simple token rewrite, no re-serialization, so the dtb size is unchanged."""
import struct, sys

def nop_property(in_path, out_path, prop_name):
    data = open(in_path, 'rb').read()
    (magic, totalsize, off_struct, off_strings, off_rsv, version,
     last_comp, boot_cpu, size_strings, size_struct) = struct.unpack('>10I', data[:40])
    assert magic == 0xd00dfeed, f"bad magic {hex(magic)}"
    struct_blk = bytearray(data[off_struct:off_struct + size_struct])
    strings_blk = data[off_strings:off_strings + size_strings]

    n = 0
    i = 0
    while i < len(struct_blk):
        token = struct.unpack('>I', struct_blk[i:i+4])[0]
        if token == 0x1:  # BEGIN_NODE
            i += 4
            j = struct_blk.index(0, i)
            nm_end = j + 1
            pad = (4 - ((nm_end - i) % 4)) % 4
            i = nm_end + pad
        elif token == 0x2:  # END_NODE
            i += 4
        elif token == 0x3:  # PROP
            length = struct.unpack('>I', struct_blk[i+4:i+8])[0]
            nameoff = struct.unpack('>I', struct_blk[i+8:i+12])[0]
            name_end = strings_blk.index(0, nameoff)
            name = strings_blk[nameoff:name_end].decode()
            pad = (4 - (length % 4)) % 4
            i_next = i + 12 + length + pad
            if name == prop_name:
                # NOP every 4-byte word in the property span (token..value+pad)
                for w in range(i, i_next, 4):
                    struct.pack_into('>I', struct_blk, w, 0x4)  # FDT_NOP
                n += 1
            i = i_next
        elif token == 0x4:  # NOP
            i += 4
        elif token == 0x9:  # END
            i += 4
            break
        else:
            raise ValueError(f"unknown token {token} at {i}")

    new_data = bytearray(data)
    new_data[off_struct:off_struct + size_struct] = bytes(struct_blk)
    open(out_path, 'wb').write(bytes(new_data))
    return n

if __name__ == '__main__':
    prop = sys.argv[1]
    for p in sys.argv[2:]:
        op = p.rsplit('.', 1)[0] + '.noextcon.dtb'
        n = nop_property(p, op, prop)
        print(f"{p}: NOP'd {n} x '{prop}' -> {op}")