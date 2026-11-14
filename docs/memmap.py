"""
Generate a memory map from region names + cell counts.

Address 0x00000 is reserved for the memory-mapped Stack Pointer.
Regions are packed sequentially from 0x00001 onward.
"""

# ---- EDIT THIS: (name, number_of_cells) ----
REGIONS = [
    ("System ROM",         0x1000),    # 4096
    ("Stack  RAM",         0x7FF),     # 2047
    ("Stack Pointer",      0x1),       # 1
    ("Random Number",      0x4),       # 4
    ("Keymap",             0x10),      # 16
    ("Second",             0x4),       # 4
    ("System Control",     0x2),       # 2 bytes; RESET and POWEROFF       
    ("System Audio",       0x6),       # 7 bytes; AUDIO buffer, essentially
    ("8-bit RAM",          0x4000),    # 16384 cells ; 16k of 8-bit RAM
    ("16-bit RAM",         0x100),     # 1048  cells ; 6k of 16-bit RAM
    ("Character Buffer",   0x960),     # 2400 = 80x30
    ("Palette Buffer",     0x40),      # 64 = 4 palettes x 16 colors
    ("Font Buffer",        0x1000),    # 4096 = 128 glyphs x 32 bytes
    ("PMEM 1",             0x8000),    # 32768
    ("PMEM 2",             0x8000),    # 32768
    ("EXTPMEM",            0x8000),    # 32768
]
# ---------------------------------------------

FIRST_ADDR = 0x00000


def to_binary(n, width):
    """Format n as space-separated 4-bit groups."""
    s = f"{n:0{width}b}"
    # pad to a multiple of 4
    while len(s) % 4:
        s = "0" + s
    return " ".join(s[i:i+4] for i in range(0, len(s), 4))


def main():
    # Determine the binary width needed for the highest address.
    total_cells = sum(count for _, count in REGIONS)
    last_addr = FIRST_ADDR + total_cells - 1
    width = max(20, len(f"{last_addr:b}"))
    while width % 4:
        width += 1

    print("-" * 56)
    print("           Memory Map")
    print("-" * 56)
    print()

    # Region start lines
    addr = FIRST_ADDR
    starts = []
    for name, count in REGIONS:
        starts.append((name, addr, count))
        print(f"{to_binary(addr, width)} - Start of {name}")
        addr += count

    # Last address of final region
    last = addr - 1
    print(f"{to_binary(last, width)} - Last address of {REGIONS[-1][0]}")
    print()

    # Hex range table
    addr = FIRST_ADDR
    for name, count in REGIONS:
        end = addr + count - 1
        print(f"0x{addr:05X} -> 0x{end:05X} : {name}")
        addr += count

    print()
    # Sanity checks
    print("--- Sanity checks ---")
    print(f"Total cells (excl. SP): {total_cells}")
    print(f"Address range: 0x{FIRST_ADDR:05X} .. 0x{last:05X}")
    assert last < (1 << 20), "Memory map exceeds 20-bit address space!"
    # check no overlap (trivially true by construction, but be explicit)
    prev_end = None
    for name, start, count in starts:
        if prev_end is not None:
            assert start == prev_end + 1, f"Gap/overlap before {name}"
        prev_end = start + count - 1
    print("No overlaps, no gaps. OK.")


if __name__ == "__main__":
    main()
