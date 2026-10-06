# Application-owned LVGL 9.6 integration

This repository is the platform smoke application for the ArtInChip Luban-Lite
SDK. It is consumed as one submodule at `application/rt-thread/lvgl-aic-smoke`
and itself consumes two pinned third-party submodules:

```text
lvgl-aic-smoke/
  Kconfig                 application runtime settings + component Kconfig
  SConscript              application sources + component SConscript
  main.c                  LVGL lifecycle, test page and UI thread
  third_party/
    lvgl/                 upstream LVGL v9.6.0
    lvgl-aic/             ArtInChip adapter, configuration and test tooling
```

The SDK supplies board configuration, RT-Thread, framebuffer/touch drivers and
MPP/GE interfaces. The LVGL port, OS adaptation and development tools belong to
`third_party/lvgl-aic`. Do not patch `kernel/`, add SDK-wide LVGL configuration,
or register this dependency under `packages/`. Product applications can consume
the same sibling submodule layout with their own entry point and runtime policy.

`LPKG_USING_LVGL` must stay disabled: the legacy SDK LVGL package and this path
cannot be linked together. `RT_USING_EVENT` is required by the component's
binary notification adapter. The smoke application additionally requires the
component's display and manual-test features.

## Dependencies

- LVGL remains pinned to v9.6.0 (`80ca777e37a2b176770726a02e07a6fb79ef0b39`).
- lvgl-aic is pinned by this repository's gitlink; the SDK pins this repository
  and never advances either dependency on its own. `.gitmodules` identifies
  `main` as lvgl-aic's update branch. A normal submodule update reproduces the
  committed versions.

Run from the repository root (from the SDK, pass `--recursive` to the update
for `application/rt-thread/lvgl-aic-smoke`):

```powershell
git submodule update --init --recursive
```

For an intentional component update, first ensure that its checkout is clean,
then fetch and review the upstream change before advancing the pin:

```powershell
git -C third_party/lvgl-aic fetch origin
git -C third_party/lvgl-aic log --oneline HEAD..origin/main
git -C third_party/lvgl-aic merge --ff-only origin/main
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
including timestamp, level/tag and colour codes, so the earlier combined
137-character banner was truncated after the `lvgl-aic` tail on the board
console. Use these three lines, not the `Built on ...` banner timestamp, to
identify the system under test in a board log.

## Application configuration

The application owns `AIC_LVGL_SMOKE_THREAD_STACK_SIZE` (32768 bytes),
`AIC_LVGL_SMOKE_THREAD_PRIO` (20), `AIC_LVGL_SMOKE_CYCLES` (3), and
`AIC_LVGL_SMOKE_FRAMES` (5 timer iterations per cycle). These Kconfig symbols
are emitted into `rtconfig.h`; there are no legacy `LPKG_LVGL_*` fallbacks or
SCons command-line overrides. Rendering configuration comes from the component's
reviewed `lv_conf.h`, including RGB565 and the 10 ms refresh period.

The three `target/configs/d13x_d50t-2-lite_rt-thread_lvgl-aic-*_defconfig`
profiles select this application. They differ by software rendering, MPP image
decoding, and MPP plus GE2D. The SDK's profile loader requires these files under
`target/configs`; application code and tooling remain in the application tree.

## Build and validation

Use a dedicated worktree: each SDK build changes `.config`, generated headers,
board bootloader staging and `output/` in its containing checkout. Do not run
these commands in the active Hangcha product checkout or run two SDK profiles
concurrently in one worktree.

```powershell
$build = './application/rt-thread/lvgl-aic-smoke/third_party/lvgl-aic/tools/sdk/build.ps1'
& $build -Phase gate1 -Jobs 8
& $build -Phase mpp -Jobs 8
& $build -Phase ge2d -Jobs 8
```

Each entry builds the bootloader and application, checks the link map for the
application-owned LVGL/adapter objects and absence of legacy LVGL, verifies the
image, and records source state and artifact hashes under
`output/lvgl-evidence/{gate1,mpp,ge2d}`. Development builds with component edits
require the explicit `-AllowComponentDirty` switch.

For host contract tests with a configured CMake/Ninja C compiler:

```powershell
$sdk = (Get-Location).Path
$thirdParty = "$sdk/application/rt-thread/lvgl-aic-smoke/third_party"
cmake -S "$thirdParty/lvgl-aic/tests/host" -B output/lvgl-host-app -G Ninja "-DLVGL_ROOT=$thirdParty/lvgl" "-DAIC_SDK_ROOT=$sdk" -DCMAKE_BUILD_TYPE=Debug
cmake --build output/lvgl-host-app -j 8
ctest --test-dir output/lvgl-host-app --output-on-failure
```

Host contracts and successful image builds do not establish hardware acceptance.
On a separately authorized board run, collect the `event binary-sync self-test
passed`, three lifecycle passes and first-frame log, then verify touch and the
selected decoder/GE2D page. The ISR signal API is invoked from thread context by
the startup self-test; actual interrupt-context scheduling is a separate check.
The build scripts do not flash a board.

See [VALIDATION.md](VALIDATION.md) for the 2026-09-30 build matrix, image hashes
and remaining hardware acceptance work.
