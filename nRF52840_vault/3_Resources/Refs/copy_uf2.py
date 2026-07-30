#!/usr/bin/env python3
"""PlatformIO post-build conversion from Intel HEX to real nRF52840 UF2."""

Import("env")

import os
import shutil
import struct

UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30
UF2_FLAG_FAMILY_ID = 0x00002000
NRF52840_FAMILY_ID = 0xADA52840
UF2_PAYLOAD_SIZE = 256


def hex_to_blocks(hex_path):
    """Read Intel HEX into ordered, 256-byte flash blocks."""
    upper_address = 0
    blocks = {}

    with open(hex_path, "r", encoding="ascii") as hex_file:
        for raw_line in hex_file:
            line = raw_line.strip()
            if not line or not line.startswith(":"):
                continue

            record = bytes.fromhex(line[1:])
            byte_count = record[0]
            address = (record[1] << 8) | record[2]
            record_type = record[3]
            data = record[4:4 + byte_count]

            if record_type == 0x00:
                absolute_address = upper_address + address
                for value in data:
                    block_address = absolute_address & ~0xFF
                    if block_address not in blocks:
                        blocks[block_address] = bytearray(UF2_PAYLOAD_SIZE)
                    blocks[block_address][absolute_address & 0xFF] = value
                    absolute_address += 1
            elif record_type == 0x02:
                upper_address = int.from_bytes(data, "big") << 4
            elif record_type == 0x04:
                upper_address = int.from_bytes(data, "big") << 16
            elif record_type == 0x01:
                break

    return sorted(blocks.items())


def write_uf2(hex_path, uf2_path):
    blocks = hex_to_blocks(hex_path)
    block_count = len(blocks)

    with open(uf2_path, "wb") as uf2_file:
        for block_number, (address, payload) in enumerate(blocks):
            header = struct.pack(
                "<IIIIIIII",
                UF2_MAGIC_START0,
                UF2_MAGIC_START1,
                UF2_FLAG_FAMILY_ID,
                address,
                UF2_PAYLOAD_SIZE,
                block_number,
                block_count,
                NRF52840_FAMILY_ID,
            )
            padding = bytes(512 - len(header) - len(payload) - 4)
            uf2_file.write(header)
            uf2_file.write(payload)
            uf2_file.write(padding)
            uf2_file.write(struct.pack("<I", UF2_MAGIC_END))


def create_uf2(source, target, env):
    hex_path = env.subst("$BUILD_DIR/${PROGNAME}.hex")
    build_uf2 = env.subst("$BUILD_DIR/${PROGNAME}.uf2")
    project_dir = env.subst("$PROJECT_DIR")
    node_id = env.GetProjectOption("custom_node_id", "unknown")
    radio_macro = env.GetProjectOption("custom_radio", "unknown")
    radio_names = {
        "USE_SX1262": "sx1262_ra01sh",
        "USE_RFM95": "rfm95",
    }
    radio_name = radio_names.get(radio_macro, radio_macro.lower())
    root_uf2 = os.path.join(project_dir, "firmware.uf2")
    named_uf2 = os.path.join(project_dir, "firmware_node_" + node_id + ".uf2")
    full_name_uf2 = os.path.join(
        project_dir, "firmware_node_" + node_id + "_" + radio_name + ".uf2"
    )

    write_uf2(hex_path, build_uf2)
    shutil.copyfile(build_uf2, root_uf2)
    shutil.copyfile(build_uf2, named_uf2)
    shutil.copyfile(build_uf2, full_name_uf2)
    print("[UF2] Created real nRF52840 UF2:")
    print("[UF2]   " + build_uf2)
    print("[UF2]   " + root_uf2)
    print("[UF2]   " + named_uf2)
    print("[UF2]   " + full_name_uf2)


env.AddPostAction("$BUILD_DIR/${PROGNAME}.hex", create_uf2)
