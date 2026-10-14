# ESC HAT (BLDC motor controller) – context for Claude Code

Read this first. `README.md` has the pin map and connector pinouts; `firmware/README.md` has the bring-up steps.

## Status (2026-10-14)
- **Board V1.2 is ordered** at JLCPCB with exactly the files in `jlcpcb/` (generated 2026-09-30 18:42):
  5 PCBs, 3 assembled (top side). 4 layers, 1.6 mm, FR4 TG155, ENIG, 2 oz outer / 1 oz inner,
  vias epoxy filled & capped, min via 0.2 mm, flying probe test, confirm production file + parts placement.
- JLC's production file was compared with ours: identical except etch compensation (traces +0.03 mm inner /
  +0.065 mm outer), non-functional inner-layer pads removed, plated holes drilled +0.15 mm, panel with 5 mm rails
  on the top/bottom edges (V-score exactly on the board edges), QR code + tooling holes on the rails.
- **Boards received 2026-10-14. Bring-up board #1 (bench supply 24 V):**
  - Step 0 ✓: 5 V = 4.977 V (J9.1), VBUCK = 12.072 V (L1); 5 V came on at ~16 V on the supply's display
    (calculated 18.0 V). With Nucleo (ST-LINK V3) ~0.1 A at 24 V. Green LED D3 on.
  - Step 1 ✓: all DRV8353 registers read back ok, no faults, Vbus reads 23.3 V (not yet checked against a
    multimeter), FET 22–23 °C, motor sensor open (motor not connected yet).
  - Next: step 2 (hall test) with the motor connected. Steps 2–5 untested on hardware.

