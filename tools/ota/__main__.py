"""可自动化的 CAN OTA 命令行；默认只读预检，激活与重启显式请求。"""
import argparse
import json
from pathlib import Path
import sys
from .package import PackagePolicy
from .client import Manifest, UpdateError, activate, confirm, download, preflight


def main(argv=None):
    parser = argparse.ArgumentParser(description="Framework CAN OTA client")
    parser.add_argument("--interface", default="pcan")
    parser.add_argument("--channel", default="PCAN_USBBUS1")
    parser.add_argument("--bitrate", type=int, default=500000)
    parser.add_argument("--evidence", type=Path, help="new directory for JSONL and CAN ASC evidence")
    sub = parser.add_subparsers(dest="action", required=True)
    for name in ("preflight", "download"):
        command = sub.add_parser(name)
        command.add_argument("package", type=Path)
        command.add_argument("--manifest", type=Path, required=True)
        command.add_argument("--os-file", required=True)
        command.add_argument("--candidate-capacity", type=int, required=True)
    command = sub.add_parser("pack")
    command.add_argument("os_image", type=Path)
    command.add_argument("destination", type=Path)
    for field in ("product", "hardware", "version", "os-file"):
        command.add_argument("--" + field, required=True)
    command.add_argument("--candidate-capacity", type=int, required=True)
    # reference-board AIC OTA 后端按 4 KiB 写入候选分区；默认值必须满足该板级契约。
    command.add_argument("--pad-os-to", type=int, choices=(1, 4096), default=4096)
    command.add_argument("--cpio", default="cpio")
    command.add_argument("--mkenvimage", default="mkenvimage")
    sub.add_parser("info", aliases=["probe"])
    sub.add_parser("abort")
    command = sub.add_parser("activate")
    command.add_argument("--version", required=True)
    command.add_argument("--reboot", action="store_true")
    command = sub.add_parser("confirm")
    command.add_argument("--version", required=True)
    args = parser.parse_args(argv)
    log = None
    def emit(event):
        line = json.dumps(event, ensure_ascii=False)
        print(line, flush=True)
        if log:
            log.write(line + "\n")
            log.flush()
    try:
        policy = PackagePolicy(args.os_file, args.candidate_capacity) if args.action in ("preflight", "download", "pack") else None
        if args.action == "pack":
            from .pack import pack
            emit(pack(args.os_image, args.destination, args.product, args.hardware, args.version, policy,
                      cpio=args.cpio, mkenvimage=args.mkenvimage, pad_os_to=args.pad_os_to))
            return 0
        if args.action == "preflight":
            emit(preflight(args.package, Manifest.load(args.manifest), policy))
            return 0
        if not args.evidence:
            raise UpdateError("--evidence is required for CAN operations")
        args.evidence.mkdir(parents=True, exist_ok=False)
        log = (args.evidence / "events.jsonl").open("x", encoding="utf-8")
        manifest = None
        if args.action == "download":
            manifest = Manifest.load(args.manifest)
            emit({"event": "offline_preflight", **preflight(args.package, manifest, policy)})
        from .transport import connect
        with connect(args.channel, args.interface, args.bitrate, args.evidence / "can.asc") as client:
            if args.action in ("info", "probe"):
                emit({"event": "info", "device": client.info()})
            elif args.action == "download":
                download(client, args.package, manifest, emit, package_policy=policy)
            elif args.action == "activate":
                emit(activate(client, args.version, args.reboot))
            elif args.action == "confirm":
                emit(confirm(client, args.version))
            elif args.action == "abort":
                client.programming()
                client.abort()
                emit({"event": "abort", "device": client.info()})
        return 0
    except KeyboardInterrupt:
        emit({"event": "error", "reason": "interrupted"})
        return 130
    except Exception as exc:
        emit({"event": "error", "reason": str(exc), "type": type(exc).__name__})
        return 1
    finally:
        if log:
            log.close()


if __name__ == "__main__":
    sys.exit(main())
