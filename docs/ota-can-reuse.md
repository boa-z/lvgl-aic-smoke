# CAN-USB OTA reuse plan (forklift-meter-platform -> lvgl-aic-smoke)

## Location (2026-10-08)

The endpoint moved from lvgl-aic into this application: it is bench
debugging, not an LVGL feature. Paths below that read `update/`, `protocols/`,
`contracts/` and `tp/` are now under `ota/`; `third_party/iso14229` and
`tools/ota` are at this repository's root; the host contracts are
`tests/host/` here (they also build `meter-ota-inspect`). Kconfig symbols are
`AIC_LVGL_SMOKE_CAN_OTA`, `AIC_LVGL_SMOKE_CAN_OTA_VERSION` and
`AIC_LVGL_SMOKE_CAN_OTA_AUTOSTART` (formerly `AIC_LVGL_USE_CAN_OTA`,
`AIC_LVGL_CAN_OTA_*`; renamed throughout this document). The smoke main loop,
not the component manual-test timer, polls the progress overlay. Board
re-run after the move: USB-flashed 1.0.0, CAN `info`, CAN screenshot trigger,
download 1.0.1 (105 s, 10.3 KB/s), `activate --reboot`, `info` reports 1.0.1
slot B (SDK `output/lvgl-evidence/board-2026-10-08-ota-in-smoke/`).

Goal: the same wire protocol and host CLI as the forklift product
(CAN 500 kbit/s there; smoke profile matches at 500 kbit/s),
so one PCAN-USB adapter + one `tools/ota` CLI serves both. Third-party
libraries are reused with identical pins; only glue is new.

## Shared host scripts

`tools/ota` is an identical copy of the `tools/ota` in the forklift-meter-platform
Framework (not a cross-repository reference). `tools/ota/SHARED.sha256` records
the hashes of the shared files and `python tools/ota/check_shared.py` verifies
them. Change a shared file in one repository, run `check_shared.py --update`,
then copy the files and the manifest to the other. This copy adds only
`smoke_pack.py`, `package_inspector.c` and the smoke endpoint policy; the
`confirm` command is shared but only meaningful against this endpoint.

## S1 — vendor (this commit, no build integration)

Firmware, paths mirror the source tree for diffability:

- `third_party/iso14229/` — **submodule** of
  https://github.com/driftregion/iso14229 @`d018adc7` (latest,
  2026-09-29; newer than forklift's `2e36afc`). Full history, no file
  filtering. Windows needs `core.longpaths=true` (nested modules exceed
  MAX_PATH otherwise).
- Nested `src/tp/isotp-c` (SimonCahill/isotp-c): upstream rewrote
  history, so iso14229's recorded pin `259ee1bf` is orphaned and a plain
  recursive init fails. Resolved to latest `v1.9.3` (`1fc19e2f`) with
  `--separate-git-dir` outside the worktree (same path-length cause);
  glue API (`init/poll/receive/send`) verified present, full signature
  check is S2's compile gate. Fresh clones: init iso14229, then check
  out `src/tp/isotp-c` at `v1.9.3` (or current HEAD) the same way.
- **Superseded (2026-10-08):** iso14229 is now pinned at release `0.11.0`
  (`8a7eb23d`), the same pin as forklift-meter-platform. That release
  bundles isotp-c as plain files, so none of the nested-submodule handling
  above (separate git dir, `v1.9.3` checkout, `ignore = dirty`,
  `core.longpaths`) applies any more. The two items above remain as the record
  of why upstream `main` was not used: its isotp-c submodule commit is
  reachable only from a pull-request ref. Do not run
  `git submodule update --recursive` on iso14229 (stray gitlink without a
  `.gitmodules` mapping, exit 128).
- `update/meter_update.{c,h}` — pure job state machine
  (`BEGIN->...->CONFIRMED`; only `<string.h>` + own headers).
- `update/meter_package.{c,h}` — read-only CRC-CPIO validator
  (also builds for host as the package inspector).
- `protocols/uds/meter_uds.{c,h}` — UDS services
  (`RequestDownload/TransferData/TransferExit/RoutineControl
  0xF001/0xF002/ECUReset/RDBI 0xF180`) mapped to bounded jobs.
- `contracts/meter_update_view.h` — tiny state contract.

Host (`tools/ota/`, same pins as
`forklift.../tools/ota/requirements.txt`: `python-can==4.6.1`,
`can-isotp==2.0.7`, `udsoncan==1.25.1`):

- `client.py` / `transport.py` / `pack.py` / `package.py` /
  `__init__.py` / `__main__.py` — download/preflight/activate CLI.
