# ESC-BLDC-FORMULA1 V2.0 (work in progress – placement only, NOT routed, NOT orderable)

Redesign of the V1.2 ESC HAT (DRV8353RS + 6x CSD19532Q5B, SimpleFOC, NUCLEO-G474RE). V1.2 stays untouched in
`../ESC-BLDC-FORMULA1-V1_2/` (those are the files that were ordered).

## Background: how V1.2 board #1 failed
At ~45 V and ~93 % modulation the DRV8353 faulted and its internal low-side gate-drive supply was permanently
damaged (GDUV, VGLS 3 V instead of ~11 V). Not a component defect (C29 is not the cause); the root cause is not
determined yet – see `../ESC-BLDC-FORMULA1-V1_2/CLAUDE.md`. Trace length alone does not explain it: TI's
DRV8353RH-EVM has gate paths of the same length as V1.2. V2 follows that TI board (same parts, low-side shunts
into the DRV8353 amplifiers, DRV beside the FETs) and must survive a 60 V battery.

## What changed vs V1.2
- **Power supply (option A):** DRV8353 buck makes 12 V (VBUCK) for VM only, faster (R34 180k ≈ 480 kHz, L1 100 µH 6x6);
  TPS562201 makes 5 V from 12 V (replaces the LM5164 58 V buck). 5 V now comes up at ~6 V bus.
- **Phase voltage sensing removed** (U3 + dividers) – SimpleFOC does not use it. Fuse F1 removed (fuse goes in the battery cable).
- **Bus caps:** 3x SMD 47 µF 100 V (C11, C21, C68) + MLCCs in every half-bridge; TVS SMCJ60A (C224057; starts conducting at 66.7 V, so the board is safe with up to a 60 V battery).
- **Wheel speed input** J11 (JST-XH 3P: 1 = +5V, 2 = SIG, 3 = GND) → D10 / R78 4.7k to 3V3 / C73 → **PC6 (TIM3_CH1)**.
- **Test points:** TP1/TP2 Keystone 5001 GND loops for the scope clip (power side / logic side), pads TP3 VGLS,
  TP4 VCP, TP5 12 V, TP6 5 V, TP7 3.3 V.
- **Motor pads** are three single 2.5 mm² solder-wire pads: J6 = MOTC, J12 = MOTB, J13 = MOTA.
- Freed Nucleo pins: PB0, PC0, PC2 (were phase voltage sensing).

## Layout (board 98 x 90 mm, x 143–241 / y 44–134)
- Sits on top of the Nucleo (sockets on our bottom side); the **ST-LINK / USB end of the Nucleo stays uncovered**.
- **Power stage "pinwheel"** (after TI's DRV8353RS EVM): DRV rotated 45°, one half-bridge cell per DRV pin group –
  phase C above, B right, A below – so every phase gets a short gate/sense path (14–26 mm straight line; V1 had
  30–45 mm). Each cell: shunt (Kelvin, net tie under the body) → LS FET over HS FET → motor pad at the outer edge,
  MLCCs right beside the HS drain. Route GHx with SHx and GLx with SPx as tight pairs.
- Battery pads J5 + TVS at the top-right edge, bulk caps in the right corners; plan an inner VDRAIN plane under
  the power stage and a solid GND plane.
- Connectors next to the Nucleo pins they use: wheel / CAN / halls in the strip above CN10, UART / LEDs / throttle
  in the strip below CN7.
- M3 holes in the 4 corners; the Nucleo's own 3 holes are kept for standoffs.

## Before ordering
- Route the board; check that the HAT clears the Nucleo's Arduino headers (socket height).
- `tools/lcsc_map.py` is still the V1.2 map → update for V2 parts (U6, L3, C68–C73, R75–R78, D10, J11, J12, J13,
  TP1/TP2 = C238122; TP3–TP7 are bare pads) before running `gen_jlc.py`.
- Firmware: wheel input on PC6; 5 V rail now from VBUCK.

Placement is scripted: `tools/v2_place.py` (positions), `tools/v2_pcb_prepare.py` (netlist → board),
`tools/v2_sch_*.py` (schematic changes, each run once).
