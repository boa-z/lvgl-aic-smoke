#!/usr/bin/env python3
"""Build a smoke-app OTA package with the shared packer (no SDK edits).

Product/hardware/version must match the device endpoint policy
(product lvgl-aic-smoke, hardware d50t-2-lite, version 1.0.0); the
candidate capacity (4 MiB) matches meter_aic_update_prepare().
Needs GNU cpio + mkenvimage on PATH (MSYS2: pacman -S cpio uboot-tools).

Example:
  python3 smoke_pack.py --os-image output/.../images/d13x_os.itb
      --out-dir output/ota-smoke-1.0.0
"""
import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from ota.pack import pack
from ota.package import PackagePolicy


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--os-image", type=Path, required=True)
    parser.add_argument("--out-dir", type=Path, required=True)
    parser.add_argument("--version", default="1.0.0")
    args = parser.parse_args()
    policy = PackagePolicy(os_file="d13x_os.itb", candidate_capacity=4 * 1024 * 1024)
    result = pack(args.os_image, args.out_dir, "lvgl-aic-smoke", "d50t-2-lite",
                  args.version, policy)
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