- Deliberately NOT vendored (product-specific): `build_board.py`
  (forklift SDK defconfig flow; smoke uses `build.ps1 -WithCanOta`),
  `check_native_backend.py` (forklift paths), `__pycache__/`.

## S2 — smoke endpoint (done, `demo-meter-canota` image)

- `update/lv_aic_can_ota.{c,h}`: RX thread (blocking `can0` read,
  SW filter `0x7E0` → `isotp_on_can_message`), 5 ms service thread
  (`UDSServerPoll`), update worker (job MQ, adapted entry),
  shell `ota_can start|stop|status|info|maintenance on/off`.
- Admission = shell maintenance flag; NVM barrier trivially true
  (no app settings store — documented deviation); stop terminal
  until reboot; `reset` → CPU reset; `UDSMillis`/`send_can`/`get_us`
  via RT-Thread tick + `rt_device_write`.
- Backend: `update/meter_update_backend_aic.c` (include paths only),
  `update/meter_sha256_aic.c` (direct mbedtls sources, no package),
  native `ota.c`/`burn.c` via VariantDir (smoke profile leaves
  `LPKG_USING_OTA_DOWNLOADER` off).
- Kconfig `AIC_LVGL_SMOKE_CAN_OTA`, SConscript group (sources +
  12 CPPDEFINES incl. `UDS_CUSTOM_MILLIS`, `METER_AIC_OTA`),
  `build.ps1 -WithCanOta`, `check_integration.py --with-can-ota`
  (7 live symbols), host `update_contract` (state machine + package
  negatives) PASS.
- `demo-meter-canota` (sdk@e4cb5194): all gates PASS. Image SHA256
  `42bf83c16ab30bb7f4dd868d023754357885ca93c407e3f97c426af73cce1032`.
- Coexistence: OTA (`0x7E0/0x7E8`) and CAN capture (`0x1CA`) share
  CAN0 by ID; never run both transfers at once.

## S3 — board validation (bench, careful)

Baseline/candidate flow per forklift `docs/ota/can-update.md`: build A
and B from committed sources, flash A only, download B over PCAN-USB,
activate, observe new firmware. SPL/PBP USB recovery is the backstop.
No activation without a recorded package SHA256 + readback match.

- `maintkeep` image (`9d8a9edf...`, self-flashed via artinchip-flash):
  `maintenance on` survives `start` (`maintenance: 1` in host info);
  two consecutive host `info` exchanges PASS with `backend_supported:
  true`, 4 MiB candidate, zero drops/errors — isotp race fix holds.
- PCAN-USB (genuine PEAK, 500 kbit/s) + `tools/ota` CLI verified
  end to end at the UDS `info` level; download/activate pending
  explicit approval (flash writes).

## Board download PASS (2026-10-07)

`tools/ota download` of `output/ota-smoke-1.0.0` (1,049,600 bytes,
SHA256 `7f908242…04eb746`) over PCAN-USB at 500 kbit/s: **2050/2050
TransferData blocks, `state=CANDIDATE_READY`, `error=0`,
`received=1049600`, elapsed 101 s (10.4 KB/s), `drop=0`**. Evidence:
SDK `output/lvgl-evidence/board-2026-10-07-can-ota-download/`
(events.jsonl, can.asc, ota.manifest.json). Activation NOT performed.

### Debugging record — the non-monotonic console clock

Symptom family across this bring-up: intermittent `RequestSequenceError
(0x24)` at varying blocks, one silent response drop, and spurious
`METER_UPDATE_TIMEOUT`. Root cause: `ota_now_ms()` computed
`rt_tick_get() * 1000u` in 32-bit arithmetic. This board's RT-Thread
tick is not cleared by warm (watchdog/software) resets and had
accumulated near 2^32, so the product overflowed: the millisecond
value becomes a sawtooth that ramps 0..4,294,967 and jumps back every
~71.6 minutes of accumulated ticks. `meter_update_tick`'s unsigned
`now - last_activity` diff then read ~4.28e9 and failed the session
with TIMEOUT; the following write returned `METER_UPDATE_STATE`, which
`map_error` maps to 0x24. Fix: 64-bit intermediates in `ota_now_ms()`
and `isotp_user_get_us()`.

Latent defects fixed along the way (keep them):

- CAN bring-up: `RT_DEVICE_CTRL_SET_INT` needed, `msg.hdr=-1` under
  HDR, `SET_BAUD` takes a pointer-sized value not a pointer, 500 kbit/s
  product rate.
- isotp-c is not thread-safe: RX ingestion and `UDSServerPoll` share
  one link and are serialized by `ota_isotp_lock`.
