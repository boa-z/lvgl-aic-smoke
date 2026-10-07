# Application-owned LVGL validation - 2026-09-30

## Source baseline

- SDK build base: `78f0648c` plus the application integration patch archived with each build.
- LVGL: `80ca777e37a2b176770726a02e07a6fb79ef0b39` (v9.6.0).
- lvgl-aic: `2294fafdbe36916d60ac311f49a1e32283cefbb8` (fetched `origin/main`).
- Builds ran only in the dedicated `d50t-lvgl-port` worktree on `codex/port-lvgl-9.6`.
- No SDK kernel, driver, package, global Kconfig or build-system source changes were required.

## Board run (2026-10-06)

The latest `ge2d` image (evidence tag
`ge2d-fonts-widgets-aicp-player-apng-ge2d-yuv-layer-final3`: SDK `1d4f04df`,
lvgl-aic `86dab31`, LVGL `80ca777e`, image SHA-256
`79129AD21EE77E9F07934702B749FA4E151E8BF2BB52C09E3122A466E4248442`) was
flashed to D50T-2-Lite and booted. The raw serial log (115200 8N1) is archived
in this worktree at `output/lvgl-evidence/board-2026-10-06-serial.log`. The
boot banner reports Luban-Lite 1.3.2, `Image version: 1.0.0`, tinySPL
`Sep 30 2026 01:05:33` and application `Built on Oct 5 2026 05:33:21`, with
reset reason `Command-Reboot`. The application banner timestamp is the last
compile time of the shared banner object and appears in the
`yuv-mask-transformed-tiles` (`DFF751A9...`), `recolor-colorkey` (`CE5172...`),
`ge-source-admission` (`34C493...`) and `yuv-layer` (`79129A...`) images alike,
so it does not uniquely identify the flashed image. The probe set matches the
latest candidate, but the identity remains inferred rather than proven. From
SDK `262be369` on, the smoke thread logs the source identity as its first
messages. The single combined line used through `d4c631ef` was truncated on
the board: ulog formats each record into a 128-byte buffer that also carries
the timestamp, level/tag and colour codes, so the 137-character banner lost
its `lvgl=9.6.0@80ca777e` tail after 115 visible characters. One identity per
line keeps every field inside that budget:

    build: sdk=<branch>@<hash>[+dirty]
    build: lvgl-aic=<branch>@<hash>[+dirty]
    build: lvgl=<tag>@<hash>

Passed in this boot:

- `RT-Thread event binary-sync self-test passed`, three init/deinit lifecycle
  cycles, `LVGL 9.6 smoke page is running` and `first frame presented`.
- FreeType: 18/28/42 px latin+CJK hashes, cache busy guard/selective purge/
  identical rebuild and native font metrics/bitmap/fallback/cache churn.
- MPP fixtures including corrupt/unsupported inputs; resource stage FILE/memory
  pixel parity and 100-hit caches without new CMA for PNG, BMP (RGB555/RGB565/
  24/32) and AICP `bird.aicp` (alpha fixture SKIP: requires V31); 1000
  decode/close cycles with `CMA current=0 peak=61440 alloc=1000 free=1000`.
- GE2D fill: 12 solid-fill probes, 3 fake-replace probes, video-window
  alpha-zero probes and 84 ARGB solid/fake fill probes, all `guards=OK`.

Failed or not evaluated in this boot:

- `FAIL key565 sample=1 y=0 byte=0 got=255 want=165`: the raw-GE RGB565
  color-key probe matched sample 0 (`key=000000`) but not sample 1 (white,
  `key=f8fcf8`, LVGL's plain-shift expansion of `0xffff`; a `key=ffffff`
  reading would be a documentation error). The keyed pixel was
  converted/written (blue `0xff`) instead of retaining the background. The
  probe stopped, so the remaining samples were not evaluated, and
  `lv_aic_ge2d_test_run()` aborted before the `ge2d accepted/engine/sw/...`
  counters, refresh timing, YUV probes and video-window test. The diagnostic
  does not enable production RGB565 color key; the pixel-mismatch path frees
  its CMA (retain-until-reboot applies to DMA failure only).
