# Application-owned LVGL 9.6 integration

This repository is the platform smoke application for the ArtInChip Luban-Lite
SDK. It is consumed as one submodule at `application/rt-thread/lvgl-aic-smoke`
and itself consumes three pinned third-party submodules:

```text
lvgl-aic-smoke/
  Kconfig                 application settings (+ the component Kconfig)
  SConscript              application sources (+ the component SConscript)
  main.c                  LVGL lifecycle, test page and UI thread
  ota/                    CAN-USB OTA debug endpoint (bench tool, see below)
  watchdog/               bench recovery watchdog ([docs/watchdog.md](docs/watchdog.md))
  tools/ota/              host CLI and packer for that endpoint
  tests/host/             host contracts for ota/
  docs/ota-can-reuse.md   OTA design, provenance and board records
  third_party/
    lvgl/                 upstream LVGL v9.6.0
    lvgl-aic/             ArtInChip LVGL adapter, configuration and test tooling
    iso14229/             UDS server used only by ota/
```

The SDK supplies board configuration, RT-Thread, framebuffer/touch drivers and
MPP/GE interfaces. The LVGL port, OS adaptation and LVGL development tools
(manual pages, CAN/UART screenshots, official SDK demos) belong to
`third_party/lvgl-aic`. Bench debugging that is not an LVGL feature, such as
firmware update over CAN, belongs to this application. Do not patch `kernel/`,
add SDK-wide LVGL configuration, or register these dependencies under
`packages/`. Product applications can consume the same `lvgl`/`lvgl-aic`
sibling layout with their own entry point and runtime policy.

`LPKG_USING_LVGL` must stay disabled: the legacy SDK LVGL package and this path
cannot be linked together. `RT_USING_EVENT` is required by the component's
binary notification adapter. The smoke application additionally requires the
component's display and manual-test features.

## Dependencies

- LVGL is pinned to v9.6.0 (`80ca777e37a2b176770726a02e07a6fb79ef0b39`).
- lvgl-aic is pinned by this repository's gitlink and follows its `main` (the
  update branch named in `.gitmodules`).
- iso14229 is pinned at release 0.11.0 (`8a7eb23d`), the same pin as the
  forklift-meter-platform product, so one host CLI serves both. 0.11.0 bundles
  isotp-c as plain files: no nested checkout. Upstream `main` after 0.11.0 turns
  isotp-c into a nested submodule whose recorded commit is reachable only from a
  pull-request ref (a plain recursive init fails), so it is deliberately not used.
- The SDK pins this repository and never advances any dependency on its own.

From the repository root (from the SDK, run it inside
`application/rt-thread/lvgl-aic-smoke`):

```powershell
git submodule update --init --recursive third_party/lvgl third_party/lvgl-aic
git submodule update --init third_party/iso14229
```

Do not add `--recursive` for iso14229: its isotp-c tree contains a stray gitlink
(`doxygen-awesome-css`) with no `.gitmodules` mapping, so `git submodule update
--init --recursive` exits 128 there (and would also fetch an STM32Cube example
submodule nothing here uses). Only the OTA endpoint needs iso14229.

For an intentional component update, first ensure that its checkout is clean,
then fetch and review the upstream change before advancing the pin:

```powershell
git -C third_party/lvgl-aic fetch origin
git -C third_party/lvgl-aic log --oneline HEAD..origin/<branch>
git -C third_party/lvgl-aic merge --ff-only origin/<branch>
git add third_party/lvgl-aic
```

The integration gate compares the component HEAD against this repository's
gitlink, so stage the reviewed pin before validating. Advancing the pin here is
completed by a commit in this repository and a new pin in the SDK. LVGL itself
is not advanced by this procedure.

## Build identity

The application SConscript derives identities from the local SDK,
`third_party/lvgl-aic` and `third_party/lvgl` checkouts and writes them to the
gitignored `build/lvgl_aic_build_id.h` on every parse. The smoke thread logs
them as its first three messages:

    I/lvgl.aic.smoke: build: sdk=<branch>@<commit>[+dirty]
    I/lvgl.aic.smoke: build: lvgl-aic=<branch>@<commit>[+dirty]
    I/lvgl.aic.smoke: build: lvgl=<tag>@<commit>

