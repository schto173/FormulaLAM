// ESC HAT pin map and board constants (NUCLEO-G474RE). Matches the KiCad schematic.
#pragma once
#include <Arduino.h>

// ---- motor: SET THESE FOR YOUR MOTOR ------------------------------------------------------------
#define POLE_PAIRS        7       // step 2 (hall test) tells you the real value
// The 7070 has only ~0.054 Ohm + ~0.01 Ohm FET/shunt/wire, and at standstill nothing but that resistance
// limits the current: 0.5 V ~ 8-10 A in the windings, 3 V would be ~50 A and collapse a current-limited
// bench supply below the 16.8 V UVLO of the 5 V regulator (Nucleo resets).
#define ALIGN_VOLTAGE     0.4f    // V used to align the halls in step 4 (raise in 0.1 V steps if it does not move)
#define VOLTAGE_LIMIT     0.5f    // V max applied to the motor in steps 3-5 (in step 5 this also caps the speed)
#define CURRENT_LIMIT     5.0f    // A max in step 5 (start low!)
#define FOC_VOLTAGE_LIMIT 1.0f    // V the current controller may apply in step 5 (caps the no-load speed at
                                  // ~15 rad/s; change live with "MLU<volts>")
// SpaceVectorPWM: max phase voltage Vbus/sqrt(3) instead of Vbus/2 (+15.5 %): 5000 rpm needs ~26 V per phase
// -> SinePWM only above ~52 V bus, SVPWM above ~45 V (most of a 14S battery). Same switching losses.
#define USE_SVPWM         1
// MLU is refused above this fraction of Vbus: keeps a few % margin so the low-side shunts still get time
// to measure (SVPWM max 0.577, SinePWM max 0.5)
#define MLU_MAX_FRACTION  (USE_SVPWM ? 0.55f : 0.45f)
#define HALL_SMOOTHING    1       // 1 = SmoothingSensor interpolates between the 60-degree hall steps (steps 4-5)
// Current PI from "MFP0.4" + "MFC200" on the 7070 (2026-10-14: R 0.16 Ohm incl. FETs/wires, Ld 50 uH, Lq 70 uH)
#define CURR_Q_P          0.09f
#define CURR_D_P          0.06f
#define CURR_I            200.0f

// ---- motor temperature sensor (white wire, J8 pin 6) ------------------------------------------------
// Type unknown so far: step 1 prints the sensor resistance and a guess. Set it here, then steps 3-5 show
// the motor temperature in C and derate / stop on over-temperature.
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
#define BUS_UV_FULL       (BATTERY_CELLS * 3.20f)         // 12S: 38.4 V - full current above
#define BUS_UV_ZERO       (BATTERY_CELLS * 3.00f)         // 12S: 36.0 V - current derated linearly to 0 (coast)
#define ALIGN_V_BUS       0.6f    // V for hall + current-sense alignment: the dead time eats ~0.2 V at 50 V
#else
#define BUS_OV_LIMIT      40.0f   // V, bench supply (max 30 V)
#define BUS_START_MAX     35.0f
#define BUS_UV_FULL       0.0f    // no under-voltage derating on the bench
#define BUS_UV_ZERO       0.0f
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
#define THROTTLE_RAMP_A_S 10.0f   // A/s the current may rise (it drops immediately when released)

// ---- temperature protection (steps 3-5): full power below WARN, linear derating, motor stops at MAX ----
#define FET_TEMP_WARN     80.0f   // C, NTC TH1 on the board next to Q4
#define FET_TEMP_MAX     100.0f
#define MOTOR_TEMP_WARN   90.0f   // C, only active once MOTOR_TEMP_SENSOR is set
#define MOTOR_TEMP_MAX   110.0f

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
