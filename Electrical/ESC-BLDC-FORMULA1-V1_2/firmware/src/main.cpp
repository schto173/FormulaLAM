// ESC HAT bring-up with SimpleFOC. The step is chosen by the PlatformIO environment (-D STAGE=n):
//   1 drv_check    DRV8353 answers on SPI, settings applied, bus voltage, FET + motor temperature (no motor needed)
//   2 hall_test    turn the wheel by hand: hall states, sequence check, pole-pair count (no PWM)
//   3 open_loop    motor spins without sensors (low voltage)
//   4 closed_loop  hall alignment + FOC in voltage mode
//   5 foc_current  FOC with the low-side current sensing
// Serial monitor 115200 baud over the Nucleo USB. In steps 3-5 type e.g. "T2" + Enter to set the target.
#include <Arduino.h>
#include <SimpleFOC.h>
#include "board.h"
#include "drv8353.h"
#include "analog_in.h"

#ifndef STAGE
#define STAGE 1
#endif

static void banner(const char *name) {
  Serial.println();
  Serial.print("=== ESC bring-up step "); Serial.print(STAGE); Serial.print(": "); Serial.println(name);
}

static void blink() {
  static uint32_t t = 0;
  if (millis() - t > 500) { t = millis(); digitalWrite(PIN_LED, !digitalRead(PIN_LED)); }
}

// ================================================================================================
#if STAGE == 1   // DRV8353 check
void setup() {
  Serial.begin(115200);
  delay(1500);
  pinMode(PIN_LED, OUTPUT);
  banner("DRV8353 check");
  if (!ain::begin(false)) Serial.println("ADC2 init FAILED");
  Serial.print("bus voltage: "); Serial.print(ain::busVolts(), 1); Serial.println(" V");
  bool ok = drv::begin(Serial);
  Serial.println(ok ? "SPI OK - the DRV8353 is alive and configured"
                    : "SPI FAILED - check the supply, EN (PA11) and the SPI wiring");
  drv::printFaults(Serial);
  float r = ain::motorSensorOhms();
  Serial.print("motor temperature sensor (J8 pin 6): "); Serial.print(r, 0); Serial.print(" ohm -> ");
  Serial.println(ain::motorSensorGuess(r));
  Serial.println("  (valid at ROOM temperature; then set MOTOR_TEMP_SENSOR in include/board.h)");
}

void loop() {
  blink();
  static uint32_t t = 0;
  if (millis() - t > 1000) {
    t = millis();
    Serial.print("Vbus "); Serial.print(ain::busVolts(), 1);
    Serial.print(" V | FET "); Serial.print(ain::fetCelsius(), 1);
    Serial.print(" C | motor sensor "); Serial.print(ain::motorSensorOhms(), 0); Serial.print(" ohm");
    float tm = ain::motorCelsius();
    if (!isnan(tm)) { Serial.print(" = "); Serial.print(tm, 1); Serial.print(" C"); }
    Serial.print(" | throttle "); Serial.print(ain::throttle01() * 100.0f, 0); Serial.print(" %");
    Serial.print(" | nFAULT "); Serial.print(drv::faultActive() ? "LOW (fault) | " : "high | ");
    drv::printFaults(Serial);
  }
}

// ================================================================================================
#elif STAGE == 2   // hall test, no PWM
static volatile uint32_t transitions = 0;
static volatile uint8_t lastState = 0;
static uint8_t hallState() {
  return (digitalRead(PIN_HALL_A) ? 1 : 0) | (digitalRead(PIN_HALL_B) ? 2 : 0) | (digitalRead(PIN_HALL_C) ? 4 : 0);
}
static void onHall() {
  uint8_t s = hallState();
  if (s != lastState) { transitions++; lastState = s; }
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  pinMode(PIN_LED, OUTPUT);
  banner("hall test - turn the wheel slowly by hand");
  pinMode(PIN_HALL_A, INPUT); pinMode(PIN_HALL_B, INPUT); pinMode(PIN_HALL_C, INPUT);   // external pull-ups
  lastState = hallState();
  attachInterrupt(digitalPinToInterrupt(PIN_HALL_A), onHall, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_HALL_B), onHall, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_HALL_C), onHall, CHANGE);
  Serial.println("Good: states 1..6 only, each change moves to a neighbour, 0 and 7 never appear.");
  Serial.println("Turn the wheel exactly ONE turn, then type 'r' to reset the counter before the next try.");
  Serial.println("pole pairs = transitions per turn / 6");
}

void loop() {
  blink();
  if (Serial.available() && Serial.read() == 'r') { transitions = 0; Serial.println("counter reset"); }
  static uint8_t shown = 0xFF;
  uint8_t s = hallState();
  if (s != shown) {
    shown = s;
    Serial.print("state "); Serial.print(s);
    Serial.print("  (A B C = "); Serial.print(s & 1); Serial.print(' '); Serial.print((s >> 1) & 1);
    Serial.print(' '); Serial.print((s >> 2) & 1); Serial.print(")");
    if (s == 0 || s == 7) Serial.print("  <-- INVALID: check hall wiring / supply");
    Serial.print("  transitions "); Serial.print(transitions);
    Serial.print("  -> pole pairs if this was one turn: "); Serial.println(transitions / 6.0f, 1);
  }
}

// ================================================================================================
#else   // steps 3-5: SimpleFOC
BLDCMotor motor = BLDCMotor(POLE_PAIRS);
BLDCDriver6PWM driver = BLDCDriver6PWM(PIN_INHA, PIN_INLA, PIN_INHB, PIN_INLB, PIN_INHC, PIN_INLC);  // EN is ours
HallSensor sensor = HallSensor(PIN_HALL_A, PIN_HALL_B, PIN_HALL_C, POLE_PAIRS);
LowsideCurrentSense current_sense = LowsideCurrentSense(SHUNT_OHMS, CSA_GAIN, PIN_SOA, PIN_SOB, PIN_SOC);
Commander command = Commander(Serial);