- Fix pinned with component `0f8c15a`: `key565_probe()` now records the
  engine's own unkeyed conversion per sample (`base=`/`class=`) and tries five
  key encodings, accepting only one common encoding across all nine samples;
  an inconclusive measurement no longer stops the remaining samples or the
  later GE2D blocks (`lv_aic_ge2d_fill_test_run()` returns 1 and the suite
  reports the failure at the end). Board re-run with the pinned image: see
  "Board run 2" below - key565 PASS 9/9 with the engine encoding confirmed.
- Verified build for that pin (`-Phase ge2d -Jobs 8 -WithFonts -WithWidgets
  -WithAicp -WithPlayer -WithApng -EvidenceTag key565-measure-final`): boot,
  app config/build, static checks, image checks and manifest **PASS** with a
  clean tree. Image SHA256
  `5CF251CFD3AE923045B903B239EAB38FD007BE8D09012FA48FAB7CECF68580FD`, ELF
  SHA256 `B7B76112CF43E99612AF6DEFC041278B3826BDD3A4A2A1DF30CB818BD44BB6D8`,
  boot identity `sdk=codex/port-lvgl-9.6@d4c631ef
  lvgl-aic=codex/sdk-basic-capabilities@0f8c15a8 lvgl=9.6.0@80ca777e` (no
  `+dirty`; this docs commit only follows it). Evidence:
  `output/lvgl-evidence/ge2d-fonts-widgets-aicp-player-apng-key565-measure-final`.
- No panel/visual, touch-interaction, `lv_aic_capture`, APNG/plane/GIF shell or
  paired GE timing evidence was captured in this log.
- `E/DFS: mount fs[elm] on /sdcard failed` is a storage/environment note,
  unrelated to LVGL.

Serial boot/self-test evidence only; it does not substitute for panel photos,
capture dumps or touch confirmation.

## Board run 2 (2026-10-06): key565 PASS, native-fill alpha open

The `key565-measure-final` image (SHA-256
`5CF251CFD3AE923045B903B239EAB38FD007BE8D09012FA48FAB7CECF68580FD`) was
flashed; the raw log is archived at
`output/lvgl-evidence/board-2026-10-06-key565-measure-native-fill/serial.log`
(SHA-256 `FE71EC491FAED54E79E2971853ACAB8F1C63B39A587C81840D56FC721A958DA2`).

Resolved in this boot:

- **key565 PASS 9/9**: every sample matched the engine's own RGB565 to
  ARGB8888 conversion, `common encoding=engine mask=03`. The engine expands
  with replication (`(c<<3)|(c>>2)`); the earlier mismatch was LVGL's
  plain-shift key, not the comparator. Production RGB565 color key stays
  disabled pending that policy decision.
- Unchanged PASS: RT-Thread event self-test, three lifecycle cycles, font
  probes, MPP fixtures, resource stage with 1000-cycle CMA balance
  (`CMA current=0 peak=61440 alloc=1000 free=1000`), 12 solid + 84 ARGB +
  fake/video-window fill probes with `guards=OK`.

Open in this boot:

- **Native fill, first execution: `FAIL gradient opaque-background alpha`**
  (`fmt=0 dir=1 blend=1`). The two blend-disabled ARGB8888 probes that ran
  kept destination alpha 255; the first blended probe returned a non-255
  destination alpha and stopped the suite, so the 240 solid-YUV, 32
  YUV-gradient cases and every later GE2D counter/refresh/video block were
  not evaluated. The probe is now a soft measurement (component `cf0e54e`)
  that reports `min/max/bad` and continues; the next flash characterizes the
  engine's blended destination alpha.
- **Banner truncation**: the combined `build:` line stopped after
  `lvgl-aic=...@0f8c15a8`; the ulog 128-byte line buffer (timestamp,
  level/tag and colour codes included) keeps 115 visible characters. SDK
  `92685615` now logs one identity per line.
- Panel, touch, capture: **NOT_RUN**.

