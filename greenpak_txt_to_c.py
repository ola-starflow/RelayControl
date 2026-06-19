#!/usr/bin/env python3
"""
Convert a GreenPAK Designer bit export table to a C byte array.

Expected input format:
    index <tab/space> value <tab/space> comment
    0             0        //
    1             1        //

The exported index is treated as a bit index in the SLG RAM address space:
    byte_address = index // 8
    bit_position = index % 8

That mapping matches the SLG register bit numbering used in the datasheet, for
example I2C_CONTROL_CODE_SEL[819:816].

The SLG47011 RAM/register I2C-visible address range is 0x0000..0x2249
inclusive, so the default generated config is truncated to 8778 bytes. The
uploaded GreenPAK export may contain extra bits beyond that; those are ignored
unless --max-bytes is changed.
"""

from __future__ import annotations

import argparse
import datetime as _dt
import pathlib
import re
import sys
from typing import List, Tuple

DEFAULT_MAX_BYTES = 0x224A  # valid addresses 0x0000..0x2249 inclusive


def parse_bit_file(path: pathlib.Path) -> Tuple[List[int], int, int]:
    values: dict[int, int] = {}
    max_index = -1

    with path.open("r", encoding="utf-8", errors="replace") as f:
        for line_no, line in enumerate(f, 1):
            text = line.strip()
            if not text:
                continue
            if text.lower().startswith("index"):
                continue

            # Accept tab separated and whitespace separated versions. Comments
            # after the value are ignored.
            m = re.match(r"^\s*(\d+)\s+(0|1)\b", line)
            if not m:
                continue

            idx = int(m.group(1))
            val = int(m.group(2))

            if idx in values:
                raise ValueError(f"duplicate bit index {idx} at line {line_no}")

            values[idx] = val
            if idx > max_index:
                max_index = idx

    if max_index < 0:
        raise ValueError("no bit rows found")

    bit_count = max_index + 1
    bits = [0] * bit_count

    missing = []
    for idx in range(bit_count):
        if idx not in values:
            missing.append(idx)
        else:
            bits[idx] = values[idx]

    if missing:
        preview = ", ".join(str(x) for x in missing[:10])
        raise ValueError(f"missing bit indices, first missing: {preview}")

    return bits, bit_count, max_index


def pack_bits_lsb_first(bits: List[int], max_bytes: int) -> Tuple[bytearray, int]:
    total_bytes_from_file = (len(bits) + 7) // 8
    out_len = min(total_bytes_from_file, max_bytes)
    data = bytearray(out_len)

    used_bits = min(len(bits), out_len * 8)
    for bit_index in range(used_bits):
        if bits[bit_index]:
            data[bit_index // 8] |= 1 << (bit_index % 8)

    ignored_bits = max(0, len(bits) - used_bits)
    return data, ignored_bits


def c_array_literal(data: bytes, symbol: str) -> str:
    lines = []
    lines.append(f"const uint8_t {symbol}[] =")
    lines.append("{")

    for offset in range(0, len(data), 16):
        chunk = data[offset:offset + 16]
        hexes = ", ".join(f"0x{b:02X}" for b in chunk)
        lines.append(f"    {hexes},")

    lines.append("};")
    return "\n".join(lines)


def write_files(data: bytes,
                ignored_bits: int,
                bit_count: int,
                input_path: pathlib.Path,
                output_c: pathlib.Path,
                output_h: pathlib.Path,
                symbol: str) -> None:
    guard = output_h.name.upper().replace(".", "_").replace("-", "_")
    timestamp = _dt.datetime.utcnow().replace(microsecond=0).isoformat() + "Z"

    output_h.write_text(f"""#ifndef {guard}\n#define {guard}\n\n#include <stdint.h>\n#include <stddef.h>\n\n#define SLG47011_RAM_CONFIG_START_ADDR   0x0000u\n#define SLG47011_RAM_CONFIG_END_ADDR     0x2249u\n#define SLG47011_RAM_CONFIG_MAX_LEN      0x224Au\n\nextern const uint8_t {symbol}[];\nextern const size_t {symbol}_len;\n\n#endif /* {guard} */\n""", encoding="utf-8")

    output_c.write_text(f"""/*\n * Auto-generated from: {input_path.name}\n * Generated UTC: {timestamp}\n * Source bits: {bit_count}\n * Bytes generated: {len(data)}\n * Ignored source bits beyond 0x2249: {ignored_bits}\n *\n * Do not edit by hand. Regenerate with scripts/greenpak_txt_to_c.py.\n */\n\n#include \"slg47011_config_data.h\"\n\n#include <stdint.h>\n#include <stddef.h>\n\n{c_array_literal(data, symbol)}\n\nconst size_t {symbol}_len = sizeof({symbol});\n""", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=pathlib.Path, help="GreenPAK Designer text export")
    parser.add_argument("--output-c", type=pathlib.Path, default=pathlib.Path("testSW/App/Src/slg47011_config_data.c"))
    parser.add_argument("--output-h", type=pathlib.Path, default=pathlib.Path("testSW/App/Inc/slg47011_config_data.h"))
    parser.add_argument("--symbol", default="slg47011_ram_config")
    parser.add_argument("--max-bytes", type=lambda s: int(s, 0), default=DEFAULT_MAX_BYTES,
                        help="maximum bytes to generate, default 0x224A")
    args = parser.parse_args()

    bits, bit_count, max_index = parse_bit_file(args.input)
    data, ignored_bits = pack_bits_lsb_first(bits, args.max_bytes)

    args.output_c.parent.mkdir(parents=True, exist_ok=True)
    args.output_h.parent.mkdir(parents=True, exist_ok=True)

    write_files(data, ignored_bits, bit_count, args.input, args.output_c, args.output_h, args.symbol)

    print(f"Parsed {bit_count} bits, max index {max_index}")
    print(f"Generated {len(data)} bytes")
    print(f"Ignored {ignored_bits} bits beyond generated range")
    print(f"Wrote {args.output_h}")
    print(f"Wrote {args.output_c}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
