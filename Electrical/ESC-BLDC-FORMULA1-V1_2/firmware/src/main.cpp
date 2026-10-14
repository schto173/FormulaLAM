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
    Serial.print(" | throttle "); Serial.print(ain::throttleVolts(), 2); Serial.print(" V");
    Serial.print(" | nFAULT "); Serial.print(drv::faultActive() ? "LOW (fault) | " : "high | ");
    drv::printFaults(Serial);
  }
}

// ================================================================================================
#elif STAGE == 2   // hall test, no PWM
// Counts signed steps: the hall states follow the sequence 1-3-2-6-4-5 (seen on the 7070), so each change is
// one step forward (+1) or back (-1). Wiggling the wheel by hand cancels out; only the net rotation remains.
static volatile uint32_t transitions = 0, errors = 0;
static volatile int32_t net = 0;
static volatile uint8_t lastState = 0;
static const int8_t ORDER[8] = {-1, 0, 2, 1, 4, 5, 3, -1};   // position of state s in 1-3-2-6-4-5, -1 = invalid
static uint8_t hallState() {
  return (digitalRead(PIN_HALL_A) ? 1 : 0) | (digitalRead(PIN_HALL_B) ? 2 : 0) | (digitalRead(PIN_HALL_C) ? 4 : 0);
}
static void onHall() {
  uint8_t s = hallState();
  if (s == lastState) return;
  transitions++;
  int8_t a = ORDER[lastState], b = ORDER[s];
  int8_t d = (a < 0 || b < 0) ? 0 : (b - a + 6) % 6;
  if (d == 1) net++;
  else if (d == 5) net--;
  else errors++;                                   // invalid state or a skipped step
  lastState = s;
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
  Serial.println("Type 'r' + Enter to reset, then turn the wheel exactly ONE turn (wiggling back and forth is fine).");
  Serial.println("pole pairs = net steps per turn / 6");
}

void loop() {
  blink();
  if (Serial.available() && Serial.read() == 'r') {
    noInterrupts(); transitions = 0; net = 0; errors = 0; interrupts();
    Serial.println("counter reset");
  }
  static uint8_t shown = 0xFF;
  uint8_t s = hallState();
  if (s != shown) {
    shown = s;
    Serial.print("state "); Serial.print(s);
    Serial.print("  (A B C = "); Serial.print(s & 1); Serial.print(' '); Serial.print((s >> 1) & 1);
    Serial.print(' '); Serial.print((s >> 2) & 1); Serial.print(")");
    if (s == 0 || s == 7) Serial.print("  <-- INVALID: check hall wiring / supply");
    int32_t n = net;
    Serial.print("  net steps "); Serial.print(n);
    Serial.print("  -> pole pairs if this was one turn: "); Serial.print(abs(n) / 6.0f, 2);
    Serial.print("  (all changes "); Serial.print(transitions);
    Serial.print(", errors "); Serial.print(errors); Serial.println(")");
  }
}

// ================================================================================================
#else   // steps 3-5: SimpleFOC
BLDCMotor motor = BLDCMotor(POLE_PAIRS);
BLDCDriver6PWM driver = BLDCDriver6PWM(PIN_INHA, PIN_INLA, PIN_INHB, PIN_INLB, PIN_INHC, PIN_INLC);  // EN is ours
HallSensor sensor = HallSensor(PIN_HALL_A, PIN_HALL_B, PIN_HALL_C, POLE_PAIRS);
#if HALL_SMOOTHING
#include "encoders/smoothing/SmoothingSensor.h"
// predicts the angle between hall edges from the speed (max one 60-degree step ahead); the HallSensor
// constructor also sets phase_correction = -30 deg el., because a hall edge marks the START of a sector
SmoothingSensor smooth = SmoothingSensor(sensor, motor);
#endif
LowsideCurrentSense current_sense = LowsideCurrentSense(SHUNT_OHMS, CSA_GAIN, PIN_SOA, PIN_SOB, PIN_SOC);
Commander command = Commander(Serial);

