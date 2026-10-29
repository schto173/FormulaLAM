// ESC HAT pin map and board constants (NUCLEO-G474RE). Matches the KiCad schematic.
#pragma once
#include <Arduino.h>

// ---- motor: SET THESE FOR YOUR MOTOR ------------------------------------------------------------
#define POLE_PAIRS        7       // step 2 (hall test) tells you the real value
// The 7070 has only ~0.054 Ohm + ~0.01 Ohm FET/shunt/wire, and at standstill nothing but that resistance
// limits the current: 0.5 V ~ 8-10 A in the windings, 3 V would be ~50 A and collapse a current-limited
// bench supply below the 16.8 V UVLO of the 5 V regulator (Nucleo resets).
// 3x PWM + IDRIVE 50/100 mA: every switching edge is slower and the DRV waits for the opposite FET (VGS
// handshake), so small voltages lose ~0.3-0.4 V at 24 V (~0.6-0.7 V at 45 V). Board #2, 2026-10-20: open loop
// only rocked at 0.5 V, ran perfectly at 1.0 V. Current control (step 5) compensates this by itself.
#define ALIGN_VOLTAGE     0.8f    // V used to align the halls in steps 4-5 on the bench (was 0.4 in 6x mode)
#define VOLTAGE_LIMIT     1.0f    // V max applied to the motor in steps 3-4 (was 0.5 in 6x mode)
#define CURRENT_LIMIT     10.0f   // A motor current limit after every start (step 5)
#define CURRENT_MAX       20.0f   // A HARD maximum - "L" can raise the limit up to this, never above (2026-10-21).
                                  //   20 A at 45 V continuous: ~12-15 W in the FETs (too much without heatsink -
                                  //   bursts only), battery ~10 A -> 20 A fuse + BMS discharge limit; VDS OCP 0.3 V
                                  //   still above 25 A peak x 8 mOhm hot. Edges clean at max load (scope, board #2).
#define FOC_VOLTAGE_LIMIT 1.0f    // V cap in step 5 during start-up (alignment); then the cap below
#define FOC_VOLTAGE_MAX   24.0f   // V motor voltage cap: 40 km/h at 8:1 needs ~350 rad/s x 0.05 + 1.5 V reserve = 19 V;
                                  //   never above FOC_MAX_FRACTION x Vbus (was 17 V until 2026-10-21)
#define USE_SVPWM         1       // SpaceVector PWM: for the same motor voltage the highest phase duty is lower than with
                                  //   sine (0.5 + 0.866 U/Vbus instead of 0.5 + U/Vbus) -> the low-side shunts get a LONGER
                                  //   measuring window: SVPWM at 0.45 x Vbus -> max duty 89 % (5.5 us of 50 us), sine at
                                  //   0.40 -> 90 % (5.0 us). Board #1 died at SVPWM 0.55 -> 97.6 % (1.2 us). Back on 2026-10-21.
#define HALL_SMOOTHING    1       // 1 = SmoothingSensor estimates the angle between the 60-degree hall steps (smooth sine, quieter)
// The voltage cap never exceeds this fraction of Vbus: board #1 lost its DRV8353 at ~0.55 x Vbus (SVPWM, 1.2 us
// low-side window). 0.45 with SVPWM keeps a 5.5 us window (more than the 0.40 sine setting before). With an empty
// pack this lowers the top speed.
#define FOC_MAX_FRACTION  0.45f   // default at start; live with "V<fraction>" between 0.30 and FOC_FRACTION_LIMIT
#define FOC_FRACTION_LIMIT 0.55f  // highest "V" accepts: with two-phase reconstruction the 2nd-highest phase still
                                  //   has >= 4.4 us (SVPWM: 0.5 + 0.75 U/Vbus = 91 %) to measure at 0.55
// Two-phase current reconstruction (what a VESC does): a phase above RECON_DUTY (its low-side window < 10 us)
// is not measured but calculated from the other two: ia + ib + ic = 0 (star-connected motor). Only after initFOC.
#define RECON_DUTY        0.80f
// Current PI from "MFP0.4" + "MFC200" on the 7070 (2026-10-14: R 0.16 Ohm incl. FETs/wires, Ld 50 uH, Lq 70 uH)
#define CURR_Q_P          0.09f
#define CURR_D_P          0.06f
#define CURR_I            200.0f

// ---- motor temperature sensor (white wire, J8 pin 6) ------------------------------------------------
// Type unknown so far: step 1 prints the sensor resistance and a guess. Set it here, then steps 3-5 show
// the motor temperature in C and stop on over-temperature.
#define MOTOR_TEMP_UNKNOWN 0
#define MOTOR_TEMP_NTC10K  1
#define MOTOR_TEMP_KTY84   2
#define MOTOR_TEMP_PT1000  3
#define MOTOR_TEMP_SENSOR  MOTOR_TEMP_NTC10K   // measured 11.85 kOhm at ~21 C room temperature (2026-10-14)
#define MOTOR_NTC_BETA     3950.0f // only for NTC10K; 3950 is the usual motor NTC (check the motor's spec)

