#!/usr/bin/env python3
"""Run the real decoder/write engine with a fake clock/UART under ASan/UBSan.

Usage: python3 tests/run_host_tests.py --fastcrc /path/to/FastCRC [capture.pcap ...]
FastCRC must be a checkout of https://github.com/vinnyspb/FastCRC.
"""
import argparse
import json
import re
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--fastcrc", type=Path, required=True)
parser.add_argument("--library", type=Path)
parser.add_argument("--protocol", type=int, default=170, choices=[160,170,180,221])
parser.add_argument("pcaps", type=Path, nargs="*")
args = parser.parse_args()
lib = args.library or (ROOT if (ROOT / "comfortzone_heatpump.cpp").exists()
                       else ROOT / "components/comfortzone/third_party/comfortzone_heatpump")
if args.pcaps:
    from pcap_decode_v170 import packets
with tempfile.TemporaryDirectory(prefix="comfortzone-tests-") as tmp:
    binary = Path(tmp) / "write_test"
    registers = sorted(set(re.findall(r"\{\s*((?:0x[0-9a-fA-F]{2}\s*,\s*){8}0x[0-9a-fA-F]{2})\s*\}",
                                     (lib / "comfortzone_decoder.cpp").read_text())))
    (Path(tmp) / "register_fixture.h").write_text(
        "static const byte registers[][9] = {" + ",".join("{"+r+"}" for r in registers) + "};")
    if args.protocol == 170:
        cases = json.loads((ROOT / "tests/v170_frequency_fixtures.json").read_text())["frames"]
        rows = ["{" + str(c["actual_frequency_raw"]) + ",{ " +
                ",".join(str(b) for b in bytes.fromhex(c["raw"])) + "}}" for c in cases]
        (Path(tmp) / "frequency_fixture.h").write_text(
            "static const struct { int16_t frequency; std::vector<byte> raw; } frequency_cases[] = {" +
            ",".join(rows) + "};")
    subprocess.run([
        "g++", "-std=c++17", "-g", "-O1", "-fsanitize=address,undefined",
        "-fno-sanitize-recover=all", "-fno-omit-frame-pointer", f"-DHP_PROTOCOL={args.protocol}",
        *(["-DTEST_ESPHOME_PROBE"] if (ROOT / "components/comfortzone/comfortzone_probe.h").exists() and not args.library else []),
        "-include", str(ROOT / "tests/host_clock.h"),
        "-I", tmp, "-I", str(lib), "-I", str(args.fastcrc / "src"),
        str(ROOT / "tests" / ("write_test.cpp" if args.protocol == 170 else "protocol_smoke.cpp")),
        *map(str, sorted(lib.glob("*.cpp"))),
        str(args.fastcrc / "src/FastCRCsw.cpp"), "-o", str(binary)
    ], check=True, timeout=120)
    frames = Path(tmp) / "frames.bin"
    with frames.open("wb") as output:
        for pcap in args.pcaps:
            for _, payload in packets(pcap):
                output.write(struct.pack("<H", len(payload)))
                output.write(payload)
    subprocess.run([str(binary), str(frames)], check=True, timeout=30)