- Host packer: `ota-subimgs.cfg` must be written LF-only on Windows
  (CRLF leaked `\r` into mkenvimage values).
- First-evaluation result wait (`ota_result()`, bounded 1.5 s) keeps
  the vendor RCRRP re-evaluation window rare (`n=2050` evals for 2050
  blocks on the passing run).
- Thread stacks last in the translation unit; 16 KiB worker stack.
- Lazy `ota_ipc_init()` for shell use before `start`; maintenance flag
  preserved across start.

## Board closed loop PASS (2026-10-07)

Full CAN OTA cycle in both directions on D50T-2-Lite, PCAN-USB 500 kbit/s.
Evidence: SDK `output/lvgl-evidence/board-2026-10-07-can-ota-closed-loop/`
(per-step `events.jsonl` + `can.asc`, serial boot logs). Images built with
`build.ps1 -Phase ge2d -WithMeter -WithCanCapture -WithCanOta -OtaVersion X`.

| Step | Result |
|------|--------|
| USB flash A (1.0.0, slot A) | `info`: version 1.0.0, slot A, `ready` |
| Download B 1.0.1 (1,057,792 B, `7c0f4282…`) | `CANDIDATE_READY`, 95.7 s, 11.0 KB/s |
| `activate --reboot` | bootloader `Start-up from os_r`; CAN `info` version 1.0.1, slot B |
| Plain reboot | still `os_r`; env `osAB_now=B osAB_next=B upgrade_available=0` |
| Download C 1.0.2 from B (`bdf6f403…`) | `CANDIDATE_READY`, 94.5 s, 11.2 KB/s |
| `activate --reboot` | `Start-up from os`; CAN `info` version 1.0.2, slot A |

Additions in this round:

- `AIC_LVGL_SMOKE_CAN_OTA_VERSION` (Kconfig string, `build.ps1 -OtaVersion`) so
  the candidate identity is provable after reboot.
- `AIC_LVGL_SMOKE_CAN_OTA_AUTOSTART` (default y): the smoke app starts the
  endpoint after its first presented frame, so the host reaches a freshly
  activated image without a serial console. Download still requires
  `maintenance on`.
- Info JSON reports `slot/next/trial/bootcount` (cached from the backend's
  env reads; never read from the service thread).
- RoutineControl `0xF003` / shell `lv_aic_can_ota confirm` /
  `python -m ota confirm --version X`: clears `upgrade_available` on a
  trial boot (`osAB_now == osAB_next`) and re-verifies the env from flash.

**Trial-confirm finding (open policy decision):** the SDK already confirms
every non-SD boot itself: `packages/artinchip/env/absystem_os.c`
`aic_absystem_mount_fs()` runs `aic_ota_status_update()` from
`INIT_ENV_EXPORT`, before any application code. The bootloader's
`bootlimit` rollback therefore only protects against images that die before
INIT_ENV; an image that boots the kernel but hangs in the app stays
selected. The application confirm path is in place but sees `trial=0`
on this SDK; the host reports `mode: sdk_auto_confirm`. A health-gated
trial needs that SDK hook disabled for the product (SDK change, not done).

Bench note: `artinchip-flash burn` timed out mid-transfer twice while an
`-j8` SDK build ran concurrently; it succeeded with the host idle.

## Smoke package + progress UI (done)

- `tools/ota/smoke_pack.py` drives the shared `pack()` for the smoke
  product (`lvgl-aic-smoke` / `d50t-2-lite` / `1.0.0`, 4 MiB candidate):
  `output/ota-smoke-1.0.0/{ota.cpio,ota.manifest.json,package-report.json}`,
  package SHA256 recorded in the report; preflight PASS via the native
  inspector (`output/meter-ota-inspect.exe`, built from
  `tools/ota/package_inspector.c` + `update/meter_package.c`).
- Windows bench notes: MSYS2 `pacman -S cpio u-boot-tools`
  (`msys/u-boot-tools` provides `mkenvimage`); `METER_OTA_INSPECTOR`
  points at the built inspector. `pack.py` writes `ota-subimgs.cfg`
  LF-only — text-mode CRLF translation leaked `\r` into values and
  failed byte-exact inspector matching (Linux CI never saw it; fix
  proposed upstream).
- `update/lv_aic_can_ota_widget.{c,h}`: fullscreen top-layer progress
  overlay mirroring `meter_update_widget` (title/phase/bar/percent/
  versions/error/note, fixed English); `ota_can ui show|close` via the
  UI-thread mailbox; 500 ms live refresh; host `ota_widget_contract`
  drives synthetic views (bar value, error visibility, render).