## Folder
| Path | What |
|---|---|
| `bldc-75v.kicad_pro/_sch/_pcb` | KiCad 10 project (all libraries local via `${KIPRJMOD}`) |
| `jlcpcb/` | ordered gerber zip, BOM, CPL (+ unzipped gerbers) |
| `firmware/` | PlatformIO bring-up firmware (SimpleFOC) |
| `tools/` | JLC file generator + checks (run with KiCad's Python, see below) |

## Hardware facts not obvious from the schematic
- NUCLEO-G474RE sits **on top** of this board in 2×19 sockets (CN7/CN10, 8.5 mm); its USB faces the left board
  edge. Footprint verified 0.000 mm against SnapEDA's Nucleo-64 package; all 39 used pins verified against the
  official Nucleo-64 morpho table. **Nucleo jumper JP5 must be on E5V.** Default solder bridges: CN7_36 = PC1,
  CN7_38 = PC0 (that is what we use). Do not use a NUCLEO-G431RB (no FDCAN2) or F446RE (no PB11 = DRV chip select).
- Power path: battery → J5 → fuse F1 (ATO 30 A, 58 V) → VDRAIN (58 V bus). TVS D8 SMCJ60A, bulk 2× 330 µF.
  **No reverse-polarity protection** – use a keyed connector (XT90-S) and a current-limited bench supply first.
- 5 V: LM5164 (U4), 5.00 V / 303 kHz, ~1 A for Nucleo + halls + CAN + J9 aux. **EN/UVLO (R63 1M / R64 90.9k)
  turns it on at 18.0 V, off at 16.8 V → bench bring-up at 20–24 V, not 12 V.**
- DRV8353 integrated buck → VBUCK 12.1 V / 318 kHz → feeds only the DRV's VM pin (gate-drive supply).
  Both converters were re-verified against the TI datasheets on 2026-10-07.
- Current sense: 2 mΩ shunts (R14/R15/R16, Kelvin via net ties NT1–3), CSA gain 20 V/V, bidirectional around
  VREF/2 = 1.65 V → ±30 A ≈ 0.45…2.85 V. SOA/SOB/SOC on **PA4/PA1/PA0** (A and C crossed vs. pin order;
  firmware had them swapped until 2026-10-14), all ADC2.
- Temperatures: FET NTC TH1 (10k B3380, next to Q4) → 10k pull-up → **PA6**. Motor sensor (J8 pin 6, white wire) →
  2.2k pull-up, 1k series, 100 nF → **PA7**. Motor sensor type is unknown: at room temperature ~10 kΩ = NTC 10k,
  ~600 Ω = KTY84, ~1 kΩ = PT1000 (the 2.2k pull-up works for all three).
- J8 (JST-XH 6): 1 +5V, 2 GND, 3 A, 4 B, 5 C, 6 TEMP. Motor wires: red 5V, black GND, yellow H1, green H2, blue H3,
  white TEMP.
- CAN: TJA1051T/3 (U5), J10 (1 CAN_H, 2 CAN_L, 3 GND), FDCAN2 TX PB6 / RX PB12. **JP1 is bridged = 120 Ω
  termination ON** (the ESC is meant to be a bus end); cut the bridge to disable.
- UART J9 (1 5V, 2 TX, 3 RX, 4 GND), UART4 TX PC10 / RX PC11. USB to the Pi = Nucleo ST-LINK VCP = `Serial`
  (LPUART1, PA2/PA3).

## Motor: Flipsky 7070 110 KV, sensored (datasheet at 48 V)
12N14P → **7 pole pairs**. Phase resistance 0.054 Ω, Kt 0.0958 Nm/A (1 Nm ≈ 10.4 A). No load 5140 rpm / 1.1 A
(~50 W idle loss when spinning). **Best efficiency 91.2 % at 4755 rpm, 1.18 Nm, 13.4 A.** Max output 83 A at 53 %.
On 58 V the voltage-limited top speed is ~6200 rpm (needs SpaceVectorPWM). Inductance not measured yet (expected
very low → current ripple; try 20 / 30 / 40 kHz PWM and compare battery current).

## Car context and decisions so far
- ~100 kg with driver, aerodynamic; cruise needs only ~35–50 W at the wheel.
- **Burn-and-coast**: accelerate at ~12–18 A (motor sweet spot), then coast with `motor.disable()` (not 0 torque).
  A **freewheel** between motor and wheel is strongly recommended (otherwise the motor's ~50 W drag while coasting).
  Gear so the motor runs ~4500–5000 rpm during burns (≈16–18:1 for a 0.5 m wheel, 20–30 km/h). Current limit ≤ 30 A.
  Regenerative braking: hardware can, not planned (conflicts with the freewheel).
- Network: Raspberry Pi = main computer, steering-wheel ESP32 (display + the driver's single "go" button),
  GPS on USB. Now: everything over USB (motor controller via the Nucleo's USB, ideally through a USB isolator).
  Later: CAN with M12 A-coded connectors on housings (CiA 303-1: 2 V+, 3 GND, 4 CAN_H, 5 CAN_L), JST-XH inside,
  T-pieces, Pi in the middle (its termination OFF), ESC and wheel at the ends (termination ON).
- Driver button path is ESP32 → USB → Pi → USB → ESC, so the firmware **must** have a heartbeat/watchdog.

## Firmware (`firmware/`)
- PlatformIO, `ststm32@19.7.1` (STM32 Arduino core 2.12) + `Simple FOC@2.4.0`, both pinned: SimpleFOC 2.4.0
  does not compile on core 3.0. `workspace_dir` is moved under `~/.platformio/ws/esc-hat` because the project path
  plus SimpleFOC's nested includes exceed the 260-char Windows path limit.
- Environments `1_drv_check` … `5_foc_current` select the step via `-D STAGE=n` (see `firmware/README.md`).
  `pio run` alone builds only `1_drv_check` (default_envs); build all with `-e 1_drv_check -e 2_hall_test …`.
- If the VS Code PlatformIO extension fails to install Core (pip → PyPI "PermissionError 13"), install it from a
  normal terminal into the extension's venv: `%USERPROFILE%\.platformio\penv\Scripts\python.exe -m pip install -U platformio`.
- EN (PA11) is driven by our code, not given to SimpleFOC: EN low puts the DRV8353 to sleep and resets its registers.
- DRV8353 registers written at start (`include/drv8353.h`, SPI mode 1, 16-bit): GATE_HS 0x333 / GATE_LS 0x733
  (IDRIVE 150 mA source / 300 mA sink instead of the very hard 1000/2000 mA default), OCP 0x157 (100 ns dead time,
  auto-retry, VDS_LVL 0.4 V instead of 1 V which would never trip), CSA 0x280 (gain 20, VREF/2), DRV_CTRL 6×PWM.
- **Never call `analogRead()` once SimpleFOC's current sensing runs – on any pin.** The STM32 core de-inits the ADC
  after each `analogRead()` and force-resets ADC1+ADC2 together (shared ADC12 reset/clock), wiping SimpleFOC's
  injected setup on ADC2. All slow analog inputs go through `include/analog_in.h` instead: register-level single
  conversions on ADC2's free regular group (PA6 = IN3 FET NTC, PA7 = IN4 motor sensor, PC1 = IN7 throttle,
  PC3 = IN9 bus voltage). No `analogRead()` anywhere: on the real board `analogRead(PC3)` returned 0 V (the
  ADC2 reading gave 23.3 V), so every step starts with `ain::begin(false)` + `ain::busVolts()`; step 5 calls
  `ain::end()` before `current_sense.init()` and `ain::begin(true)` after it.
- Temperature protection (steps 3–5, `temperatureTask()` in main.cpp): every 200 ms; full power below
  FET 80 °C / motor 90 °C, linear derating, stop at 100 °C / 110 °C (motor only once `MOTOR_TEMP_SENSOR` is set).

### Firmware TODO (rough priority)
1. Hardware bring-up, steps 1–5 (`firmware/README.md`), at 20–24 V with a current limit.
2. Heartbeat/watchdog: no valid command for ~200 ms → `motor.disable()`; plus a hardware e-stop.
3. ~~FET + motor temperature, derating~~ done (2026-10-14, compiled, untested on hardware). Still to do: identify
   the motor sensor type from step 1's resistance reading and set `MOTOR_TEMP_SENSOR` in `include/board.h`.
4. Driver button: ramp the current target (e.g. +2 A/s up to ~13–18 A), coast = disable.
5. Tuning: `characteriseMotor()` for R/L, current PID P = L·ωc, I = R·ωc; SpaceVectorPWM; `SmoothingSensor`
   (SimpleFOCDrivers) for the halls; `sensor.velocity_max ≈ 800` rad/s; hard-code `zero_electric_angle` and
   `sensor_direction` after the first run so the car starts without the alignment wiggle.
6. Protocol to the Pi (USB serial now, CAN/FDCAN2 later).

## Ideas for board V2 (known weak spots of V1.2)
- GND stitching vias are sparse (median ~10 mm, worst 42 mm in the empty area under the Nucleo) → add a 8–10 mm grid.
- Copper pour only 0.25 mm from the V-scored top/bottom edges → keep ≥ 0.5 mm.
- Reverse-polarity protection; shorter gate traces (now 30–45 mm); maybe the MCU directly on the board.

## Tools (`tools/`, run with KiCad's Python: `"C:/Program Files/KiCad/10.0/bin/python.exe"`)
- `gen_jlc.py <board> jlcpcb/bldc-75v` → BOM + CPL with JLC rotation corrections (table `CORR`); part numbers in
  `lcsc_map.py`. Verified to reproduce the ordered files byte for byte.
- `overlay.py <board> <cpl.csv> REF=LCSC …` → checks a footprint's JLC rotation/offset (needs `curl`).
- `dfm_checks.py <board>` → silkscreen-to-pad/hole, annular rings, mask openings, vias in pads, isolated GND
  pieces, stitching density.

Release checklist after a board change: refill zones (KiCad: B) → DRC with schematic parity → `dfm_checks.py` →
gerbers + drill (below) → zip the 16 files → `gen_jlc.py` → `overlay.py` for any new footprint.
```
kicad-cli pcb export gerbers -o jlcpcb/gerbers/ -l F.Cu,In1.Cu,In2.Cu,B.Cu,F.Paste,B.Paste,F.Silkscreen,B.Silkscreen,F.Mask,B.Mask,Edge.Cuts bldc-75v.kicad_pcb
kicad-cli pcb export drill -o jlcpcb/gerbers/ --format excellon --drill-origin absolute --excellon-units mm --excellon-separate-th --generate-map --map-format gerberx2 bldc-75v.kicad_pcb
```

## Gotchas learned the hard way
- KiCad Python: after `board.Remove(x)`, other SWIG proxies can go stale → read all values first, then remove, then add.
  `board.Save(path)` writes a default `<name>.kicad_pro` next to it → save under another name or restore the .kicad_pro.
- Always refill zones before DRC/saving; stale fills cause fake clearance errors and real shorts.
- Closed rings of traces can cut off a piece of GND pour → `dfm_checks.py` check 6; fix with a GND via.
- JST-XH holes must be 1.0 mm (KiCad's 0.95 mm fails JLC's through-hole check). New footprints need an
  `overlay.py` check (JLC's B4B and SOT-23 needed 180°, SOIC-8 270°).
- The SEM logo is two items: copper text + solder-mask text; move both together.

## Working style
Push straight to `main` (no PRs unless asked). Ask before installing software or changing anything outside the
project. Explain electronics in plain words; prefer short steps with visible progress over long silent jobs.
