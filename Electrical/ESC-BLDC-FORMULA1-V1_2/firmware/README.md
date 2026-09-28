# ESC bring-up firmware (SimpleFOC)

## Setup (once)
1. Install **VS Code** and the **PlatformIO IDE** extension.
2. Install the **ST-LINK USB driver** (STSW-LINK009 from st.com) so Windows sees the Nucleo.
3. Open this `firmware` folder in VS Code (File → Open Folder). PlatformIO downloads the STM32
   toolchain and SimpleFOC by itself the first time (a few minutes).
4. Nucleo: jumper **JP5 on E5V**, plug it onto the ESC board, USB cable to the PC.

## Steps
Pick the environment at the bottom of VS Code (or in the PlatformIO sidebar), click **Upload and
Monitor**. Do them in order.

| Env | Motor | Supply | What you should see |
|---|---|---|---|
| `1_drv_check` | not needed | bench supply 12–24 V, current limit 0.3 A | all settings `ok`, "SPI OK", no faults, plausible bus voltage and temperature |
| `2_hall_test` | connected, turn by hand | same | states 1–6 only, never 0 or 7; one wheel turn → pole pairs |
| `3_open_loop` | connected | 12–24 V, limit ~1 A | motor turns slowly and smoothly. `T5` = faster, `T-5` = reverse, `T0` stop |
| `4_closed_loop` | connected | same | alignment wiggle at start, then `T0.5` … `T2` (volts) gives torque |
| `5_foc_current` | connected | same, later the battery | `T0.5` … `T2` (amps) gives torque; current follows the target |

Before step 3: put the real pole pair count from step 2 into `POLE_PAIRS` in `include/board.h`.

## If something goes wrong
- **"SPI FAILED"** in step 1: supply below 9 V, EN not high, or a wrong/missing Nucleo jumper.
- **DRV fault `VDS_…`**: overcurrent seen across a FET. Lower `VOLTAGE_LIMIT`, check for a short.
- **`GDF` / `VGS_…`**: a gate did not switch. Check the bootstrap capacitors and that phase wire.
- **initFOC fails / motor jerks in step 4**: wrong pole pairs, or swap two hall wires (or two phases).
- **Current reads the wrong sign in step 5**: SimpleFOC corrects this during initFOC; if it reports
  a problem, set `CSA_GAIN` to `-20.0f` in `board.h`.

Only raise the voltage towards 58 V once steps 3–5 run cleanly at low voltage.
