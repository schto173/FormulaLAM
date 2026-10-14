# ESC V2 – ideas and plan (draft, 2026-10-14)

Goal: a true **Nucleo HAT** (board on top of the NUCLEO-G474RE, ~70 × 82.5 mm), learning from V1.2.
Nothing here is decided yet – bring up and drive V1 first, then revisit this list.
Part numbers are **candidates**: check datasheet, voltage margin and LCSC/JLC stock before using them.

## 1. Power supply (option A)
- DRV8353 integrated buck → **12 V for VM only** (keeps the DRV cool, VM protected from bus spikes; VM abs max is
  80 V). Raise its frequency to ~500 kHz (on-time ≥ 400 ns at 58 V) so L1 shrinks from 12 × 12 mm to ~6 × 6 mm.
  Budget: 350 mA total = gate drive (~20–45 mA) + 12 V input of the 5 V converter.
- **Small synchronous 12 → 5 V buck** for Nucleo (E5V), halls, CAN (~250 mA). Candidates: TPS6216x family
  (3–17 V in, 1 A, 2 × 2 mm), TPS560430. Removes the LM5164, its 47 µH 12 × 12 inductor and 100 V input caps.
- Why not option B (DRV buck → 5 V, VM straight from the battery): ~1.05 W in the DRV at 20 kHz (~1.6 W at 40 kHz)
  instead of ~0.35 W, and VM (80 V abs max) would see the bus spikes the TVS lets through (up to ~97 V at full surge).
- **Test points**: 5 V, 12 V, 3.3 V, VCP, VGLS, SHA/SHB/SHC, SOA/SOB/SOC.
- **Scope ground**: 2–3 dedicated GND test loops (black THT loop, Keystone 5001/5011 style) – one at the power
  stage, one near the logic/Nucleo – for the scope's ground clip. Plus a small GND pad ~2.5 mm next to every
  fast-signal test point (gates, phase nodes, SOx) so the probe's short ground spring can be used instead of the
  long clip (the clip's loop picks up switching noise and shows ringing that isn't there).
  (V1 has none: use the Nucleo's top-side GND morpho pins CN10-9/20, CN7-8/19/20/22 for logic, and the bottom-side
  solder joint of C11/C21's negative lead or J5 battery − near the power stage. V1 mounting holes are not GND.)

## 2. FOC performance and efficiency
- **Magnetic absolute encoder** on the motor shaft (MT6701: 14 bit, SSI/ABZ and UVW "hall" outputs; or AS5047P on
  SPI with its own CS). Halls stay as backup. Needs a diametric magnet on the 7070's shaft end – check mechanics.
- **Inline phase current sensing** (INA240: −4…80 V common mode, PWM rejection) instead of / in addition to low-side
  shunts → current measured continuously, better at high duty cycle and speed.
- **Tight power loops**: bus ceramics directly across each half-bridge, loop closed over the In1 GND plane, short
  gate/source pairs (V1 gate traces are 30–45 mm). Enables faster edges → lower switching loss (at 58 V switching
  loss is a big share: ~0.5 W per switching FET at 13 A with V1's soft 50/25 ns edges).
- **Battery energy meter** (INA228: 85 V, 20 bit, I²C, V/I/P/energy) on the battery input → Wh per lap/burn.
- **Wheel speed input** (hall/reed on the wheel): with a freewheel the motor stops while coasting; to re-engage
  smoothly the controller should match motor speed to wheel speed first (velocity mode), then apply torque.
- Keep phase voltage sensing only if sensorless backup is wanted; otherwise drop U3 + dividers (~10 parts).

## 3. Safety and robustness
- **Reverse-polarity protection** (V1 has none). Options: N-MOSFET in the negative line (simple, any voltage, but the
  board GND then differs from battery − by the FET drop – check against grounds shared over USB/CAN), or a high-side
  ideal-diode controller with back-to-back FETs (most are rated ≤ 65 V – too little margin at 58 V).
- **Hardware e-stop + watchdog**: DRV ENABLE = MCU_EN AND E-STOP_OK AND WATCHDOG_OK (single-gate logic, e.g.
  74LVC1G08 ×2). Watchdog e.g. TPS3431 (MCU must toggle a pin). Motor turns off without any software.
- **Locking connectors** (JST-GH or Molex Micro-Fit) instead of JST-XH – vibration.
- Optional **isolated CAN** (ISO1042 + small isolated 5 V supply) against ground loops.
- Conformal coating (JLC option) for outdoor use.
- From V1 DFM: GND stitching via grid every 8–10 mm; copper ≥ 0.5 mm from V-scored edges.

## 4. Size
- Board sits **on top of** the Nucleo (classic shield): tall parts allowed, Nucleo USB free underneath.
- Fuse (inline ATO holder) and large bulk caps move **into the battery cable**; on board several SMD hybrid-polymer
  caps (100 V, ~10 mm) sized for the input ripple current.
- Smaller connectors, dense placement, possibly small parts on both sides.
- Thermal: less copper around the FETs – fine for eco-marathon currents (burns ~13 A); 30 A continuous would need a
  heatsink (expose a thermal pad / bottom copper under the FETs).

## Top 5 if only five things happen
1. Reverse-polarity protection  2. Hardware e-stop + watchdog  3. Energy meter (INA228)
4. Magnetic encoder  5. Tight power loops / power stage layout