// ---- bus protection (steps 3-5) -----------------------------------------------------------------------
// A torque against the rotation turns the motor into a generator and pumps energy into the bus caps (660 uF).
// A bench supply cannot take it back: ~1-2 J from the spinning can would lift 30 V far above 60 V.
#define NO_REGEN_SPEED    3.0f    // rad/s: above this a torque target against the rotation is set to 0 (coast)

// ---- supply: bench (BATTERY_CELLS 0) or Li-ion pack (env 5_foc_battery sets -D BATTERY_CELLS=12) ----------
#ifndef BATTERY_CELLS
#define BATTERY_CELLS     0
#endif
#if BATTERY_CELLS
#define BUS_OV_LIMIT      (BATTERY_CELLS * 4.20f + 5.0f)  // 12S: 55.4 V (full 50.4 V + margin; TVS stand-off 60 V)
#define BUS_START_MAX     (BATTERY_CELLS * 4.30f)         // 12S: 51.6 V - above: wrong pack, refuse to start
#define BUS_UV_STOP       (BATTERY_CELLS * 3.00f)         // 12S: 36.0 V - below for more than 2 s: stop (empty)
#define ALIGN_V_BUS       1.2f    // V for hall + current-sense alignment on the pack (~0.6-0.7 V lost at 45 V)
#else
#define BUS_OV_LIMIT      40.0f   // V, bench supply (max 30 V)
#define BUS_START_MAX     35.0f
#define BUS_UV_STOP       0.0f    // no under-voltage stop on the bench
#define ALIGN_V_BUS       ALIGN_VOLTAGE
#endif

// ---- throttle (J7: 1 = 3.3 V, 2 = signal, 3 = GND; 100k pull-down -> unplugged reads 0 V) - step 5 only --
// Throttle 0..100 % -> 0..limit A (limit = CURRENT_LIMIT, change live with "L"). Calibrate live: release fully
// + "AI", press fully + "AF"; then put the printed values here.
#define THROTTLE_ON_AT_START 1    // 1: throttle controls the current after start-up ("A0" = off, T commands again)
// hall throttle on 3.3 V (2026-10-14): ~0.87 V released, ~2.60 V fully pressed -> with dead band:
#define THROTTLE_IDLE_V   0.97f   // V at and below: 0 A
#define THROTTLE_FULL_V   2.55f   // V at and above: full current
#define THROTTLE_FAULT_V  3.25f   // V above: broken wire / short to 3.3 V -> 0 A, must be released to re-arm

// ---- drive (step 5): throttle = MAXIMUM SPEED, not current ----------------------------------------------
// Throttle 0..100 % -> target 0..SPEED_MAX_KMH. A P speed controller asks for motor current 0..CURRENT_LIMIT
// while the motor is slower than the target; never negative (no braking - the freewheel hub can't pass it on).
// The current rises at most CURRENT_RAMP_A_S (soft freewheel engagement) and drops at once. Released = 0 A,
// the motor spins down, the car rolls; once the motor stands still the DRV goes to COAST (all FETs off).
#define GEAR_RATIO        9.41f   // motor turns per wheel turn (2026-10-29: pinion 15 -> 22 teeth, 13.8 x 15/22; was 8:1, 13.8:1)
#define WHEEL_DIAMETER_M  0.508f  // outer diameter with tyre (20" is often 0.48-0.50) <- CONFIRM
#define SPEED_MAX_KMH     40.0f   // throttle 100 %
#define MOTOR_DIRECTION   1       // +1 / -1: which way is forward (+1 = normal again with the new gearing 2026-10-26;
                                  //   was -1 with 8:1; live: "R1" normal, "R0" reversed, at standstill)
