# ESC – BLDC motor controller HAT (V1.2)

Motor controller for the car: a 4-layer HAT that plugs onto a **NUCLEO-G474RE** and drives the
hub motor with **SimpleFOC** (6-PWM, low-side current sensing, hall sensors).

| | |
|---|---|
| Battery | 58 V max (fuse holder for an ATO blade fuse, 58 V rated) |
| Phase current | 15–30 A |
| Gate driver | TI DRV8353RS (SPI, 3 current-sense amplifiers, integrated buck) |
| Power stage | 6× TI CSD19532Q5B (100 V), 2 mΩ shunts, Kelvin sensing |
| Logic supply | LM5164 buck → 5 V (Nucleo via E5V), Nucleo LDO → 3.3 V |
| Protections | Bus TVS (SMCJ60A), fuse, ESD on voltage-sense (TPD4S010) and CAN (PESD1CAN), clamp diodes on hall inputs, DRV8353 VDS/OCP/UVLO |
| Extras | FET temperature NTC, UART telemetry, CAN bus |

Open `bldc-75v.kicad_pro` with **KiCad 10**. All libraries are in this folder and referenced with
`${KIPRJMOD}`, so nothing needs to be installed.

## Connectors

| Ref | Connector | Pins |
|---|---|---|
| J5 | Battery, solder pads for 4 mm² wire | BAT+ / BAT− (use an XT90-S anti-spark plug in the cable) |
| J6 | Motor phases, solder pads for 4 mm² wire | PHASE C / B / A (left → right) |
| J8 | Hall sensors, JST-XH 5-pin | +5V, GND, A, B, C (as on the silkscreen) |
| J7 | Throttle / driver button, JST-XH 3-pin | 3V3, SIG, GND |
| J9 | UART telemetry, JST-XH 4-pin | 1 = 5V, 2 = TX, 3 = RX, 4 = GND (3.3 V logic, 330 Ω series) |
| J10 | CAN bus, JST-XH 3-pin | 1 = CAN_H, 2 = CAN_L, 3 = GND |

**CAN:** TJA1051T/3 transceiver (bus pins survive ±58 V). The ESC is meant to sit at one **end** of
the bus, so the 120 Ω termination is **on** (solder jumper JP1 is bridged). Cut the bridge between
the JP1 pads if the board ever sits in the middle of the bus.
On the housings: M12 A-coded, CiA 303-1 pinout (2 = V+, 3 = GND, 4 = CAN_H, 5 = CAN_L).

**UART to a Raspberry Pi:** connect only GND, TX and RX (cross TX↔RX). The 5 V pin on J9 is for small
modules only – the 5 V rail is 1 A in total and also feeds the Nucleo and the hall sensors.

## Nucleo pin map (firmware)

| Function | Pins |
|---|---|
| 6-PWM (A_H, A_L, B_H, B_L, C_H, C_L) + enable | `BLDCDriver6PWM(PA10, PB15, PA9, PB14, PA8, PB13, PA11)` |
| DRV8353 SPI | SCK PB3, MISO PB4 (SDO), MOSI PB5 (SDI), CS PB11 |
| DRV8353 nFAULT | PB10 |
| Current sense SOA / SOB / SOC | PA0 / PA1 / PA4 (all on ADC2) |
| Voltage sense A / B / C / bus | PB0 / PC0 / PC2 / PC3 (divider 383 k / 9.76 k) |
| Halls A / B / C | PB2 / PB1 / PC4 |
| Throttle | PC1 |
| FET temperature (NTC 10 k B3380, 10 k pull-up to 3.3 V) | PA6 (ADC2_IN3) |
| UART telemetry (UART4) | TX PC10, RX PC11 → `HardwareSerial Serial3(PC11, PC10)` |
| CAN (FDCAN2) | TX PB6, RX PB12 |
| Status LED | PB7 |
| USB to the Pi | Nucleo ST-LINK virtual COM port (LPUART1, PA2/PA3 = `Serial`) – no extra hardware |

**Before the first power-up:**
- Nucleo jumper **JP5 on E5V** (the Nucleo is powered from this board).
- Firmware must set the DRV8353 over SPI: lower `VDS_LVL` for the CSD19532Q5B and pick the
  current-sense amplifier gain.
- The motor should stop by itself if the control link drops (watchdog), and keep a hardware
  emergency stop that cuts motor power independently of any software.

## Firmware

`firmware/` is a PlatformIO project (VS Code + PlatformIO) for the first power-up with SimpleFOC, split
into five steps: DRV8353 check → hall test → open loop → closed loop → FOC with current sensing.
It also writes the DRV8353 settings (gate drive, overcurrent threshold, CSA gain).
See [`firmware/README.md`](firmware/README.md).

## Manufacturing (JLCPCB)

Everything for the order is in `jlcpcb/`: `bldc-75v_gerbers.zip`, `bldc-75v_bom_jlc.csv`,
`bldc-75v_cpl_jlc.csv` (rotations already corrected to JLC's footprints).

Order settings: 4 layers · ENIG · 2 oz outer / 1 oz inner copper · via covering
"Epoxy Filled & Capped" · confirm production file and parts placement · top-side assembly.

Not assembled on purpose: the Nucleo (plugs in), battery and motor wire pads (solder the wires),
JP1 (it is only a copper bridge).
