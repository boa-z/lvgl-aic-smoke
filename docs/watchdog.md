# Bench recovery watchdog

`watchdog/lv_aic_watchdog.{c,h}`, Kconfig `AIC_LVGL_SMOKE_WATCHDOG` (default y
with `AIC_USING_WDT`), started from `main()`. It is a bench tool of this
application: a test that locks the board up resets it instead of hanging the
bench. It is not part of lvgl-aic, and a product supervises its own hardware
watchdog (forklift-meter-platform: `platform/rtthread/meter_watchdog_port.c`).

A priority-29 thread feeds the `wdt` device every 3 s with a 10 s timeout. Any
lockup that stops the scheduler (CPU exception, hard loop with interrupts off)
resets the board within about 10 s. Long legitimate work (GE2D suites, CAN
streaming) yields the scheduler and keeps feeding. A task-level deadlock with
the scheduler alive keeps feeding too and is **not** caught.

## Shell

| Command | Effect |
|---|---|
| `lv_aic_watchdog status` | running flag and the driver's time left |
| `lv_aic_watchdog regs` | raw registers: control, counter (sampled 1 s apart; 32000 ticks = counting), thresholds |
| `lv_aic_watchdog stop` | stops feeding **and disarms the hardware**; use before a debugger halt |
| `lv_aic_watchdog start` | arms it again |
| `lv_aic_watchdog hang` | self-test: disables interrupts and spins; the board must reset within about 10 s. Refuses to run when the watchdog is not running |

## Why the first version never worked

The version that lived in lvgl-aic called `rt_device_find("wdt")` and then
`rt_device_control(SET_TIMEOUT / START)` but never `rt_device_init()`. The SDK's
`drv_wdt_init()` enables the module clock and releases the block from reset;
without it, writes to the control register are ignored. Symptom on the board:
the thresholds were programmed (`rst_thd=10s`), `START` returned success and
`status` said `running=1`, yet `ctl=0`, the counter did not move, and a
deliberate lockup (`hang`) left the board dead until a power cycle. The
product's own port calls `rt_device_init()` first and is correct. Earlier notes
that this watchdog "turned hangs into 10 s reboots" are not supported by that
evidence.

Fixed here: `start` initializes the device first, and the feeder thread stops
the hardware when it exits (before, `stop` only ended the thread, which would
have reset the board 10 s later once the hardware really ran).

## Board evidence (2026-10-08)

Image SHA-256 `BE7D8683B393F1D822966A34578AE2551F525F4B950319FC2ED4804F048DBD7B`. All on D50T-2-Lite:

- `regs` while feeding: `ctl=0x1`, counter +31972 ticks in 1 s, `rst_thd=10s`;
  `status` `timeleft` moves between 8 and 10 s with the 3 s feed.
- `stop`: no reset in the 16 s that followed, `ctl=0`, counter frozen;
  `start`: `ctl=1`, counting again.
- `hang`: the board reset by itself, booted (`Start-up from os`), presented its
  first frame, started the CAN OTA endpoint, and `main()` re-armed the
  watchdog (`running=1`).
- With the unfixed code the same `hang` froze the board (UART and CAN dead)
  until power was cycled.

Evidence: SDK `output/lvgl-evidence/board-2026-10-08-smoke-watchdog/`.
