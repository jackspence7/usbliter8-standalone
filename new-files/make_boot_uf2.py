#!/usr/bin/env python3
"""
Combine the usbliter8 autoboot firmware UF2 with a tethered boot image
(surrealra1n's boot/<model>/<version>/iBSS.boot) into one UF2 to drag
onto the Pico. The image lands in flash where autoboot.c looks for it.

usage: make_boot_uf2.py firmware.uf2 iBSS.boot out.uf2
"""

import struct
import sys
import zlib

UF2_MAGIC0 = 0x0A324655
UF2_MAGIC1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30
UF2_FLAG_FAMILY = 0x2000

XIP_BASE = 0x10000000
FLASH_SIZE = 4 * 1024 * 1024  # Pico 2

# keep in sync with autoboot.c
AUTOBOOT_FLASH_OFFSET = 0x100000
AUTOBOOT_DATA_OFFSET = 0x100
AUTOBOOT_MAGIC = b"L8BOOT01"

PAYLOAD = 256


def read_blocks(path):
    data = open(path, "rb").read()
    if len(data) % 512:
        sys.exit(f"{path}: not a UF2 file")
    blocks = []
    for i in range(0, len(data), 512):
        blk = data[i:i + 512]
        m0, m1, flags, addr, size, bno, nblocks, fam = struct.unpack("<8I", blk[:32])
        if m0 != UF2_MAGIC0 or m1 != UF2_MAGIC1:
            sys.exit(f"{path}: bad UF2 block {i // 512}")
        blocks.append(dict(flags=flags, addr=addr, size=size, fam=fam, data=blk[32:32 + size]))
    return blocks


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__.strip())

    fw_path, image_path, out_path = sys.argv[1:]
    fw = read_blocks(fw_path)
    image = open(image_path, "rb").read()

    # the firmware's main family (RP2350 ARM-S), ignoring the single
    # "absolute" block picotool adds as the RP2350-E10 workaround
    families = {}
    for b in fw:
        families[b["fam"]] = families.get(b["fam"], 0) + 1
    fam = max(families, key=families.get)

    main_blocks = [b for b in fw if b["fam"] == fam]
    fw_end = max(b["addr"] + b["size"] for b in main_blocks)
    base = XIP_BASE + AUTOBOOT_FLASH_OFFSET

    if fw_end > base:
        sys.exit(f"firmware ends at 0x{fw_end:x}, overlaps the boot image at 0x{base:x}")
    if AUTOBOOT_FLASH_OFFSET + AUTOBOOT_DATA_OFFSET + len(image) > FLASH_SIZE:
        sys.exit(f"{image_path} is too big ({len(image)} bytes)")

    header = struct.pack("<8sII", AUTOBOOT_MAGIC, len(image), zlib.crc32(image) & 0xFFFFFFFF)
    blob = header.ljust(AUTOBOOT_DATA_OFFSET, b"\0") + image
    blob = blob.ljust((len(blob) + PAYLOAD - 1) // PAYLOAD * PAYLOAD, b"\xff")

    for off in range(0, len(blob), PAYLOAD):
        main_blocks.append(dict(flags=UF2_FLAG_FAMILY, addr=base + off, size=PAYLOAD,
                                fam=fam, data=blob[off:off + PAYLOAD]))

    # other families' blocks (the E10 block) go first, verbatim;
    # main-family blocks are renumbered to include the image
    out = bytearray()
    raw = open(fw_path, "rb").read()
    for i, b in enumerate(fw):
        if b["fam"] != fam:
            out += raw[i * 512:(i + 1) * 512]

    n = len(main_blocks)
    for i, b in enumerate(main_blocks):
        blk = struct.pack("<8I", UF2_MAGIC0, UF2_MAGIC1, b["flags"], b["addr"], b["size"], i, n, b["fam"])
        blk += b["data"].ljust(476, b"\0")
        blk += struct.pack("<I", UF2_MAGIC_END)
        out += blk

    open(out_path, "wb").write(out)

    print(f"firmware:   {len([b for b in fw if b['fam'] == fam])} blocks, ends at 0x{fw_end:x}")
    print(f"boot image: {len(image)} bytes, crc32 0x{zlib.crc32(image) & 0xFFFFFFFF:08x}, at 0x{base:x}")
    print(f"wrote {out_path}: {len(out) // 512} blocks")


if __name__ == "__main__":
    main()