Next candidate built with the diagnostic and split banner: `-Phase ge2d
-Jobs 8 -WithFonts -WithWidgets -WithAicp -WithPlayer -WithApng -EvidenceTag
native-alpha-probe`, SDK `7cb27ad7`, lvgl-aic `cf0e54ee`, LVGL `80ca777e`,
image SHA-256
`697AD25C9952E7FE8643CE6ACB26ABE776F2E962F2C26FF19B8DE4C3B24E0B12`, ELF
SHA-256 `6B3D79BD5CDD1AE46AA4102C2C98A4200DA61839D6779B14426E5013D8E593AC`;
boot/app/static/image/manifest gates PASS. Expected banner on the next flash:

    build: sdk=codex/port-lvgl-9.6@7cb27ad7
    build: lvgl-aic=codex/sdk-basic-capabilities@cf0e54ee
    build: lvgl=9.6.0@80ca777e

## Board run 3 (2026-10-06): blended alpha characterized; GE v1.1 fillrect admission

The `native-alpha-probe` image (SHA-256
`697AD25C9952E7FE8643CE6ACB26ABE776F2E962F2C26FF19B8DE4C3B24E0B12`) was
flashed; the raw log is archived at
`output/lvgl-evidence/board-2026-10-06-native-alpha-probe/serial.log`
(SHA-256 `115AA9CAC39C8EECB13EB2014B0EAA74735F6950345E9629F43D04930F054BF0`).
The returned slice starts at 5.846 s, so the three `build:` identity lines are
not in it; the soft `dst-alpha` lines prove this exact candidate ran.

Measured in this boot:

- **Blended ARGB8888 destination alpha is now characterized.** With
  destination alpha 255 the GE v1.1 blend unit applies its straight-alpha OVER
  result to the alpha channel itself:
  `A_out = (A*A + 255*(255-A) + 127)/255`. The board matched exactly:
  constant A=128 -> 191 in all four direction/reverse cases, and the 32..224
  ramp -> min 192 at mid-ramp and max 228, again for all four cases. The probe
  now hard-checks that formula with tolerance 1 (component `b493356`);
  blend-disabled probes keep requiring 255.
- **key565: PASS 9/9** reconfirmed, `common encoding=engine mask=03`.
- **YUV abort root cause**: the first YUV submission (`fmt=32`, YUV420P)
  printed the SDK gate `fill rectangle not support yuv format, except yuv400`
  (`packages/artinchip/mpp/ge/cmdq_ops.c`, `ge_fillrect()` under
  `AIC_GE_DRV_V11`) and our helper treated the rejection as a DMA fault and
  quarantined. `lv_ge_fill` now mirrors the RGB/YUV400-only admission locally:
  other layouts return INVALID with no submission and no quarantine. The
  runner asserts 236 such rejections with untouched buffers and submits only
  YUV400 (20 solid + 16 gradient).
- Everything before the stop passed: RT-Thread event self-test, three
  lifecycle cycles, font probes, MPP fixtures with 1000-cycle CMA balance
  (`CMA current=0 peak=61440 alloc=1000 free=1000`), 12 solid-fill, 84 ARGB
  solid/fake fill, fake/video-window, 12 key565 and 12 RGB gradient probes.
- NOT_EVALUATED in this boot because the quarantine stopped the suite: GE2D
  counters/refresh, the YUV blocks (now only the YUV400 subset), the
  video-window block, `lv_aic_capture`, panel/visual and touch.

Fix and next candidate:

- Component commits `6d81c62` (admission, hard alpha check, host contracts)
  and `b493356` (docs); SDK pin `17ed72f2`. Host vector profile **93/93** and
  GE profile **71/71** PASS, including the reworked `native_fill_probe` and
  `canvas_image` contracts.
- Verified image (`-Phase ge2d -Jobs 8 -WithFonts -WithWidgets -WithAicp
  -WithPlayer -WithApng -EvidenceTag fill-admission`): image SHA-256
  `5E92F14BDD81B47676138212785E441A8E792D93878BD2E4703387C54B1238B8`, ELF
  SHA-256 `374348510E201CB392EAC1E8B5D8B12BFA6FE77732853BA9762F3243ECD74BDD`;
  boot/app/static/image/manifest gates PASS with clean sources and empty
  source patches. Evidence:
  `output/lvgl-evidence/ge2d-fonts-widgets-aicp-player-apng-fill-admission`.
  Expected banner on the next flash:

      build: sdk=codex/port-lvgl-9.6@17ed72f2
      build: lvgl-aic=codex/sdk-basic-capabilities@b4933569
      build: lvgl=9.6.0@80ca777e