void doA() { sensor.handleA(); }
void doB() { sensor.handleB(); }
void doC() { sensor.handleC(); }
void doTarget(char *cmd) { command.scalar(&motor.target, cmd); }
// "L<value>": limit before temperature derating - volts in steps 3-4, amps in step 5. Capped for the bench.
#if STAGE == 5
static float limitBase = CURRENT_LIMIT; static const float LIMIT_CAP = 15.0f;
#else
static float limitBase = VOLTAGE_LIMIT; static const float LIMIT_CAP = 1.5f;
#endif
void doLimit(char *cmd) { float v = limitBase; command.scalar(&v, cmd); limitBase = constrain(v, 0.0f, LIMIT_CAP); }
void doMotor(char *cmd) {
  // lowering the voltage limit below the back-EMF while spinning brakes regeneratively (see BUS_OV_LIMIT)
  if (cmd[0] == 'L' && cmd[1] == 'U' && cmd[2] && atof(cmd + 2) < motor.voltage_limit &&
      fabsf(motor.shaft_velocity) > NO_REGEN_SPEED) {
    Serial.println("refused: lowering MLU while spinning brakes into the supply - T0 / release, wait until it stops");
    return;
  }
  if (cmd[0] == 'L' && cmd[1] == 'U' && cmd[2] && atof(cmd + 2) > MLU_MAX_FRACTION * driver.voltage_power_supply) {
    Serial.print("refused: MLU max is "); Serial.print(MLU_MAX_FRACTION * driver.voltage_power_supply, 1);
    Serial.println(" V at this bus voltage (above it the phases clip and the current sensing fails)");
    return;
  }
  command.motor(&motor, cmd);
}

static bool pwmReady = false;
static void halt(const char *why) {
  if (pwmReady) motor.disable();
  digitalWrite(PIN_DRV_EN, LOW);
  Serial.print("STOPPED: "); Serial.println(why);
  drv::printFaults(Serial);
  while (true) { digitalWrite(PIN_LED, !digitalRead(PIN_LED)); delay(100); }
}

// Every 1 ms: bus over-voltage -> driver off (it then coasts; on the bench the back-EMF stays below the bus).
static void busTask() {
  static uint32_t t = 0;
  if (micros() - t < 1000) return;
  t = micros();
  float vb = ain::busVolts(1);
  if (vb > BUS_OV_LIMIT) halt("bus over-voltage - energy pumped back into a supply that cannot take it?");
}

// No regenerative braking: a torque target against the rotation (above NO_REGEN_SPEED) becomes 0 = coast.
static void noRegenTask() {
  if (motor.controller != MotionControlType::torque) return;
  if (motor.target * motor.shaft_velocity < 0.0f && fabsf(motor.shaft_velocity) > NO_REGEN_SPEED) {
    motor.target = 0.0f;
    static uint32_t tw = 0;
    if (millis() - tw > 1000) { tw = millis(); Serial.println("# no regen: target against the rotation -> 0, wait until it stops"); }
  }
}

