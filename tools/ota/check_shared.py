#!/usr/bin/env python3
"""核对上位机升级脚本与另一个仓库里的副本保持一致。

lvgl-aic-smoke 与 forklift-meter-platform 各带一份相同的 tools/ota（不跨仓库引用）。
SHARED.sha256 记录共享文件的哈希，两个仓库里这份清单必须相同：改了共享文件，
先在一处修改并 --update，再把同样的文件与清单拷到另一处，否则这里会失败。

用法：python check_shared.py          核对
      python check_shared.py --update 修改共享文件后重写清单
"""
import argparse
import hashlib
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
SHARED = ("__init__.py", "__main__.py", "client.py", "pack.py", "package.py", "transport.py", "update.py",
          "requirements.txt")
MANIFEST = HERE / "SHARED.sha256"


def digests():
    # 先把换行统一成 LF，Windows 检出的 CRLF 不应改变哈希。
    return {name: hashlib.sha256((HERE / name).read_bytes().replace(b"\r\n", b"\n")).hexdigest()
            for name in SHARED}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--update", action="store_true")
    args = parser.parse_args(argv)
    current = digests()
    if args.update:
        MANIFEST.write_bytes("".join("%s  %s\n" % (current[name], name) for name in SHARED).encode("ascii"))
        return 0
    recorded = {}
    for line in MANIFEST.read_text(encoding="ascii").splitlines():
        digest, name = line.split("  ", 1)
        recorded[name] = digest
    stale = [name for name in SHARED if recorded.get(name) != current[name]]
    if stale:
        print("shared OTA scripts differ from SHARED.sha256: " + ", ".join(stale), file=sys.stderr)
        print("sync them with the other repository's copy, then run --update", file=sys.stderr)
        return 1
    print("shared OTA scripts match SHARED.sha256")
    return 0


if __name__ == "__main__":
    sys.exit(main())