- Expected new probe lines: `PASS YUV400 20 CSC solid probes guards=OK`,
  eleven `SKIP YUV fmt=...` lines, `SKIP YUV444P 16 CSC gradient probes; GE
  v1.1 fillrect admits RGB/YUV400 only`, then `PASS 36 RGB gradient, 20 YUV400
  solid, 16 YUV400 gradient; 236 other-YUV probes rejected locally`, followed
  by the GE2D counter/refresh and video-window blocks this boot did not
  reach. A blended destination-alpha mismatch would now abort before those
  blocks instead of reporting softly.

## Board run 4 (2026-10-06): fill-admission executed - RGB hard alpha PASS, YUV400 CSC open

The `fill-admission` image was flashed; the captured log is archived at
`output/lvgl-evidence/board-2026-10-06-fill-admission/serial.log`, SHA-256
`661740b0304c69e6c2e1e09f36d267e0d041b854b7fecaed06dae13361c5f4a4`.

Confirmed on hardware this boot:

- **Blended-fill destination-alpha hard check PASS.** 24 non-alpha RGB
  gradient probes (ARGB8888/RGB888 error 0, RGB565 error 3-4) and 12
  alpha-gradient probes (ARGB8888 error 1; RGB565 error 7-8, inside the 9/255
  5-bit tolerance); 84 ARGB solid/fake fills and nine key565 samples passed
  again. The `A_out = (A*A + 255*(255-A) + 127)/255` formula now has on-board
  hard-check evidence.
- **GE v1.1 fillrect admission PASS.** Eleven `SKIP YUV fmt=32/35/43/33/34/
  36/37/38/39/40/41 20 CSC solid probes` lines, no SDK `fill rectangle not
  support yuv format` print and no quarantine; the suite reached the YUV400
  probes, which the previous image never did.
- **First YUV400 CSC submission mismatch (open).** `FAIL YUV pixel fmt=42
  space=0 color=0 error=16`: BT.601-limited black is expected Y=16 from the
  SDK `rgb2yuv_bt601` table (`{66,129,25,16}`); a delta of exactly 16 means
  the engine wrote 0 or 32, and the offset column rather than the matrix is
  in question. This log cannot distinguish those cases and the responsible
  stage is not yet proven.
- NOT_EVALUATED because the mismatch stopped the native-fill runner: the
  remaining YUV400 solids/gradients, GE2D counters/refresh and the
  video-window block. Panel/touch/capture remain NOT_RUN.

Diagnostic follow-up (`676b970`, SDK pin `d2a6aadc`; docs `70aabf6`): the
failure line now prints `got`, `want`, the active-pixel `range` and `offset`,
and all 20 YUV400 solids run before the summary decides, so one boot yields
the full 4-space x 5-color observed table. New candidate
`-EvidenceTag yuv400-csc`: image SHA-256
`F1FEF3B6EB672AA2720A9B1099A479BAB195FFF9C9ED194ADD84F6C98B09B9DD`, ELF
SHA-256 `E7BA94B25791EE80AC1A9EF2BB8F8CD1E3B96BB75C775949048BC7A7B7B24238`;
boot/app/static/image/manifest gates PASS with clean sources. Expected banner
on this flash:

    build: sdk=codex/port-lvgl-9.6@d2a6aadc
    build: lvgl-aic=codex/sdk-basic-capabilities@676b9708
    build: lvgl=9.6.0@80ca777e

The next log should contain either `PASS YUV400 20 CSC solid probes
guards=OK` or up to 20 `FAIL YUV pixel ... got=... want=... range=...
offset=...` lines plus `FAIL YUV400 solids N of 20 probes mismatch`, which
will pin the YUV400 CSC model for the corrective change.

## Board run 5 (2026-10-06): YUV400 fill resolved to a raw red-byte path

The `yuv400-csc` image was flashed; the pasted serial slice (from 9.672 s,
not including the banner) is archived at
`output/lvgl-evidence/board-2026-10-06-yuv400-csc/serial.log`, SHA-256
`89e5035bd86dc7c28e4dec6e286f9d63e2548c8610ed969d7d9d1ef373dfa765`.

