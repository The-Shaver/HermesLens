#!/usr/bin/env python3
"""Post-build merge: combine bootloader + partitions + app into one flashable .bin."""
import sys
from pathlib import Path

try:
    from esptool import main as esptool_main
except ImportError:
    sys.stderr.write("ERROR: esptool not installed. Run: pip install esptool\n")
    sys.exit(1)

# Standard ESP32-S3 flash offsets
OFFSETS = {
    "bootloader": "0x0000",
    "partitions": "0x8000",
    "app": "0x00010000",
}

def main() -> int:
    build_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(".pio/build/hermeslens-s3")
    build_dir = Path(build_dir).resolve()

    inputs = {
        "bootloader": build_dir / "bootloader.bin",
        "partitions": build_dir / "partitions.bin",
        "app": build_dir / "firmware.bin",
    }

    missing = [name for name, path in inputs.items() if not path.exists()]
    if missing:
        sys.stderr.write(f"ERROR: Missing build artifacts: {', '.join(missing)}\n")
        return 1

    output = build_dir / "hermeslens-s3-merged.bin"

    cmd = [
      "--chip", "esp32s3",
      "merge-bin",
      "-o", str(output),
      "--flash-mode", "dio",
      "--flash-freq", "80m",
      "--flash-size", "8MB",
      OFFSETS["bootloader"], str(inputs["bootloader"]),
      OFFSETS["partitions"], str(inputs["partitions"]),
      OFFSETS["app"], str(inputs["app"]),
    ]

    print(f"[postbuild] Merging {len(inputs)} binaries -> {output.name}")
    try:
      esptool_main(cmd)
    except SystemExit as exc:
      if int(exc.code) != 0:
        raise
    print(f"[postbuild] Output size: {output.stat().st_size:,} bytes")
    return 0

if __name__ == "__main__":
    sys.exit(main())