#if STAGE == 5
// ---- throttle: 0..100 % -> 0..limitBase A, must be released once before it acts ("armed") ----------------
static bool thrOn = THROTTLE_ON_AT_START, thrArmed = false;
static float thrIdle = THROTTLE_IDLE_V, thrFull = THROTTLE_FULL_V, thrTarget = 0.0f, thrPct = 0.0f;
static void thrOff(const char *why) { thrOn = false; thrTarget = 0.0f; motor.target = 0.0f; Serial.println(why); }
// "A1" on, "A0" off, "AI" idle = now, "AF" full = now, "A" status
void doThrottle(char *cmd) {
  float v = ain::throttleVolts();
  switch (cmd[0]) {
    case '1': thrOn = true; thrArmed = false; thrTarget = 0.0f; motor.target = 0.0f;
              Serial.println("throttle ON - release it fully to arm"); break;
    case '0': thrOff("throttle OFF, target 0 - T commands work again"); break;
    // dead band: idle 0.1 V above the released reading (noise must not count as "pressed"), full 0.05 V below
    case 'I': thrIdle = v + 0.10f; Serial.print("released "); Serial.print(v, 2); Serial.print(" V -> idle = ");
              Serial.print(thrIdle, 2); Serial.println(" V"); break;
    case 'F': thrFull = v - 0.05f; Serial.print("pressed "); Serial.print(v, 2); Serial.print(" V -> full = ");
              Serial.print(thrFull, 2); Serial.println(" V"); break;
    default:  break;
  }
  if (thrFull < thrIdle + 0.2f) Serial.println("WARNING: full must be at least 0.2 V above idle - recalibrate (AI / AF)");
  Serial.print("throttle "); Serial.print(thrOn ? (thrArmed ? "ON, armed" : "ON, not armed (release it)") : "OFF");
  Serial.print(" | now "); Serial.print(v, 2); Serial.print(" V | idle "); Serial.print(thrIdle, 2);
  Serial.print(" V | full "); Serial.print(thrFull, 2); Serial.print(" V | max "); Serial.print(limitBase, 1);
  Serial.println(" A");
}
static void throttleTask() {
  static uint32_t t = 0;
  uint32_t now = millis();
  if (now - t < 10) return;
  float dt = min((now - t) * 1e-3f, 0.05f);
  t = now;
  if (!thrOn) return;
  float v = ain::throttleVolts();
  float x = 0.0f;
  if (isnan(v) || v > THROTTLE_FAULT_V || thrFull < thrIdle + 0.2f) {
    if (thrArmed) Serial.println("# throttle signal invalid (open / short to 3.3 V / bad calibration) -> 0 A");
    thrArmed = false;
  } else {
    x = constrain((v - thrIdle) / (thrFull - thrIdle), 0.0f, 1.0f);
  }
  thrPct = x * 100.0f;
  if (!thrArmed) {
    thrTarget = 0.0f; motor.target = 0.0f;
    if (x <= 0.0f && !isnan(v) && v <= THROTTLE_FAULT_V && thrFull >= thrIdle + 0.2f) {
      thrArmed = true; Serial.println("# throttle armed");
    }
    return;
  }
  float want = x * limitBase;                        // A; temperatureTask additionally clamps to the derated limit
  thrTarget = (want > thrTarget) ? min(want, thrTarget + THROTTLE_RAMP_A_S * dt) : want;
  motor.target = thrTarget;
}
#endif

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
  // NOT the motor limit: SimpleFOC centres the phase voltages at driver.voltage_limit / 2. With 0.5 V here the
  // phases swung 0..0.5 V (0-2 % duty, <1 us pulses eaten by the dead time) and the motor only jerked.
  driver.voltage_limit = vbus;                       // phases centred at Vbus/2; the motor limit is below
  driver.pwm_frequency = 20000;
  // 100 ns at 20 kHz, plus the DRV's own 100 ns dead time and VGS handshake (it never turns on both FETs of a
  // phase). Every ns of dead time costs voltage: 600 ns total was ~0.3 V of the 0.5 V we apply.
  driver.dead_zone = 0.002f;
  if (!driver.init()) halt("PWM driver init failed");
  motor.linkDriver(&driver);
  pwmReady = true;
  motor.voltage_limit = VOLTAGE_LIMIT;
  motor.velocity_limit = 50;                         // rad/s
#if USE_SVPWM
  motor.foc_modulation = FOCModulationType::SpaceVectorPWM;
#endif

#if STAGE == 3
  motor.controller = MotionControlType::velocity_openloop;
  motor.target = 2;                                  // rad/s, change with T
  motor.init();
#else
  sensor.pullup = Pullup::USE_EXTERN;
  sensor.init();
  sensor.enableInterrupts(doA, doB, doC);
#if HALL_SMOOTHING
  motor.linkSensor(&smooth);
#else
  motor.linkSensor(&sensor);
#endif
  motor.voltage_sensor_align = ALIGN_VOLTAGE;
  motor.controller = MotionControlType::torque;
  motor.target = 0;
#if STAGE == 4
  motor.torque_controller = TorqueControlType::voltage;   // target in volts
  motor.init();
