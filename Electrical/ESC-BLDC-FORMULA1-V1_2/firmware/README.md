# ESC bring-up firmware (SimpleFOC)

## Setup (once)
1. Install **VS Code** and the **PlatformIO IDE** extension.
2. Install the **ST-LINK USB driver** (STSW-LINK009 from st.com) so Windows sees the Nucleo.
3. Open this `firmware` folder in VS Code (File → Open Folder). PlatformIO downloads the STM32
   toolchain and SimpleFOC by itself the first time (a few minutes).
4. Nucleo: jumper **JP5 on E5V**, plug it onto the ESC board, USB cable to the PC.

## Step 0: first power-up, no Nucleo, no motor (no firmware needed)
1. Look the board over: solder bridges on U1 (DRV8353, QFN), U4, the FETs; all parts there, polarised parts
   the right way round (D8, the two 330 µF caps).
2. Multimeter, supply **off**: no short from BAT+ (fuse side) to GND, +5 V (J9 pin 1) to GND, and each phase pad
   to GND and to BAT+. Readings that start low and climb are just the capacitors charging; that is fine.
3. Bench supply **24 V, current limit 0.1 A**, fuse in F1, Nucleo **not** plugged in. Switch on and watch the
   current: it should settle at a few mA to a few tens of mA. If it sits at the limit, switch off.
4. Measure (black probe on J9 pin 4 = GND):
   - **5.0 V** on J9 pin 1 (LM5164, for the Nucleo)
   - **12.1 V** across C24 (DRV8353 buck = gate-drive supply)
5. Optional: turn the supply down slowly. 5 V should drop out at about 16.8 V and come back at about 18 V.
6. Switch off, set Nucleo jumper **JP5 to E5V**, plug the Nucleo in (USB towards the left board edge),
   switch on again: the green LED D3 (3.3 V) lights. Then plug in USB and go to step 1.

## Steps
Pick the environment at the bottom of VS Code (or in the PlatformIO sidebar), click **Upload and
Monitor**. Do them in order.

| Env | Motor | Supply | What you should see |
|---|---|---|---|
| `1_drv_check` | not needed | bench supply **20–24 V**, current limit 0.3 A | all settings `ok`, "SPI OK", no faults, plausible bus voltage, FET temperature, motor sensor resistance + type guess |
| `2_hall_test` | connected, turn by hand | same | states 1–6 only, never 0 or 7; one wheel turn → pole pairs |
| `3_open_loop` | connected | 20–24 V, limit ~1 A | motor turns slowly and smoothly. `T5` = faster, `T-5` = reverse, `T0` stop |
| `4_closed_loop` | connected | same | alignment wiggle at start, then `T0.1` … `T0.5` (volts, capped at `VOLTAGE_LIMIT`) gives torque |
| `5_foc_current` | connected | same, later the battery | first `MFP0.4` (measures R and L), then `MFC200` (sets the current PI); then `T0.5` … `T2` (amps) gives torque, `Iq` in the status line follows the target |

**Supply voltage:** the 5 V regulator (LM5164) only switches on above **18 V** (EN/UVLO divider R63/R64,
off again below 16.8 V). Below that the Nucleo stays dark, so always bring up at 20–24 V.

Before step 3: put the real pole pair count from step 2 into `POLE_PAIRS` in `include/board.h`.

## Throttle (step 5)
Throttle on J7 (1 = 3.3 V, 2 = signal, 3 = GND). It sets the motor current: 0–100 % → 0 … `L` amps (default
`CURRENT_LIMIT` 5 A). It is on after start-up (`THROTTLE_ON_AT_START`) but only acts after it has been released
once ("armed"); the current rises at most 10 A/s and drops at once when released. Commands: `A` status,
`AI` idle = now (released), `AF` full = now (pressed fully), `A0` off (T commands work again), `A1` on.
Put the calibrated volts into `THROTTLE_IDLE_V` / `THROTTLE_FULL_V` in `include/board.h`.

## Bus protection (steps 3–5)
A torque against the rotation makes the motor a generator; a bench supply cannot take that energy back and the
bus capacitors would charge far above 60 V. So: a target against the rotation (above 3 rad/s) becomes 0 (coast),
`MLU` can't be lowered while spinning, the bus is checked every 1 ms and the driver switches off above
`BUS_OV_LIMIT` (40 V for the bench; raise it for the battery). The PWM follows the measured bus voltage.

## Battery (12S Li-ion): environment `5_foc_battery`
Same as step 5, built with `-D BATTERY_CELLS=12`: refuses to start below 36.0 V (3.0 V/cell) or above 51.6 V,
over-voltage stop at 55.4 V, current derated linearly from 38.4 V (3.2 V/cell, full) to 36.0 V (0 = coast).
The bench build (`5_foc_current`) refuses to start above 35 V, so it can't run on the pack by mistake.
Before the first connection: polarity checked with a multimeter (the board has **no reverse-polarity
protection**), anti-spark plug (XT90-S) for the 660 µF inrush, a small fuse in F1 for the first tests.

## Temperatures
- **FET temperature** (NTC TH1 next to Q4) is always active.
- **Motor temperature** (white wire, J8 pin 6): the sensor type is not known yet. Step 1 prints its resistance
  and a guess (at room temperature: ~10 kΩ = NTC 10k, ~600 Ω = KTY84, ~1.1 kΩ = PT1000). Set
  `MOTOR_TEMP_SENSOR` in `include/board.h` accordingly (for an NTC also check `MOTOR_NTC_BETA`, usually 3950).
- Steps 3–5 print a `# FET … | motor … | Vbus … | power …%` line every second. Above `FET_TEMP_WARN` /
  `MOTOR_TEMP_WARN` the voltage/current limit is reduced linearly; at `*_MAX` the motor stops.
- All slow analog values (temperatures, throttle, bus voltage) are read through `include/analog_in.h`.
  **Never use `analogRead()` once the motor runs**: the STM32 core resets ADC1 and ADC2 after every
  `analogRead()`, which destroys SimpleFOC's current sensing.

## If something goes wrong
- **"SPI FAILED"** in step 1: supply below 9 V, EN not high, or a wrong/missing Nucleo jumper.
- **DRV fault `VDS_…`**: overcurrent seen across a FET. Lower `VOLTAGE_LIMIT`, check for a short.
- **`GDF` / `VGS_…`**: a gate did not switch. Check the bootstrap capacitors and that phase wire.
- **initFOC fails / motor jerks in step 4**: wrong pole pairs, or swap two hall wires (or two phases).
- **Current reads the wrong sign in step 5**: SimpleFOC corrects this during initFOC; if it reports
  a problem, set `CSA_GAIN` to `-20.0f` in `board.h`.

Only raise the voltage towards 58 V once steps 3–5 run cleanly at low voltage.
