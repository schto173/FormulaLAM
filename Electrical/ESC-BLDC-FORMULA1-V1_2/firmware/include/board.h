// ESC HAT pin map and board constants (NUCLEO-G474RE). Matches the KiCad schematic.
#pragma once
#include <Arduino.h>

// ---- motor: SET THESE FOR YOUR MOTOR ------------------------------------------------------------
#define POLE_PAIRS        7       // step 2 (hall test) tells you the real value
#define ALIGN_VOLTAGE     1.5f    // V used to align the halls in step 4 (raise slowly if it does not move)
#define VOLTAGE_LIMIT     3.0f    // V max applied to the motor in steps 3-4 (start low!)
#define CURRENT_LIMIT     5.0f    // A max in step 5 (start low!)

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
#define PIN_SOA    PA0
#define PIN_SOB    PA1
#define PIN_SOC    PA4
#define SHUNT_OHMS 0.002f
#define CSA_GAIN   20.0f          // must match the CSA_GAIN written in drv8353.h

// ---- sensors ------------------------------------------------------------------------------------
#define PIN_HALL_A PB2
#define PIN_HALL_B PB1
#define PIN_HALL_C PC4            // 4.7k pull-ups to 3.3 V on the board
#define PIN_VBUS   PC3            // divider 383k / 9.76k
#define PIN_NTC    PA6            // 10k pull-up to 3.3 V, NTC 10k B3380 to GND (next to Q4)
#define PIN_THROTTLE PC1
#define PIN_LED    PB7

#define VBUS_DIVIDER ((383.0f + 9.76f) / 9.76f)

inline float readBusVoltage() {       // only call BEFORE SimpleFOC current sensing is started
  analogReadResolution(12);
  float v = 0;
  for (int i = 0; i < 16; i++) v += analogRead(PIN_VBUS);
  return v / 16.0f / 4095.0f * 3.3f * VBUS_DIVIDER;
}

inline float readFetTemperature() {   // degC, same caveat as above
  analogReadResolution(12);
  float v = analogRead(PIN_NTC) / 4095.0f * 3.3f;
  if (v < 0.01f || v > 3.29f) return NAN;          // open or shorted
  float r = 10000.0f * v / (3.3f - v);
  return 1.0f / (1.0f / 298.15f + logf(r / 10000.0f) / 3380.0f) - 273.15f;
}