- **All 20 YUV400 solids executed; 16 failed.** Every failing probe wrote the
  raw red byte of the ARGB8888 fill color in all four SDK color spaces
  (`got == R`): BT.601-limited red got 255 against CSC2 Y=82, green got 0
  against 144, blue got 0 against 41, white got 255 against 235. Black and
  white pass in the two full-range spaces only because the red byte equals
  their luma there (16 of 20). The previous `error=16` offset-column
  hypothesis is retired: GE v1.1 bypasses the configured CSC2 output stage
  on this fillrect path.
- RGB gradients, the destination-alpha hard check and the eleven `SKIP YUV
  fmt=...` admission lines stayed green.
- NOT_EVALUATED: YUV400 gradients, the GE2D counter/refresh and the
  video-window blocks (the runner stopped after the solids summary);
  panel/touch/capture remain NOT_RUN.

Corrective revision (component `2f6b37e`, docs `f96d0ec`): solids and
gradients model the measured raw red byte, the FAIL lines fit the 128-byte
ulog buffer, and the summary wording reports raw red bytes. Host contracts:
vector 93/93, GE 76/76. Expected next log: `PASS YUV400 20 raw red-byte
solid probes guards=OK`, `SKIP YUV444P 16 CSC gradient probes; GE v1.1
fillrect admits RGB/YUV400 only`, sixteen `PASS YUV gradient ...` lines,
`PASS 36 RGB, 20 YUV400 raw-R solid, 16 raw-R gradient; 236 other-YUV
rejected`, then the counter/refresh and video-window blocks.

Rebuilt candidate `-EvidenceTag yuv400-raw`
(`output/lvgl-evidence/ge2d-fonts-widgets-aicp-player-apng-yuv400-raw`):
image SHA-256
`0b64c97210004ba600a006c827c6d5a1014c75df00a6c2bf2ad1c6dd0b8e1a46`, ELF
SHA-256 `e2f6718c5ef51379545222dc222846fd817c4a4c47c510b93802f8aaefc77d94`;
boot/app/static/image/manifest gates PASS from clean pinned sources (SDK
`94397e72`, component `f96d0ec5`). Banner:

    build: sdk=codex/port-lvgl-9.6@94397e72
    build: lvgl-aic=codex/sdk-basic-capabilities@f96d0ec5
    build: lvgl=9.6.0@80ca777e

## Board run 6 (2026-10-06): YUV400 raw-R model board-verified; GE v1.1 bitblt admission

The `yuv400-raw` image was flashed; the pasted serial slice (from 9.330 s,
not including the banner) is archived at
`output/lvgl-evidence/board-2026-10-06-yuv400-raw/serial.log`, SHA-256
`2778e45572ceb541438a5f48f871e97be302d1052d014e1448d296a55d803b92`.

- **The raw-R fill model is board-verified.** `PASS YUV400 20 raw red-byte
  solid probes guards=OK`; all 16 `PASS YUV gradient fmt=42 ... error=0`
  lines over four color spaces and both directions; summary `PASS 36 RGB,
  20 YUV400 raw-R solid, 16 raw-R gradient; 236 other-YUV rejected`.
  Key565 9/9 and the RGB/destination-alpha hard checks stayed green.
- CPU YUV matrices (4 spaces, odd-width I420) and the YUV image decoder
  passed (`PASS CPU YUV matrices=4 ...`, `PASS YUV image decoder pixels and
  deferred producer release`).
- **New open item: GE v1.1 bitblt admission.** The first non-YUV400 image
  submission printed the SDK `bitblt not support yuv format, except src
  format yuv400` line, and the port quarantined with `FAIL YUV DMA` plus
  `FAIL YUV frame/CPU conversion contract`. The SDK gate admits
  `is_rgb_or_yuv400(src) && is_rgb(dst)` only.
- NOT_EVALUATED in this slice: YUV400 ARGB target, I420/packed GE probes,
  YUV stripes, GE2D counter/refresh and video-window blocks. Panel/touch/
  capture NOT_RUN.