One identity per line is deliberate: the ulog line buffer is 128 bytes
including timestamp, level/tag and colour codes, so a combined banner is
truncated on the board console. Use these three lines, not the `Built on ...`
banner timestamp, to identify the system under test in a board log.

## Application configuration

| Symbol | Default | Meaning |
|--------|---------|---------|
| `AIC_LVGL_SMOKE_THREAD_STACK_SIZE` | 32768 | UI thread stack (bytes) |
| `AIC_LVGL_SMOKE_THREAD_PRIO` | 20 | UI thread priority |
| `AIC_LVGL_SMOKE_CYCLES` | 3 | init/deinit lifecycle cycles before the persistent page |
| `AIC_LVGL_SMOKE_FRAMES` | 5 | timer iterations per lifecycle cycle |
| `AIC_LVGL_SMOKE_WATCHDOG` | y | bench recovery watchdog: a locked-up test resets the board after about 10 s (needs `AIC_USING_WDT`) |
| `AIC_LVGL_SMOKE_CAN_OTA` | n | CAN-USB OTA endpoint (needs `AIC_USING_CAN0`, `RT_USING_CAN`) |
| `AIC_LVGL_SMOKE_CAN_OTA_VERSION` | `"1.0.0"` | version the endpoint reports |
| `AIC_LVGL_SMOKE_CAN_OTA_AUTOSTART` | y | start the endpoint after the first presented frame |

These Kconfig symbols are emitted into `rtconfig.h`; there are no legacy
`LPKG_LVGL_*` fallbacks or SCons command-line overrides. Rendering
configuration comes from the component's reviewed `lv_conf.h` (10 ms refresh
period); the display color format follows the framebuffer (`AICFB_*`), and the
smoke profiles use RGB888.

The three `target/configs/d13x_d50t-2-lite_rt-thread_lvgl-aic-*_defconfig`
profiles select this application. They differ by software rendering, MPP image
decoding, and MPP plus GE2D. The SDK's profile loader requires these files under
`target/configs`; application code and tooling remain in the application tree.

## Build

Use a dedicated worktree: each SDK build changes `.config`, generated headers,
board bootloader staging and `output/` in its containing checkout. Do not run
these commands in the active product checkout or run two SDK profiles
concurrently in one worktree.

```powershell
$build = './application/rt-thread/lvgl-aic-smoke/third_party/lvgl-aic/tools/sdk/build.ps1'
& $build -Phase gate1 -Jobs 8
& $build -Phase mpp -Jobs 8
& $build -Phase ge2d -Jobs 8
# Typical bench image: official SDK demos on the virtual 1024x600 display,
# CAN screenshots and the CAN OTA endpoint.
& $build -Phase ge2d -Jobs 8 -OfficialDemos meter,dashboard -WithCanCapture -WithCanOta -EvidenceTag bench
```