#else
  motor.torque_controller = TorqueControlType::foc_current;  // target in amps
  motor.current_limit = CURRENT_LIMIT;
  motor.voltage_limit = FOC_VOLTAGE_LIMIT;           // also the current PI output limit (set before motor.init)
  // Current PI for a ~0.05 Ohm / tens-of-uH motor, measured with "MFP0.4" + "MFC200" (see board.h).
  // SimpleFOC's defaults (P 3, I 300) are meant for ~10 Ohm gimbal motors and would oscillate here.
  motor.PID_current_q.P = CURR_Q_P;
  motor.PID_current_d.P = CURR_D_P;
  motor.PID_current_q.I = motor.PID_current_d.I = CURR_I;
  // current filter as MFC200 sets it (cutoff 5x the 200 Hz loop bandwidth); the default ~300 Hz adds lag
  motor.LPF_current_q.Tf = motor.LPF_current_d.Tf = 1.0f / (_2PI * 1000.0f);
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
  command.add('L', doLimit, "limit (V in steps 3-4, A in step 5)");
  command.add('M', doMotor, "motor");
#if STAGE == 5
  command.add('A', doThrottle, "throttle: A1 on, A0 off, AI idle=now, AF full=now");
#endif
  motor.useMonitoring(Serial);
  motor.monitor_downsample = 0;                      // off: it floods the terminal (hundreds of lines/s).
  motor.monitor_variables = _MON_TARGET | _MON_VEL | _MON_VOLT_Q | _MON_CURR_Q;   // "MMD500" switches it on
  Serial.println("ready. T<value> sets the target, M for all motor commands (e.g. MVP0.2 velocity P gain).");
  Serial.println("status every second: # target | speed | Uq | limit | Iq | throttle | FET | motor | Vbus | power");
  Serial.print("bus over-voltage stop at "); Serial.print(BUS_OV_LIMIT, 0); Serial.println(" V, no regenerative braking");
#if STAGE == 5
  if (thrOn) Serial.println("THROTTLE is ON: T commands are overridden - type A0 to use T, A for throttle status");
  char none[] = ""; doThrottle(none);
#endif
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
  // follow the real bus voltage: SimpleFOC turns volts into duty cycle with it (if the supply is turned up
  // during a run, a stale value would put MORE voltage on the motor than commanded)
  float vb = ain::busVolts();
  if (vb > 9.0f) { driver.voltage_power_supply = vb; driver.voltage_limit = vb; }
#if STAGE == 5
  float lim = limitBase * k;                         // A
  motor.current_limit = lim;
#else
  float lim = limitBase * k;                         // V
  motor.voltage_limit = lim;
#endif
  motor.PID_velocity.limit = lim;                    // velocity mode (MC1): its output is the torque command
  if (motor.controller == MotionControlType::torque) motor.target = constrain(motor.target, -lim, lim);
  if (millis() - tp >= 1000) {
    tp = millis();
    Serial.print("# target "); Serial.print(motor.target, 2);
    Serial.print(" | speed "); Serial.print(motor.shaft_velocity, 1); Serial.print(" rad/s");
    Serial.print(" | Uq "); Serial.print(motor.voltage.q, 2); Serial.print(" V");
    Serial.print(" | limit "); Serial.print(lim, 2);
#if STAGE == 5
    Serial.print(" | Iq "); Serial.print(motor.current.q, 2); Serial.print(" A");
    if (thrOn) { Serial.print(" | thr "); Serial.print(thrArmed ? "" : "(not armed) "); Serial.print(thrPct, 0); Serial.print(" %"); }
#endif
    Serial.print(" | FET "); Serial.print(tf, 1); Serial.print(" C | motor ");
    if (isnan(tm)) { Serial.print(ain::motorSensorOhms(), 0); Serial.print(" ohm (type not set)"); }
    else { Serial.print(tm, 1); Serial.print(" C"); }
    Serial.print(" | Vbus "); Serial.print(vb, 1); Serial.print(" V | power "); Serial.print(k * 100.0f, 0);
    Serial.println(" %");
  }
}

void loop() {
  motor.loopFOC();
  motor.move();
  command.run();
  motor.monitor();
#if STAGE == 5
  throttleTask();
#endif
  noRegenTask();
  busTask();
  temperatureTask();
  if (drv::faultActive()) halt("DRV8353 nFAULT");
  blink();
}
#endif