Corrective revision (component `2adbc6a`): the YUV draw path and the video
plane decline non-admitted layouts before submission; the executor decodes
to RGB888 for an admitted GE blit (software when that declines); the manual
runner reports `SKIP YUV ARGB ...` for the declined probes and accepts
either fallback outcome with unchanged pixel oracles. Host: vector 95/95,
GE 78/78.

Expected next log: eleven `SKIP YUV ARGB fmt=...` lines, one or two
`GE v1.1 declined; ...` lines, no SDK printf, no `FAIL YUV DMA` / `FAIL YUV
frame/CPU conversion contract`.

Rebuilt candidate `-EvidenceTag yuv-bitblt-admission`
(`output/lvgl-evidence/ge2d-fonts-widgets-aicp-player-apng-yuv-bitblt-admission`):
image SHA-256
`6f3aab96a4908bdaa63f0d8cd7923fe097d8ea21f3f1067cef5d91ced89e296b`, ELF
SHA-256 `a2a67300a80b53bc85b199d5ea5e039738bc35e7ae7154df9082e21182c06e6a`;
boot/app/static/image/manifest gates PASS from clean pinned sources (SDK
`01267057`, component `8b02b02a`). Banner:

    build: sdk=codex/port-lvgl-9.6@01267057
    build: lvgl-aic=codex/sdk-basic-capabilities@8b02b02a
    build: lvgl=9.6.0@80ca777e

Board run 7 flashed this candidate: the decoded-RGB fallback is
board-verified on the engine (four rotation probes `max_error=0`, seven scale
probes `<=1`, clipped 2x2 tiles `0`, `tile rot=0` `1`, eleven `SKIP YUV ARGB
...` lines, no SDK printf, no quarantine). The GE-declined `I420 tile rot=90`
cell is the first probe drawn by the software fallback and failed
`FAIL I420 tile rot=90 pixels=1920 max_error=93`; a host reproduction of the
same geometry draws correctly (worst 3 without, 5 with antialiasing), so the
93 is device-only. Component diagnostics `047baa1` report the worst pixel
with `over3`/`aa`, re-run once with `aa=0` and print whether the software
re-decode opens, so the next boot can separate a paint miss from filtering.
Full log: `output/lvgl-evidence/board-2026-10-06-yuv-bitblt-admission/serial.log`.

Rebuilt candidate `-EvidenceTag yuv-tile-rot90-diag`
(`output/lvgl-evidence/ge2d-fonts-widgets-aicp-player-apng-yuv-tile-rot90-diag`):
image SHA-256
`d2a9a8a6198bf4c69925be4e12521bc5e05e58c6d89976ef9f1d5cd845df6b8d`, ELF
SHA-256 `6cbfda6540bc2f28bc1309e22845448acbab124c80087c2e4e142434bbe463b7`;
boot/app/static/image/manifest gates PASS from clean pinned sources (SDK
`55bc6d1c`, component `1f73289c`) with no new compile warnings from
`lv_aic_yuv_test.c`. Banner:

    build: sdk=codex/port-lvgl-9.6@55bc6d1c
    build: lvgl-aic=codex/sdk-basic-capabilities@1f73289c
    build: lvgl=9.6.0@80ca777e

Expected next log: `FAIL I420 tile rot=90 ... over3=... xy=...,... got=...
want=... aa=...`, then `DIAG I420 tile sw open=... cf=... stride=...` and
`FAIL I420 tile rot=90 aa=0 ...`, before `FAIL GE I420 probe`.

## Board run 8 (2026-10-06): I420 tile rot=90 was the probe readback, not the fallback

The `yuv-tile-rot90-diag` image was flashed; the pasted serial slice (from
10.051 s, not including the banner) is archived at
`output/lvgl-evidence/board-2026-10-06-yuv-tile-rot90-diag/serial.log`, SHA-256
`710b4b206e3797aafda6d19e9c1c8ce0c2d2276b0b50f926ba99637a4166ac8b`.