Each entry builds the bootloader and application, checks the link map for the
application-owned LVGL/adapter objects and absence of legacy LVGL, verifies the
image, and records source state and artifact hashes under
`output/lvgl-evidence/<variant>-<tag>`. Development builds with component edits
require `-AllowComponentDirty`. The script lives in the component but builds
this application, so it also toggles this app's switches: `-WithCanOta
[-OtaVersion X]` sets `AIC_LVGL_SMOKE_CAN_OTA`. Component switches
(`-OfficialDemos`, `-WithCanCapture`, `-WithFonts`, `-WithPlayer`, ...) are
described in the component's [build tools](third_party/lvgl-aic/tools/sdk/README.md)
and [official demos](third_party/lvgl-aic/demos/official/README.md).

## Flash and observe

The build scripts do not flash a board.

- Flash the whole image with the SDK's `tools/scripts/upgcmd.exe -p image <img>`
  then `upgcmd shcmd reset` (what `scons --burn` does; the shell command
  `aicupg` puts the board in USB upgrade mode). `artinchip-flash` 0.1.0 skips
  the rodata/data partitions, so assets go stale with it.
- Serial console: 115200 8N1. Shell: `lv_aic_demo list|show <name>|close|status`.
- A locked-up test resets by itself after about 10 s (recovery watchdog,
  `lv_aic_watchdog start|stop|status|regs|hang`; `stop` it before a debugger
  halt; details in [docs/watchdog.md](docs/watchdog.md)).
- Screenshot over CAN (PCAN-USB, 500 kbit/s), no serial needed:
  `python third_party/lvgl-aic/tools/sdk/can_capture.py --interface pcan --channel PCAN_USBBUS1 --trigger out.png`.
  The trigger frame is received by the OTA endpoint (the only can0 reader), so
  `--trigger` needs an image built with `-WithCanOta`; otherwise start the
  capture from the shell.

## CAN OTA debug endpoint

`ota/` is a UDS-over-ISO-TP update endpoint (RX `0x7E0` / TX `0x7E8` on CAN0)
for reflashing the bench board over CAN with A/B activation. It reuses the
forklift-meter-platform wire protocol, update state machine and package format
(`ota/update/meter_*` and `ota/protocols/uds/` stay identical to the product
copies for diffing) plus the SDK native OTA installer. It is debugging
infrastructure of this app, not an LVGL feature: no lvgl-aic source depends on
it, and the component's CAN capture only receives its trigger through it.

```powershell
cd application/rt-thread/lvgl-aic-smoke/tools
python -m pip install -r ota/requirements.txt
$can = '--interface','pcan','--channel','PCAN_USBBUS1','--bitrate','500000'
python -m ota @can info
# Board shell: lv_aic_can_ota maintenance on
python -m ota @can download --manifest <dir>/ota.manifest.json --os-file d13x_os.itb `
    --candidate-capacity 4194304 <dir>/ota.cpio
python -m ota @can activate --version 1.0.1 --reboot
python -m ota @can info
```

Build the candidate with `-WithCanOta -OtaVersion 1.0.1` and package its
`d13x_os.itb` with
`python ota/smoke_pack.py --os-image <itb> --out-dir <dir> --version 1.0.1`.
Packing needs GNU `cpio` and `mkenvimage` (MSYS2 `C:\msys64\usr\bin`) and
`METER_OTA_INSPECTOR` pointing at the package inspector, which the host build
below produces as `output/smoke-host/meter-ota-inspect.exe`. The SDK confirms
every non-SD boot itself (`packages/artinchip/env/absystem_os.c`), so a trial
image is never left unconfirmed and bootloader rollback only covers images that
die before `INIT_ENV`; see the open policy note in the OTA document.

## Host contracts

Component contracts (LVGL adapter, GE2D, decoders, demos):

```powershell
$sdk = (Get-Location).Path
$thirdParty = "$sdk/application/rt-thread/lvgl-aic-smoke/third_party"
cmake -S "$thirdParty/lvgl-aic/tests/host" -B output/lvgl-host-app -G Ninja "-DLVGL_ROOT=$thirdParty/lvgl" "-DAIC_SDK_ROOT=$sdk" -DCMAKE_BUILD_TYPE=Debug
cmake --build output/lvgl-host-app -j 8
ctest --test-dir output/lvgl-host-app --output-on-failure
```

OTA contracts of this app (update state machine, progress overlay):

```powershell
cmake -S application/rt-thread/lvgl-aic-smoke/tests/host -B output/smoke-host -G Ninja
cmake --build output/smoke-host -j 8
ctest --test-dir output/smoke-host --output-on-failure
```

Host contracts and successful image builds do not establish hardware acceptance.
On a board run, collect the `event binary-sync self-test passed`, three
lifecycle passes and first-frame log, then verify touch and the selected
decoder/GE2D page. The ISR signal API is invoked from thread context by the
startup self-test; actual interrupt-context scheduling is a separate check.

See [VALIDATION.md](VALIDATION.md) for the board runs, image hashes and
remaining hardware acceptance work.
