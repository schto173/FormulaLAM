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
  - Motor temperature sensor = **NTC 10k** (11.85 kΩ at ~21 °C), `MOTOR_TEMP_SENSOR` set (B3950 assumed).
  - Step 2 ✓: all 6 hall states, never 0/7, no skipped steps; sequence 1-3-2-6-4-5; **42 net steps per turn →
    7 pole pairs confirmed**. The hall test now counts signed steps (hand wiggle cancels out).
  - Step 3 ✓ (after a fix): first try only jerked – `driver.voltage_limit` was 0.5 V, so SimpleFOC centred the
    phases at 0.25 V (0–2 % duty, eaten by dead time). Now driver limit = Vbus, motor limit 0.5 V, MCU dead time
    100 ns → turns smoothly. Scope: clean 0–24 V square wave on all three phases.
  - Step 4 ✓: initFOC ok – `sensor dir: CCW`, `PP check: OK`, `Zero elec. angle: 3.14`. Voltage mode, no load:
    T0.3 barely turns (cogging + dead time), T1.0 → 16 rad/s (KV 110 predicts ~11.5 → hall 60° steps probably
    lead the voltage a bit; compare with the reverse speed). FET 28 °C, motor 22 °C.
  - Step 5 ✓: current sense align `Success: 1` (A/C pin fix + gain sign +20 confirmed), ADC2 + TIM1 trigger.
    `MFP0.4` twice: R 0.16 / 0.24 Ω (line-line incl. FETs/wires, dead time inflates it), Ld 50–60 µH,
    Lq 70–90 µH, "New el. zero" 2.74 both times (hall alignment says 3.14). `MFC200` → Pq 0.09, Pd 0.06,
    I 199 = firmware defaults now (`CURR_*` in board.h), current LPF 1 kHz. `MFC100` sounded the same.
  - Noise fixes: 512-byte serial TX buffer (the 1 Hz status line blocked loopFOC → "clack"), SmoothingSensor
    (SimpleFOCDrivers 1.0.9) between HallSensor and motor (`HALL_SMOOTHING`) → smooth; a remaining deep
    hum/"brrr" at low speed is hall ripple + cogging (12N14P → 84 cogging steps/turn). A metallic tick once
    per turn was a circlip rubbing on the mounting plate (mechanical, fixed).
  - At 1 V (`MLU1`), no load, smoothing on: 3.14 → ±17.6 rad/s, Iq ~0.4 A; 2.74 → ±17.3 rad/s, Iq ~0.27 A.
    No clear winner at no load (differences < the ±0.2 A scatter); both directions symmetric now (without
    smoothing 16.0 vs 17.2). Pick a fixed `zero_electric_angle` later under load. Starting from standstill
    needs ~1.5–2 A (cogging), T1 sometimes needs a push.
  - Throttle (hall, on J7 3.3 V): 0.87 V released, 2.60 V full → defaults with dead band 0.97 / 2.55 V.
  - 30 V bench, `MLU12`, throttle 100 % (5 A max): **265 rad/s ≈ 2530 rpm**, Iq ≈ 0.1 A at top speed, FET 27 °C,
    motor 24 °C. Throttle 54 % → target 2.7 A, Iq 2.3 A. Coasting (Iq ≈ 0, Uq = back-EMF): 9.74 V @ 216 rad/s,
    6.35 V @ 137 rad/s → **Ke = 0.046 V/(rad/s) phase peak → KV ≈ 120 rpm/V** (datasheet 110; KV is line-line,
    i.e. √3 × phase – rough "110 rpm per volt of Uq" estimates earlier were off by that factor).
    Max no-load speed ≈ Uq / 0.046: SinePWM Uq ≤ Vbus/2, SVPWM ≤ Vbus/√3 (58 V → ~700 rad/s ≈ 6700 rpm).
  - SpaceVectorPWM on (`USE_SVPWM`), `MLU` capped at 0.55 × Vbus. 30 V, `MLU16`: **363 ± 2 rad/s ≈ 3450 rpm**,
    35 s at full throttle: FET 27.0 °C flat, motor 23.4 → 24.5 °C. Iq at top speed averages ~0.55 A
    (≈ 13 W no-load loss) but scatters −1.2 … +1.9 A: at 93 % modulation the highest phase gets only ~1.7 µs
    low-side time per PWM period, too short for a clean shunt reading (with SinePWM at MLU12: ±0.1 A).
  - **Battery is 12S Li-ion** (50.4 V full, ~43 V nominal, 36 V empty) – not 14S. Firmware env `5_foc_battery`.
    No-load top speed with SVPWM at 0.55 × Vbus: full ~600 rad/s (5700 rpm), nominal ~515 rad/s (4900 rpm),
    empty ~430 rad/s (4100 rpm); under 15 A load ~1.5 V less. → 4500–5000 rpm burns only in the upper half of
    the charge; consider gearing for ~4000–4500 rpm. Pack read 36 V (empty) on 2026-10-14 → charge first.
  - **First run on the 12S pack (2026-10-15)**, env `5_foc_battery`, 5 A fuse in F1, JBD BMS (it had cut the output
    at 36 V / cell UV; output read a meaningless 11 V until charged). Pack ~43.6 V (MLU max 24 V):
    **`MLU24` → 550 rad/s ≈ 5250 rpm** no-load (datasheet 5140 rpm @ 48 V). Hiss at standstill (0 A FOC with the
    FETs switching) fixed with the DRV8353 COAST bit while released + stopped.
  - **Board #1 FAILED (2026-10-15)** on the pack (~43.6 V), auto voltage cap 24 V, throttle 43 % (2.2 A),
    ~512 rad/s (~93 % modulation): `STOPPED: DRV8353 nFAULT`. Since then permanent **GDUV**, even with no PWM
    (step 1), across power cycles: **VGLS only 3 V instead of ~11 V**; VBUCK 12 V ok, SPI/registers ok, all six
    gates 118–137 kΩ to GND/phase (no FET gate broken).
    **Failure mode: the DRV8353's internal low-side gate-drive supply (VGLS regulator / low-side driver) is
    damaged** – it no longer reaches ~11 V. Not a component defect – C29 is NOT the cause.
    **Root cause: not determined yet.** Trace length alone does not explain it: TI's DRV8353RH-EVM (MD016, same
    parts, low-side shunts, 95 V / 15 A) has its FET gates ~27–31 mm (straight line) from the DRV gate pins;
    V1.2 routed lengths are GH 33–45 mm, GL 21–35 mm, SP 17–33 mm – the same range. Conditions at the failure:
    ~45 V, ~93 % modulation, where the low-side shunt window is only ~1.7 µs and the current readings were
    already poor (controller spikes possible). On board #2: scope VGLS, SPx and the phase nodes at 45 V while
    stepping the modulation up.
    (Also fixed: halt() read the fault registers after EN low → garbage 0x7FF.)
    Firmware since then: `MLU_MAX_FRACTION` 0.45 (was 0.55); scope the phase nodes / SPx at 45 V before high speed.
  - **Drive concept decided (2026-10-15), efficiency car, not performance:** throttle = maximum speed 0–40 km/h,
    P speed controller → current 0–10 A (hard, never more), ramp +2 A/s (soft freewheel engagement), never
    negative (freewheel hub, no braking), released = coast, COAST once stopped. Gear 8:1 (confirmed 2026-10-20) / wheel 0.508 m assumed
    (to confirm). Voltage cap 17 V but ≤ 0.40 × Vbus. Protections after board #1: total |U| limit (CappedMotor),
    DRV8353 in 3x PWM mode as on TI's EVM (DRV makes low side + dead time, INLx = phase enable; was 6x), IDRIVE 50/100 mA (TI rule IDRIVE = Qgd / 100-300 ns edge, 2026-10-19; was 150/300, then 100/200), OCP latched at VDS 0.3 V (was auto-retry 0.4 V), sine PWM, hall smoothing ON again (2026-10-20; was off after board #1 without a specific reason),
    hard stops (FET 90 °C, motor 100 °C, pack < 36 V for 2 s). Next: board #2 bring-up, then scope the phase
    pads / low-side sources at 45 V and step the load up to 10 A.
  - **Board #2 (new board, 2026-10-20)**, firmware with 3x PWM / IDRIVE 50-100 / OCP latched: step 1 ok - all
    registers ok (0x311, 0x711, 0x116, 0x280, 0x20), faults 0x0 0x0 at 18.5 V. **VGLS across C29 = 12.1 V** (healthy
    reference; dead board #1: 3 V).
    Steps 2-5 ok on the bench (24 V): halls 42 steps; open loop only rocked at 0.5 V, perfect at 1.0 V (3x PWM +
    slow edges cost ~0.3-0.4 V); step 5 current-sense align `Success: 1`, sensor dir CW (other phase order than
    #1), speed control tracks the throttle target within ~0.3 km/h at 0.2-1.2 A no-load. Two fixes after the
    first run: (1) it whistled at ~200 rad/s = the 10 V cap (Uq 9.4 of 10 V) - effective Ke is 0.048 in 3x mode,
    so the speed target limit now uses 0.050 + 1.5 V reserve; (2) the current PI wound up to ~10 V while in
    COAST - now held at 0 during COAST and the PWM set to 0 V before leaving it.
    Re-test ok: Uq 0.00 V in COAST, clean start, full throttle 19.1 km/h (167 rad/s) at Uq 8.3 V of a 10 V cap,
    no whistle, FET 24.7 C flat. **Board #2 bench bring-up (steps 1-5, 3x PWM) done.** Next: pack (`5_foc_battery`)
    with the scope on a phase pad at moderate speed, then load up to 10 A.
    **Pack run ok (2026-10-20, `5_foc_battery`, ~43.5 V):** throttle 82 % -> target 29.0 km/h, motor 28.8, Uq 12.3 V of
    the 17 V cap (= 0.28 x Vbus), Iq 0-1 A no-load, FET 25 C, clean coast-down. Scope (handheld 50 MHz, 10x) on a
    phase pad, no load: 0-44 V square wave; falling edge ~200-250 ns, smooth S-shape, no overshoot, no ringing,
    no spike below 0 V. Still to do: rising edge, full throttle (~35 km/h, Uq ~15 V), then the same edges UNDER
    LOAD (5 A, 10 A) - spikes scale with the switched current; ideally also a probe on a low-side source (SPx).
  - **Scope under load (2026-10-21, board #2, pack ~44 V, max current, ~15 km/h, MOTOR_DIRECTION -1):** phase
    falling edge ~30-40 ns (current-driven), at most a ~2-3 V / ~10-15 ns undershoot + one small ring, settled in
    ~50 ns; rising edge also clean (user: "nothing extraordinary"). No sign of spikes that could hurt the DRV.
  - **More voltage (2026-10-21): SVPWM back on, cap 0.45 x Vbus, `FOC_VOLTAGE_MAX` 19.5 V.** With SVPWM the highest
    phase duty for the same |U| is 0.5 + 0.866 U/Vbus (sine: 0.5 + U/Vbus), so SVPWM at 0.45 keeps a 5.5 us low-side
    window (sine at 0.40 had 5.0 us; board #1 died at SVPWM 0.55 = 1.2 us). **Tested on the pack (board #2, same
    load setup):** full throttle 37-37.7 km/h (sine/0.40: 33), Uq 15.1 V of an 18.8 V cap (80 %, |U|/Vbus 0.36), Iq at
    top speed 2.5-5.5 A around a 3.5-4.3 A target (same scatter as with sine), FET 25 -> 27.8 C, 0 -> 37 km/h in ~9 s
    at 10 A. P droop ~2 km/h at ~4 A load remains (target 39.5). Load run before (sine, 0.40, 10 A):
    ramp 2 A/s exact, Iq tracks within ~0.5 A, wheel breaks away at 4-5 A, 100 % throttle -> 33 km/h (P droop ~2 km/h
    at ~4 A load), FET 22 -> 27 C, pack sag 43.4 -> 40.7 V at ~12 A motor current (~0.5 ohm path - check the cells).
  - **Two-phase current reconstruction (2026-10-26, untested):** `TwoPhaseLowside` (main.cpp) replaces the reading of
    the phase above `RECON_DUTY` 0.80 by -(other two) - what a VESC does. On after initFOC, `X0`/`X1` to compare, the
    status line shows `recon x %`. Voltage fraction now live: `V0.45` (start) .. `V0.55` (`FOC_FRACTION_LIMIT`;
    2nd-highest phase keeps >= 4.4 us), cap max 24 V; speed limit still 40 km/h, so more voltage = more headroom /
    40 km/h also with a sagging pack. Plan: compare Iq scatter at top speed X0 vs X1 at 0.45, then V0.50, V0.55.
    In data mode the firmware computes Iq both ways every cycle from the same samples (`$D` fields iq_meas, sd_meas,
    iq_calc, sd_calc, calc%), the dashboard shows them side by side - no X0/X1 toggling needed for the comparison.
    Result 2026-10-26: 0 % calculated at low speed, ~40-50 % at top speed without load, ~12 % under load; scatter
    measured vs calculated "similar" at 0.45 and 0.48 -> measurement was already good enough there. 0.48: 38.3 km/h (8:1).
  - Gearing changed to 13.8:1 (2026-10-26, GEAR_RATIO 13.8; briefly assumed 15). 40 km/h needs 604 motor rad/s = 29 V
    back-EMF (phase peak), but 12S at 43 V gives at most Vbus/sqrt3 = 24.8 V even at full SVPWM -> ~31 km/h theoretical
    (~37 km/h at 50.4 V full), ~25 km/h at 0.48 / 43 V, ~29 km/h at 0.55 / 43 V (model; real ~5-10 % lower).
  - **Trip on board #2 (2026-10-26)**, 13.8:1, V0.48, Vbus 46.8 V, no load, full throttle, 26.0 km/h (395 rad/s),
    Iq ~1 A, recon 100 %: `STOPPED: DRV8353 nFAULT`, faults `0x604` = VDS_OCP on the **low-side FET of phase B**
    (VDS > 0.3 V for > 2 us = ~60-75 A on a CSD19532 at ~4 mOhm). The latched OCP shut down; the board still runs
    afterwards (no GDUV). Same regime as board #1's death (~46 V, high modulation). No data captured. Suspects:
    angle error (hall glitch) at full voltage, current loop at the voltage cap, false VDS trip. User keeps V <= 0.50.
    -> **Black box** added: every control cycle (angle, hall, raw ia/ib/ic, calculated phase, Iq/Id, Uq/Ud, duties,
    speed, nFAULT) into a 1024-entry RAM ring; `halt()` prints it after any stop as `$B,...` lines (~7 s), the
    dashboard saves it automatically as `esc-blackbox-<time>.csv` (Downloads).
  - Direction back to normal (`MOTOR_DIRECTION 1`) with the new gearing; user is fitting a larger pinion for 35 km/h.
  - 2026-10-29: pinion 15 -> 22 teeth -> **GEAR_RATIO 9.41** (13.8 x 15/22). Model at V0.50: ~35 km/h at 43 V under
    load. The board still ran 04c5329 (13.8:1, REVERSED default, no black box) until this flash.
    Not a firmware limit (a VESC has the same one, except with field weakening = extra -Id, ~ -28 A at 40 km/h here).
    Options discussed: gearing ~10:1, higher-KV motor, more cells.
  - Bus electrolytics C11/C21 = Samyoung NXA 330 uF 100 V (1.65 A ripple @100 kHz, 36 mOhm): ripple per cap ~2.1 A at
    10 A, ~4.1 A at 20 A motor current -> 20 A bursts ok, continuous 20 A too much; V2: bigger bus capacitor bank.
  - **Stall (2026-10-21):** wheel held with the brake + throttle -> motor buzzed ("brrrr", a rotating "whhht") without
    moving. Likely the rotor sitting on a hall edge under high current (hall flips, current vector jumps 60 deg;
    smoothing extrapolates the flips into rotation) - not measured yet (ask for a dashboard CSV of a 2-3 s stall).
    Added stall guard (threshold raised to 8 A): >= 8 A asked for while < 3 rad/s for 3 s -> 0 A, `# STALL`, throttle must be released.
  - **Current hard max now 20 A (`CURRENT_MAX`, 2026-10-21)**, start-up limit still 10 A; 15/20 A only via `L`.
    20 A continuous ~12-15 W in the FETs -> bursts only without heatsink; 20 A fuse; check the JBD discharge limit.
    (Earlier: hard max 15 A,) every start still uses 10 A (`CURRENT_LIMIT`);
    15 A only via `L15` / the dashboard. Estimate at 15 A, 45 V continuous: ~8-9 W in the FETs, battery ~7 A ->
    15 A fuse. Load test at 5 / 10 A still to do before using 15 A.
  - Gearing note: with 8:1 and a 20" wheel (~0.508 m), 3500 rpm ≈ 42 km/h, 5250 rpm ≈ 63 km/h; 20–30 km/h at
    ~4500 rpm needs ~16–18:1.
  - **Bring-up steps 0–5 done on board #1 (24 V bench, no load).** Not yet tested: higher bus voltage / battery,
    load, higher speeds (`MLU` > 2), long runs / temperatures.

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
7. Current sensing at high modulation (burns at 4500–5000 rpm run near full output): per PWM period skip the
   phase with the highest duty (shortest low-side window) and compute it from the other two (ia+ib+ic = 0).
   SimpleFOC 2.4.0 always uses all three. Alternatives: lower PWM frequency, or a lower `MLU_MAX_FRACTION`.

## Ideas for board V2 (known weak spots of V1.2)
Full draft plan: `V2-PLAN.md` (Nucleo-HAT size, option A supply = DRV buck 12 V for VM + small 12→5 V buck,
encoder, inline current sense, energy meter, reverse-polarity protection, hardware e-stop/watchdog, scope GND points).
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