The diagnostics localized the failure. Every probe before the GE-declined
tile cell still passes (four rotations `max_error=0`, seven scales `<=1`,
clipped 2x2 tiles `0`, `tile rot=0` `1`); the worst tile pixel reports
`got=165` (the `0xa5` pre-fill) with `max_error=93`, `over3=4998` below the
5760 channel comparisons; and the `aa=0` re-run reads the same while the
software re-decode opens cleanly (`DIAG I420 tile sw open=1 cf=15
stride=96`). That is a cache-coherency signature, not a paint miss: the
decoded-RGB fallback draws through the CPU cache, and the probe read the CMA
target back with invalidate-only, which discards the dirty lines the CPU just
wrote. The engine cells never show it because DMA leaves memory coherent,
and the display path already flushed with clean+invalidate before the DPU.

Corrective revision (component `309f0da`): every fallback readback flushes the
target with clean+invalidate first, and the two tile cells carry the
host-measured software tolerance (6, against the engine's 3). The host probe
of the same geometry measures worst 5 with antialiasing, 3 without.
NOT_EVALUATED in this slice: YUV stripes, GE2D counter/refresh and
video-window blocks (the runner stopped at the tile cell in both boots).
Panel/touch/capture NOT_RUN.

Rebuilt candidate `-EvidenceTag yuv-tile-clean`
(`output/lvgl-evidence/ge2d-fonts-widgets-aicp-player-apng-yuv-tile-clean`):
image SHA-256
`7ec543b9be84ef07f2e47d2e1f24c50c6f8642faa05f3dd00702a427c00460b5`, ELF
SHA-256 `85bd0e32ff4d14fea8567c91770b69eb5cadf256e145f0c6ce285b582e36d79b`;
boot/app/static/image/manifest gates PASS from clean pinned sources (SDK
`685eba25`, component `309f0da3`) with `lv_aic_yuv_test.c` recompiled and no
new warnings. Banner:

    build: sdk=codex/port-lvgl-9.6@685eba25
    build: lvgl-aic=codex/sdk-basic-capabilities@309f0da3
    build: lvgl=9.6.0@80ca777e

Expected next log: `PASS I420 tile rot=90 pixels=1920 max_error=5` (or
below), no `DIAG`/`aa=0` lines, then the yuv probe continues into the
stripes, packed formats and the GE2D counter/refresh and video-window
blocks.

## Board run 9 (2026-10-07): tile-rot90 closed; the v1.1 staging alpha lane exposed

The `yuv-tile-clean` image was flashed; the pasted serial slice (from 4.048 s,
not including the banner) is archived at
`output/lvgl-evidence/board-2026-10-07-yuv-argb/serial.log`, SHA-256
`3D7B810BC351B1069E7D7B18D07C1A764B7C8F396944B789609825105140CD04`.

- **Board run 8's open item is closed.** `PASS I420 tile rot=90 pixels=1920
  max_error=3 guards=OK` with no `DIAG`/`aa=0` line: the
  clean+invalidate readback and the host-measured software tolerance hold on
  hardware. Every GE I420 rotation/scale/tile probe stayed green, as did the
  thirty-two stripe lines (`error<=4`, whole and split refresh) and the
  eight packed-format lines (`error=0`).
- **New open item: the v1.1 bitblt staging alpha lane.** After three
  `SKIP YUV ARGB fmt=32/33/34 ...` lines the YUV block printed
  `FAIL YUV ARGB fmt=3 rot=0 error=39`. Probe index 3 is I400, the only
  layout GE v1.1 admits; the uniform Y=100 frame left alpha 100 in the
  private staging surface and the CPU tail mixed it with opa 64 to 25 where
  the native fill oracle keeps 64 (64-25=39). `ge_bitblt()` builds its
  blend command with `en_alpha_out_oxff=0`, and YUV has no source alpha to
  pass through. The runner stopped at `FAIL YUV ARGB target probes` /
  `FAIL YUV frame/CPU conversion contract`; the remaining eight SKIP
  lines and the later mask/color-key, counter/refresh and video-window
  blocks are NOT_EVALUATED. Panel/touch/capture NOT_RUN.

Corrective revision (component `9892cea`): every engine-written crop of the
opaque staging surface is invalidated, re-marked 0xff and cleaned back before
the CPU tail; unwritten tile/rotation gaps keep zero alpha and stay skipped.
This is a component-owned normalization; SDK behavior is unchanged. Host GE
**73/73 PASS** and the no-GE2D baseline **35/35 PASS**; disabling the
normalization turns the model red (`ARGB YUV f=0 angle=0 opa=64 xy=54,44
c=3 error=54`).

Rebuilt candidate `-EvidenceTag yuv-argb-alpha` (details added after the
clean pinned build below).

## Firmware checks

| Profile | Build / link-map / image checks | Image bytes | SHA-256 |
| --- | --- | ---: | --- |
| gate1 | PASS | 1550848 | `800adf51434dcffb3009baf5aa53d0d6d2c44add1f4d1e5b84d587460825a671` |
| mpp | PASS | 1790464 | `1b23cfbe88412a2dc208e603a36e4e436b63ad8b31b762de1e6e23a8e3b523bd` |
| ge2d | PASS | 1821184 | `b6b37c6d66e3047acdde9706b9d38c3b587e692bc02d06459a9b0713951cea0f` |

Each image passed the bootloader RISC-V ABI check and nine payload CRC checks.
MPP and GE2D also passed 26 packaged fixture/provenance hash checks.
Link-map gates verify application third-party symbol ownership and reject the
legacy SDK LVGL tree. All three generated headers contain the application-owned
32768-byte stack, priority 20, three cycles and five timer iterations.

Evidence is local to this worktree under `output/lvgl-evidence/{gate1,mpp,ge2d}`.
Each manifest records the build-time HEAD, source patch and untracked-source
archive; the builds preceded the final SDK commit. Generated images and logs
are intentionally not committed. Commands are in [README.md](README.md).

## Host contracts

CMake Debug / Ninja with MSYS2 UCRT64 GCC 16.1.0: **6/6 passed**.
Build and CTest logs are under `output/lvgl-host-app/`.

- `lvgl_aic_os_contract`
- `lvgl_aic_platform_smoke`
- `lvgl_aic_manual_pages`
- `lvgl_aic_ge2d_scale_contract`
- `lvgl_aic_mpp_contract`
- `lvgl_aic_disabled_features`

## Remaining validation and known diagnostics

- Board boot validation ran on 2026-10-06 (see above). The RGB565 color-key
  diagnostic is resolved (9/9 PASS), the blended-fill destination alpha is
  characterized (Board run 3), the hard alpha check plus the GE v1.1
  fillrect admission are board-verified (Board run 4), Board run 5 resolved
  the YUV400 expected-value model as the measured raw red byte, Board run 6
  board-verified the raw-R solids and gradients and exposed the GE v1.1
  bitblt admission fixed in component `2adbc6a`, and Board run 7 verified the
  decoded-RGB fallback on the engine (rotation/scale/tiles PASS) while
  exposing the device-only `I420 tile rot=90` software-fallback mismatch
  (component diagnostics `047baa1`), and Board run 8 localized that mismatch
  to the probe's invalidate-only readback of the CPU-drawn fallback (component
  fix `309f0da`). Board run 9 verified the `yuv-tile-clean` readback fix
  (`PASS I420 tile rot=90 max_error=3`), ran the stripes and packed blocks
  green and exposed the GE v1.1 staging alpha lane on the first admitted I400
  ARGB probe (component fix `9892cea`). Still open: the re-flash of the
  `yuv-argb-alpha` candidate, the GE2D counter/refresh and video-window
  blocks, `lv_aic_capture` dumps, panel/visual and touch confirmation, and
  the APNG/plane/GIF shell gates.
- Event binary-sync and lifecycle startup checks passed on board; actual
  interrupt-context notifications remain a separate check.
- Display/touch and GE2D rendering/timing still need on-board inspection via the
  component gates; decoder output and CMA lifecycle now have board evidence.
- Upstream `lvgl-aic/tests/manual/lv_aic_mpp_test.c` uses `rt_uint32_t` locals for `rt_memory_info`, which expects `rt_size_t *`. MPP and GE2D builds report six incompatible-pointer warnings. These are retained in the logs and should be corrected in lvgl-aic upstream, then consumed through a new pin; SDK headers must not be changed to accommodate them.
- The bundled SDK environment also reports the existing pywin32 and compiler preflight warnings. Build success is not a warning-free claim.

The active product checkout remained on `codex/hangcha-zc-202620085`, clean,
with `forklift-meter-platform` still selected. No product source or active
configuration was edited by this work.
