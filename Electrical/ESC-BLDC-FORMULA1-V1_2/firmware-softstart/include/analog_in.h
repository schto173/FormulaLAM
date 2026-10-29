// Slow analog inputs (FET temperature, motor temperature, throttle, bus voltage) read on ADC2's *regular* group with
// direct register access, so they can run alongside SimpleFOC's low-side current sensing.
//
// Why not analogRead(): the STM32 Arduino core de-initialises the ADC after every analogRead() and its
// HAL_ADC_MspDeInit() force-resets ADC1+ADC2 together (shared ADC12 clock/reset on the G474). That wipes the
// injected-channel setup SimpleFOC uses for PA0/PA1/PA4 on ADC2. So once current sensing runs, NEVER call
// analogRead() - on any pin. SimpleFOC only uses ADC2's injected group (timer-triggered); the regular group is free,
// and an injected trigger simply pre-empts a regular conversion, so current sampling stays on time.
#pragma once
#include <Arduino.h>
#include "stm32yyxx_ll_adc.h"
#include "board.h"

namespace ain {

// ADC2 channel numbers of our pins (variant PeripheralPins.c): PA6 = IN3, PA7 = IN4, PC1 = IN7, PC3 = IN9
static const uint32_t CH_FET_NTC  = LL_ADC_CHANNEL_3;
static const uint32_t CH_MOT_TEMP = LL_ADC_CHANNEL_4;
static const uint32_t CH_THROTTLE = LL_ADC_CHANNEL_7;
static const uint32_t CH_VBUS     = LL_ADC_CHANNEL_9;

static bool ready = false;

// simplefocOwnsAdc2 = false: configure ADC2 ourselves (every step at start-up; in step 5 call end() before
//                            current_sense.init(), so SimpleFOC finds ADC2 switched off and sets it up itself).
// simplefocOwnsAdc2 = true : call AFTER current_sense.init(); ADC2 is already configured and enabled by SimpleFOC.
inline bool begin(bool simplefocOwnsAdc2) {
  pinMode(PIN_NTC, INPUT_ANALOG); pinMode(PIN_MOT_TEMP, INPUT_ANALOG);
  pinMode(PIN_THROTTLE, INPUT_ANALOG); pinMode(PIN_VBUS, INPUT_ANALOG);
  if (!simplefocOwnsAdc2) {
    static ADC_HandleTypeDef h = {};
    __HAL_RCC_ADC12_CLK_ENABLE();
    h.Instance = ADC2;                                   // same base settings SimpleFOC uses
    h.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
    h.Init.Resolution = ADC_RESOLUTION_12B;
    h.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    h.Init.GainCompensation = 0;
    h.Init.ScanConvMode = ADC_SCAN_DISABLE;
    h.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
    h.Init.LowPowerAutoWait = DISABLE;
    h.Init.ContinuousConvMode = DISABLE;
    h.Init.NbrOfConversion = 1;
    h.Init.DiscontinuousConvMode = DISABLE;
    h.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    h.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    h.Init.DMAContinuousRequests = DISABLE;
    h.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
    h.Init.OversamplingMode = DISABLE;
    if (HAL_ADC_Init(&h) != HAL_OK) return false;
    HAL_ADCEx_Calibration_Start(&h, ADC_SINGLE_ENDED);
    LL_ADC_ClearFlag_ADRDY(ADC2);
    LL_ADC_Enable(ADC2);
    uint32_t t0 = millis();
    while (!LL_ADC_IsActiveFlag_ADRDY(ADC2)) if (millis() - t0 > 10) return false;
  }
  if (!LL_ADC_IsEnabled(ADC2)) return false;
  ready = true;
  return true;
}

// switch ADC2 off again after begin(false) (step 5: right before current_sense.init())
inline void end() {
  ready = false;
  if (!LL_ADC_IsEnabled(ADC2)) return;
  if (LL_ADC_REG_IsConversionOngoing(ADC2)) LL_ADC_REG_StopConversion(ADC2);
  uint32_t t0 = millis();
  while (LL_ADC_REG_IsStopConversionOngoing(ADC2) && millis() - t0 < 10) {}
  LL_ADC_Disable(ADC2);
  while (LL_ADC_IsEnabled(ADC2) && millis() - t0 < 10) {}
  LL_ADC_ClearFlag_ADRDY(ADC2);
}

// one regular conversion on ADC2, 12 bit; 0xFFFF on timeout
inline uint16_t read(uint32_t ch) {
  if (!ready) return 0xFFFF;
  if (LL_ADC_REG_IsConversionOngoing(ADC2)) return 0xFFFF;
  // sampling time may only be changed while nothing converts; the 100 nF filter caps make even a short time fine
  if (!LL_ADC_INJ_IsConversionOngoing(ADC2)) LL_ADC_SetChannelSamplingTime(ADC2, ch, LL_ADC_SAMPLINGTIME_47CYCLES_5);
  LL_ADC_REG_SetSequencerRanks(ADC2, LL_ADC_REG_RANK_1, ch);
  LL_ADC_ClearFlag_EOC(ADC2);
  LL_ADC_REG_StartConversion(ADC2);
  uint32_t t0 = micros();
  while (!LL_ADC_IsActiveFlag_EOC(ADC2)) if (micros() - t0 > 200) return 0xFFFF;
  return LL_ADC_REG_ReadConversionData12(ADC2);
}

inline float avg(uint32_t ch, int n = 8) {
  uint32_t s = 0; int k = 0;
  for (int i = 0; i < n; i++) { uint16_t v = read(ch); if (v != 0xFFFF) { s += v; k++; } }
  return k ? (float)s / k : NAN;
}

// ---- conversions --------------------------------------------------------------------------------------------
// divider: pull-up to 3.3 V (same supply as the ADC reference, so the result is ratiometric) -> sensor -> GND
inline float sensorOhms(float raw, float pullup) {
  if (isnan(raw) || raw >= 4090.0f) return INFINITY;   // open
  return pullup * raw / (4095.0f - raw);
}
inline float ntcCelsius(float r, float r25, float beta) {
  if (!(r > 1.0f) || isinf(r)) return NAN;
  return 1.0f / (1.0f / 298.15f + logf(r / r25) / beta) - 273.15f;
}
inline float pt1000Celsius(float r) {                 // Callendar-Van Dusen, T >= 0 C
  const float A = 3.9083e-3f, B = -5.775e-7f;
  float d = A * A - 4.0f * B * (1.0f - r / 1000.0f);
  return d < 0 ? NAN : (-A + sqrtf(d)) / (2.0f * B);
}
inline float kty84Celsius(float r) {                  // KTY84/130 table (NXP), linear interpolation
  static const float T[] = {-40, -30, -20, -10, 0, 10, 20, 25, 30, 40, 50, 60, 70, 80, 90, 100,
                            110, 120, 130, 140, 150, 160, 170, 180, 190, 200};
  static const float R[] = {359, 391, 424, 460, 498, 538, 581, 603, 626, 672, 722, 773, 826, 882, 940, 1000,
                            1062, 1127, 1194, 1262, 1334, 1407, 1482, 1560, 1640, 1722};
  const int n = sizeof(T) / sizeof(T[0]);
  if (r < R[0] || r > R[n - 1]) return NAN;
  for (int i = 1; i < n; i++)
    if (r <= R[i]) return T[i - 1] + (T[i] - T[i - 1]) * (r - R[i - 1]) / (R[i] - R[i - 1]);
  return NAN;
}

inline float fetCelsius() {                             // TH1: 10k NTC B3380 with 10k pull-up
  float r = sensorOhms(avg(CH_FET_NTC), 10000.0f);
  if (r < 50.0f || r > 500000.0f) return NAN;          // short or open
  return ntcCelsius(r, 10000.0f, 3380.0f);
}
inline float motorSensorOhms() { return sensorOhms(avg(CH_MOT_TEMP), 2200.0f); }
inline float motorCelsius() {
  float r = motorSensorOhms();
  if (r < 50.0f || r > 500000.0f) return NAN;
#if MOTOR_TEMP_SENSOR == MOTOR_TEMP_NTC10K
  return ntcCelsius(r, 10000.0f, MOTOR_NTC_BETA);
#elif MOTOR_TEMP_SENSOR == MOTOR_TEMP_KTY84
  return kty84Celsius(r);
#elif MOTOR_TEMP_SENSOR == MOTOR_TEMP_PT1000
  return pt1000Celsius(r);
#else
  return NAN;                                          // type not set yet
#endif
}
// what the resistance at ROOM TEMPERATURE suggests
inline const char *motorSensorGuess(float r) {
  if (isinf(r) || r > 500000.0f) return "nothing connected (open)";
  if (r < 50.0f) return "short circuit";
  if (r > 400.0f && r < 800.0f) return "probably KTY84 (~600 ohm at 25 C)";
  if (r >= 800.0f && r < 1300.0f) return "probably PT1000 (1000 ohm at 0 C, ~1100 at 25 C)";
  if (r > 4000.0f && r < 30000.0f) return "probably NTC 10k (10 kohm at 25 C)";
  return "unknown type - measure at two temperatures";
}
inline float busVolts(int n = 8) { float r = avg(CH_VBUS, n); return isnan(r) ? NAN : r / 4095.0f * 3.3f * VBUS_DIVIDER; }
inline float throttleVolts() { float r = avg(CH_THROTTLE, 4); return isnan(r) ? NAN : r / 4095.0f * 3.3f; }
inline float throttle01() { float r = avg(CH_THROTTLE, 4); return isnan(r) ? NAN : r / 4095.0f; }

// 1.0 below warn, falling linearly to 0.0 at max (NaN temperature = no derating)
inline float derate(float t, float warn, float max) {
  if (isnan(t) || t <= warn) return 1.0f;
  if (t >= max) return 0.0f;
  return (max - t) / (max - warn);
}

}  // namespace ain
