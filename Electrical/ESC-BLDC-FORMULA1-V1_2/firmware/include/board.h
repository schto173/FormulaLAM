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

// ---- motor temperature sensor (white wire, J8 pin 6) ------------------------------------------------
// Type unknown so far: step 1 prints the sensor resistance and a guess. Set it here, then steps 3-5 show
// the motor temperature in C and derate / stop on over-temperature.
#define MOTOR_TEMP_UNKNOWN 0
#define MOTOR_TEMP_NTC10K  1
#define MOTOR_TEMP_KTY84   2
#define MOTOR_TEMP_PT1000  3
#define MOTOR_TEMP_SENSOR  MOTOR_TEMP_NTC10K   // measured 11.85 kOhm at ~21 C room temperature (2026-10-14)
#define MOTOR_NTC_BETA     3950.0f // only for NTC10K; 3950 is the usual motor NTC (check the motor's spec)

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