void doA() { sensor.handleA(); }
void doB() { sensor.handleB(); }
void doC() { sensor.handleC(); }
void doTarget(char *cmd) { command.scalar(&motor.target, cmd); }
void doMotor(char *cmd)  { command.motor(&motor, cmd); }

static bool pwmReady = false;
static void halt(const char *why) {
  if (pwmReady) motor.disable();
  digitalWrite(PIN_DRV_EN, LOW);
  Serial.print("STOPPED: "); Serial.println(why);
  drv::printFaults(Serial);
  while (true) { digitalWrite(PIN_LED, !digitalRead(PIN_LED)); delay(100); }
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  pinMode(PIN_LED, OUTPUT);
  SimpleFOCDebug::enable(&Serial);
#if STAGE == 3
  banner("open loop (no sensors)");
#elif STAGE == 4
  banner("closed loop, halls, voltage mode");
#else
  banner("FOC with current sensing");
#endif

  if (!ain::begin(false)) halt("ADC2 init failed");
  float vbus = ain::busVolts();
  Serial.print("bus voltage: "); Serial.print(vbus, 1); Serial.println(" V");
  if (!(vbus >= 9.0f)) halt("bus voltage below 9 V (DRV8353 minimum) - check the supply");
  if (!drv::begin(Serial)) halt("DRV8353 does not answer on SPI - run step 1");

  driver.voltage_power_supply = vbus;
  driver.voltage_limit = VOLTAGE_LIMIT;
  driver.pwm_frequency = 20000;
  driver.dead_zone = 0.01f;                          // 500 ns at 20 kHz, on top of the DRV's 100 ns
  if (!driver.init()) halt("PWM driver init failed");
  motor.linkDriver(&driver);
  pwmReady = true;
  motor.voltage_limit = VOLTAGE_LIMIT;
  motor.velocity_limit = 50;                         // rad/s

#if STAGE == 3
  motor.controller = MotionControlType::velocity_openloop;
  motor.target = 2;                                  // rad/s, change with T
  motor.init();
#else
  sensor.pullup = Pullup::USE_EXTERN;
  sensor.init();
  sensor.enableInterrupts(doA, doB, doC);
  motor.linkSensor(&sensor);
  motor.voltage_sensor_align = ALIGN_VOLTAGE;
  motor.controller = MotionControlType::torque;
  motor.target = 0;
#if STAGE == 4
  motor.torque_controller = TorqueControlType::voltage;   // target in volts
  motor.init();
#else
  motor.torque_controller = TorqueControlType::foc_current;  // target in amps
  motor.current_limit = CURRENT_LIMIT;
  current_sense.linkDriver(&driver);
  motor.init();
  ain::end();                                        // hand ADC2 back: SimpleFOC sets it up from scratch
  if (!current_sense.init()) halt("current sense init failed");
  motor.linkCurrentSense(&current_sense);
  if (!ain::begin(true)) halt("ADC2 not running after current sense init");   // from here on: no analogRead()!
#endif
  if (!motor.initFOC()) halt("initFOC failed - see the messages above (hall order / pole pairs / align voltage)");
#endif

  command.add('T', doTarget, "target");
  command.add('M', doMotor, "motor");
  motor.useMonitoring(Serial);
  motor.monitor_downsample = 200;
  motor.monitor_variables = _MON_TARGET | _MON_VEL | _MON_VOLT_Q | _MON_CURR_Q;
  Serial.println("ready. T<value> sets the target, M for all motor commands (e.g. MVP0.2 velocity P gain).");
}

// Temperatures every 200 ms: full power below *_WARN, linear derating up to *_MAX, stop at *_MAX.
static void temperatureTask() {
  static uint32_t t = 0, tp = 0;
  if (millis() - t < 200) return;
  t = millis();
  float tf = ain::fetCelsius(), tm = ain::motorCelsius();
  if (isnan(tf)) halt("FET temperature sensor fault (TH1 open or shorted)");
  float k = ain::derate(tf, FET_TEMP_WARN, FET_TEMP_MAX);
#if MOTOR_TEMP_SENSOR != MOTOR_TEMP_UNKNOWN
  k = min(k, ain::derate(tm, MOTOR_TEMP_WARN, MOTOR_TEMP_MAX));
#endif
  if (k <= 0.0f) halt("over-temperature");
#if STAGE == 5
  float lim = CURRENT_LIMIT * k;                     // A
  motor.current_limit = lim;
#else
  float lim = VOLTAGE_LIMIT * k;                     // V
  motor.voltage_limit = lim;
#endif
  motor.PID_velocity.limit = lim;                    // velocity mode (MC1): its output is the torque command
  if (motor.controller == MotionControlType::torque) motor.target = constrain(motor.target, -lim, lim);
  if (millis() - tp >= 1000) {
    tp = millis();
    Serial.print("# FET "); Serial.print(tf, 1); Serial.print(" C | motor ");
    if (isnan(tm)) { Serial.print(ain::motorSensorOhms(), 0); Serial.print(" ohm (type not set)"); }
    else { Serial.print(tm, 1); Serial.print(" C"); }
    Serial.print(" | Vbus "); Serial.print(ain::busVolts(), 1); Serial.print(" V | power "); Serial.print(k * 100.0f, 0);
    Serial.println(" %");
  }
}

void loop() {
  motor.loopFOC();
  motor.move();
  command.run();
  motor.monitor();
  temperatureTask();
  if (drv::faultActive()) halt("DRV8353 nFAULT");
  blink();
}
#endif