#define SPEED_P_A_PER_KMH 2.0f    // A per km/h too slow (5 km/h too slow = 10 A); P only -> holds ~0.5 km/h low
#define CURRENT_RAMP_A_S  2.0f    // A/s the current may rise
// Launch curve (2026-10-29): the current limit follows the MOTOR speed, linear between these points, flat after
// the last one: strong pull from standstill, efficient acceleration and cruising later. "L" still caps all of it.
// Uses motor speed on purpose: re-engaging the freewheel while rolling, the motor spins up from 0 almost without
// load and reaches the wheel speed with the low limit already active -> soft engagement.
#define LAUNCH_CURVE      1       // 1 = speed-dependent limit below (start limit = CURRENT_MAX), 0 = flat limit
#define LAUNCH_POINTS     3
#define LAUNCH_KMH        { 0.0f,  7.0f, 15.0f }   // km/h (motor speed)
#define LAUNCH_AMPS       { 20.0f, 15.0f, 10.0f }  // A at those speeds (20 A only for a few seconds per start; 10 A floor)
#define LAUNCH_RAMP_A_S   5.0f    // A/s rise below the 2nd point (so the 20 A arrive during the launch)
// Clutch take-up (2026-10-29): the clutch engages hard after ~1/3 turn of slack. Throttle pressed with the motor
// (almost) stopped -> for TAKEUP_MS the motor only creeps at TAKEUP_KMH with at most TAKEUP_AMPS (enough for the
// ~2 A cogging, too little to slam the clutch), whatever the pedal does; then the launch curve takes over.
#define TAKEUP_MS         1000    // ms
#define TAKEUP_BELOW_KMH  2.0f    // only when the motor is slower than this at the moment the throttle is pressed
#define TAKEUP_KMH        1.0f    // creep speed (motor, km/h equivalent; ~100 rpm at 9.41:1)
#define TAKEUP_AMPS       4.0f    // A max during take-up
#define TAKEUP_P_A_PER_KMH 4.0f   // creep controller: 4 A at standstill, 0 A at TAKEUP_KMH
// Stall guard: at least STALL_CURRENT_A asked for while the motor turns slower than STALL_SPEED_RADS for
// STALL_TIME_MS -> current 0, "STALL", throttle must be released to re-arm. Blocked (brake held) all power is
// heat, concentrated in one phase's FETs, and the rotor buzzes on a hall edge. A start on the flat needs < 2 A.
#define STALL_CURRENT_A   8.0f    // A (was 5, raised to 8 on 2026-10-21 by request)
#define STALL_SPEED_RADS  3.0f    // motor rad/s (~0.35 km/h at 8:1, 0.508 m)
#define STALL_TIME_MS     3000
// Uq needed per rad/s of motor speed, for the speed target limit. Coasting (Iq ~ 0): 6x PWM board #1 0.045;
// 3x PWM + IDRIVE 50/100 board #2 0.048 (8.33 V @ 172 rad/s, 4.44 V @ 93) - the slower edges cost voltage.
// 0.050 + SPEED_V_RESERVE keep the target clearly below the cap: at the cap the current loop saturated and
// whistled (board #2, ~200 rad/s at a 10 V cap, 2026-10-20).
#define KE_V_PER_RADS     0.050f
#define SPEED_V_RESERVE   1.5f    // V kept free below the cap (winding resistance at 10 A + margin)

// ---- temperature protection (steps 3-5): hard stop, no derating ---------------------------------------------
#define FET_TEMP_STOP     90.0f   // C, NTC TH1 on the board next to Q4
#define MOTOR_TEMP_STOP  100.0f   // C, motor NTC (J8 pin 6)

// ---- gate driver (DRV8353RS) --------------------------------------------------------------------
// 6-PWM on TIM1 complementary pairs: phase A = CH3/CH3N, B = CH2/CH2N, C = CH1/CH1N
#define PIN_INHA   PA10
#define PIN_INLA   PB15
#define PIN_INHB   PA9
#define PIN_INLB   PB14
#define PIN_INHC   PA8
#define PIN_INLC   PB13
#define PIN_DRV_EN     PA11       // DRV8353 ENABLE (driven by us, not by SimpleFOC: going low resets its registers)
#define PIN_DRV_FAULT  PB10       // nFAULT, open drain, low = fault
#define PIN_DRV_SCK    PB3
#define PIN_DRV_MISO   PB4        // DRV SDO
#define PIN_DRV_MOSI   PB5        // DRV SDI
#define PIN_DRV_CS     PB11       // DRV nSCS

// ---- current sense: DRV8353 amplifiers on 2 mOhm shunts, all on ADC2 ------------------------------
#define PIN_SOA    PA4            // SOA -> R57 -> PA4 (CN7_32)  note: A and C are NOT in pin order
#define PIN_SOB    PA1            // SOB -> R56 -> PA1 (CN7_30)
#define PIN_SOC    PA0            // SOC -> R55 -> PA0 (CN7_28)
#define SHUNT_OHMS 0.002f
#define CSA_GAIN   20.0f          // must match the CSA_GAIN written in drv8353.h

// ---- sensors ------------------------------------------------------------------------------------
#define PIN_HALL_A PB2
#define PIN_HALL_B PB1
#define PIN_HALL_C PC4            // 4.7k pull-ups to 3.3 V on the board
#define PIN_VBUS   PC3            // divider 383k / 9.76k
#define PIN_NTC    PA6            // 10k pull-up to 3.3 V, NTC 10k B3380 to GND (next to Q4)       ADC2_IN3
#define PIN_MOT_TEMP PA7          // motor sensor: 2.2k pull-up to 3.3 V, 1k series, 100 nF         ADC2_IN4
#define PIN_THROTTLE PC1
#define PIN_LED    PB7

#define VBUS_DIVIDER ((383.0f + 9.76f) / 9.76f)
// Bus voltage, temperatures and throttle: read with ain::... from analog_in.h, never with analogRead().
