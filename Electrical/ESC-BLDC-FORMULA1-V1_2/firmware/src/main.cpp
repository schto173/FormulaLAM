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
// Total voltage limit: SimpleFOC limits Uq and Ud each to the cap, so together they could reach 1.41 x the cap
// and clip the PWM - then the low-side shunts can't measure and the current loop works blind. Here Ud is
// limited first and Uq gets what is left, so |U| never exceeds the cap.
class CappedMotor : public BLDCMotor {
 public:
  using BLDCMotor::BLDCMotor;
  void setPhaseVoltage(float Uq, float Ud, float angle_el) override {
    float lim = voltage_limit;
    Ud = _constrain(Ud, -lim, lim);
    float qmax = sqrtf(max(lim * lim - Ud * Ud, 0.0f));
    Uq = _constrain(Uq, -qmax, qmax);
    BLDCMotor::setPhaseVoltage(Uq, Ud, angle_el);
  }
};
CappedMotor motor = CappedMotor(POLE_PAIRS);
// DRV8353 in 3x PWM mode (as TI's EVM): INHx = PWM, INLx = per-phase enable (SimpleFOC enable/disable drives them),
// the DRV makes the low-side signal and the dead time. EN is ours.
BLDCDriver3PWM driver = BLDCDriver3PWM(PIN_INHA, PIN_INHB, PIN_INHC, PIN_INLA, PIN_INLB, PIN_INLC);
HallSensor sensor = HallSensor(PIN_HALL_A, PIN_HALL_B, PIN_HALL_C, POLE_PAIRS);
#if HALL_SMOOTHING
#include "encoders/smoothing/SmoothingSensor.h"
// predicts the angle between hall edges from the speed (max one 60-degree step ahead); the HallSensor
// constructor also sets phase_correction = -30 deg el., because a hall edge marks the START of a sector
SmoothingSensor smooth = SmoothingSensor(sensor, motor);
#endif
// Low-side current sense with two-phase reconstruction: the shunt of the phase with the highest duty cycle gets the
// shortest measuring window (lower FET on only (1 - duty) x 50 us). Above RECON_DUTY that reading is replaced by
// -(sum of the other two). driver.dc_a/b/c are the duty cycles SimpleFOC set last (the period that was sampled).
class TwoPhaseLowside : public LowsideCurrentSense {
 public:
  using LowsideCurrentSense::LowsideCurrentSense;
  bool recon = false;                                // switched on after initFOC (its checks drive single phases)
  uint32_t nAll = 0, nRecon = 0;                     // statistics for the status line
  // Side-by-side comparison for the data line: Iq from the three measured phases AND Iq with the calculated phase,
  // from the same samples and the same rotor angle, whichever one the controller uses. Collected only in data mode.
  // Per data line: n samples, sum and sum of squares (around ref = Iq at the start, keeps the float sums small).
  bool compare = false;
  uint32_t cmpN = 0, cmpHigh = 0;
  float cmpRef = 0, mSum = 0, mSq = 0, cSum = 0, cSq = 0;
  void cmpReset(float ref) { cmpN = cmpHigh = 0; cmpRef = ref; mSum = mSq = cSum = cSq = 0; }
  PhaseCurrent_s getPhaseCurrents() override {
    PhaseCurrent_s c = LowsideCurrentSense::getPhaseCurrents(), r = c;
    nAll++;
    float da = ::driver.dc_a, db = ::driver.dc_b, dc = ::driver.dc_c, m = max(da, max(db, dc));   // our global driver
    bool high = (m >= RECON_DUTY);                   // one window too short: calculate that phase
    if (high) { if (m == da) r.a = -(r.b + r.c); else if (m == db) r.b = -(r.a + r.c); else r.c = -(r.a + r.b); }
    if (compare) {
      float ang = ::motor.electrical_angle;            // set by loopFOC just before it reads the currents
      float qm = getDQCurrents(getABCurrents(c), ang).q - cmpRef;
      float qc = high ? getDQCurrents(getABCurrents(r), ang).q - cmpRef : qm;
      mSum += qm; mSq += qm * qm; cSum += qc; cSq += qc * qc; cmpN++; if (high) cmpHigh++;
    }
    if (!recon || !high) return c;
    nRecon++;
    return r;
  }
};
TwoPhaseLowside current_sense = TwoPhaseLowside(SHUNT_OHMS, CSA_GAIN, PIN_SOA, PIN_SOB, PIN_SOC);
Commander command = Commander(Serial);

void doA() { sensor.handleA(); }
void doB() { sensor.handleB(); }
void doC() { sensor.handleC(); }
void doTarget(char *cmd) { command.scalar(&motor.target, cmd); }
// "L<value>": limit before temperature derating - volts in steps 3-4, amps in step 5. Capped for the bench.
#if STAGE == 5
static float limitBase = CURRENT_LIMIT; static const float LIMIT_CAP = CURRENT_MAX;     // starts at 10, hard max 15
#else
static float limitBase = VOLTAGE_LIMIT; static const float LIMIT_CAP = 1.5f;
#endif
void doLimit(char *cmd) { float v = limitBase; command.scalar(&v, cmd); limitBase = constrain(v, 0.0f, LIMIT_CAP); }
// "D1" / "D0": data mode for the PC dashboard (firmware/dashboard/esc-dashboard.html). Only prints values that
// exist anyway; temperatures and Vbus are the ones temperatureTask measured last (no extra ADC reads).
static bool dataOn = false;
static float lastFet = NAN, lastMot = NAN, lastVb = NAN, lastLim = 0.0f;
void doData(char *cmd) {
  dataOn = (cmd[0] == '1');
  current_sense.cmpReset(motor.current.q); current_sense.compare = dataOn;
  Serial.println(dataOn ? "data mode ON (D0 = off): $D,ms,thr%,set_kmh,motor_kmh,rad_s,iq,id,uq,ud,umax,ilim,fet,mot,vbus,flags,"
                          "iq_meas,sd_meas,iq_calc,sd_calc,calc%"
                        : "data mode OFF");
}
static float capFraction = FOC_MAX_FRACTION;       // "V<fraction>": voltage cap <= capFraction x Vbus
#if STAGE == 5
static float mluLimit = FOC_VOLTAGE_MAX;             // "MLU<volts>" lowers the voltage cap for a test
#endif
void doMotor(char *cmd) {
  // "ME1" (driver on again after "ME0"): SimpleFOC's enable() calls setPwm(0,0,0) = all low sides on for a moment,
  // which would brake a spinning motor -> only at standstill. (enable() resets the PIDs itself; we do it too.)
  if (cmd[0] == 'E' && cmd[1] && cmd[1] != '0') {
    if (fabsf(motor.shaft_velocity) > 1.0f) {
      Serial.println("refused: enable only when the motor stands still - wait until it has stopped");
      return;
    }
    motor.target = 0.0f;
    motor.PID_current_q.reset(); motor.PID_current_d.reset();
  }
  // "MLU<volts>": lower the voltage cap for a test; never above FOC_VOLTAGE_MAX
  if (cmd[0] == 'L' && cmd[1] == 'U' && cmd[2]) {
    float v = atof(cmd + 2);
    if (!(v > 0.0f && v <= FOC_VOLTAGE_MAX)) {
      Serial.print("refused: MLU must be between 0 and "); Serial.print(FOC_VOLTAGE_MAX, 1); Serial.println(" V");
      return;
    }
    // lowering the voltage limit below the back-EMF while spinning brakes regeneratively (see BUS_OV_LIMIT)
    if (v < motor.voltage_limit && fabsf(motor.shaft_velocity) > NO_REGEN_SPEED) {
      Serial.println("refused: lowering MLU while spinning brakes into the supply - release, wait until it stops");
      return;
    }
#if STAGE == 5
    mluLimit = v;                                    // applied by temperatureTask (with the FOC_MAX_FRACTION x Vbus rule)
    Serial.print("voltage cap: "); Serial.print(v, 1); Serial.print(" V (and never above "); Serial.print(capFraction, 2); Serial.println(" x Vbus)");
    return;
#endif
  }
  command.motor(&motor, cmd);
}

static bool pwmReady = false;
static bool drvReady = false;                        // DRV8353 enabled and configured (SPI answers)
static void halt(const char *why) {
  // snapshot first, then PWM off, then read the faults - while EN is still HIGH: once EN is low the DRV8353
  // sleeps, SPI doesn't answer and the SDO pull-up makes every fault bit read as 1 (0x7FF)
  float sp = motor.shaft_velocity, uq = motor.voltage.q, iq = motor.current.q, vb = ain::busVolts(2);
  bool nf = drv::faultActive();
  if (pwmReady) motor.disable();
  Serial.print("STOPPED: "); Serial.println(why);
  Serial.print("  at: speed "); Serial.print(sp, 1); Serial.print(" rad/s | Uq "); Serial.print(uq, 2);
  Serial.print(" V | Iq "); Serial.print(iq, 2); Serial.print(" A | Vbus "); Serial.print(vb, 1);
  Serial.print(" V | nFAULT "); Serial.println(nf ? "LOW" : "high");
  // before drv::begin() the DRV is still asleep: SPI would read all 1s (a fake list of every fault)
  if (drvReady) drv::printFaults(Serial);
  else Serial.println("  (DRV8353 not started yet - no fault data, nothing was switched)");
  digitalWrite(PIN_DRV_EN, LOW);
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
// ---- drive: throttle = maximum speed (see board.h), must be released once before it acts ("armed") ----------
static const float RADS_PER_KMH = GEAR_RATIO / (WHEEL_DIAMETER_M / 2.0f) / 3.6f;   // motor rad/s per km/h
static bool thrOn = THROTTLE_ON_AT_START, thrArmed = false;
static float thrIdle = THROTTLE_IDLE_V, thrFull = THROTTLE_FULL_V, thrPct = 0.0f;
static float iCmd = 0.0f, vSetKmh = 0.0f, vMaxKmh = 0.0f;
static bool stalled = false;                         // stall guard tripped, cleared when the throttle re-arms
// drive direction: +1 normal, -1 reversed. Flips the sign of the current command and of the measured speed in
// the drive logic only - speed limit, current limit, ramp, no-regen and COAST work the same both ways.
static int driveDir = (MOTOR_DIRECTION < 0) ? -1 : 1;
static float fwdVelocity() { return driveDir * motor.shaft_velocity; }   // rad/s, positive = forward
static void thrOff(const char *why) { thrOn = false; iCmd = 0.0f; motor.target = 0.0f; Serial.println(why); }
// "A1" on, "A0" off, "AI" idle = now, "AF" full = now, "A" status
void doThrottle(char *cmd) {
  float v = ain::throttleVolts();
  switch (cmd[0]) {
    case '1': thrOn = true; thrArmed = false; iCmd = 0.0f; motor.target = 0.0f;
              Serial.println("throttle ON - release it fully to arm"); break;
    case '0': thrOff("throttle OFF, target 0 - T<amps> commands work again (test mode)"); break;
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
  Serial.print(" V | full "); Serial.print(thrFull, 2); Serial.print(" V | 100 % = "); Serial.print(SPEED_MAX_KMH, 0);
  Serial.print(" km/h | max "); Serial.print(limitBase, 1); Serial.print(" A, +"); Serial.print(CURRENT_RAMP_A_S, 1);
  Serial.println(" A/s");
}
// "V0.50": voltage cap fraction (0.30 .. FOC_FRACTION_LIMIT), lowering only at standstill; "V" = status
void doFraction(char *cmd) {
  if (cmd[0]) {
    float f = atof(cmd);
    if (!(f >= 0.30f && f <= FOC_FRACTION_LIMIT)) {
      Serial.print("refused: V must be between 0.30 and "); Serial.println(FOC_FRACTION_LIMIT, 2); return;
    }
    if (f < capFraction && fabsf(motor.shaft_velocity) > NO_REGEN_SPEED) {
      Serial.println("refused: lowering V while spinning brakes into the supply - release, wait until it stops"); return;
    }
    capFraction = f;
  }
  Serial.print("voltage cap fraction: "); Serial.print(capFraction, 2); Serial.print(" x Vbus (SVPWM max 0.577; ");
  Serial.print(capFraction * 1.732f * 100.0f, 0); Serial.println(" % of the battery between two motor wires)");
}
// "X1" / "X0": two-phase current reconstruction on / off (for comparison); "X" = status
void doRecon(char *cmd) {
  if (cmd[0] == '0' || cmd[0] == '1') current_sense.recon = (cmd[0] == '1');
  Serial.print("two-phase current reconstruction: "); Serial.print(current_sense.recon ? "ON" : "OFF");
  Serial.print(" (phase above "); Serial.print(RECON_DUTY * 100.0f, 0); Serial.println(" % duty is calculated)");
}
// "R1" normal / "R0" reversed / "R" status - only at standstill with the throttle released
void doDirection(char *cmd) {
  if (cmd[0] == '0' || cmd[0] == '1') {
    int want = (cmd[0] == '1') ? 1 : -1;
    if (want != driveDir) {
      if (fabsf(motor.shaft_velocity) > 1.0f || iCmd > 0.0f) {
        Serial.println("refused: change direction only at standstill with the throttle released");
        return;
      }
      driveDir = want;
    }
  }
  Serial.print("direction: "); Serial.print(driveDir > 0 ? "normal" : "reversed");
  Serial.print(" (board.h MOTOR_DIRECTION = "); Serial.print(MOTOR_DIRECTION); Serial.println(", used after a reset)");
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
    iCmd = 0.0f; motor.target = 0.0f;
    if (x <= 0.0f && !isnan(v) && v <= THROTTLE_FAULT_V && thrFull >= thrIdle + 0.2f) {
      thrArmed = true; stalled = false; Serial.println("# throttle armed");
    }
    return;
  }
  // highest speed the voltage cap can deliver, with SPEED_V_RESERVE left free (never drive into the cap)
  vMaxKmh = constrain((motor.voltage_limit - SPEED_V_RESERVE) / KE_V_PER_RADS / RADS_PER_KMH, 0.0f, SPEED_MAX_KMH);
  vSetKmh = x * vMaxKmh;
  float vKmh = fwdVelocity() / RADS_PER_KMH;
  float want = (x > 0.0f) ? constrain(SPEED_P_A_PER_KMH * (vSetKmh - vKmh), 0.0f, limitBase) : 0.0f;
  iCmd = (want > iCmd) ? min(want, iCmd + CURRENT_RAMP_A_S * dt) : want;   // up: ramp, down: at once
  // stall guard (see board.h): pushing hard without turning for STALL_TIME_MS -> 0 A until the throttle is released
  static uint32_t tStall = 0;
  if (iCmd >= STALL_CURRENT_A && fabsf(motor.shaft_velocity) < STALL_SPEED_RADS) {
    if (!tStall) tStall = now;
    else if (now - tStall > STALL_TIME_MS) {
      tStall = 0; stalled = true; thrArmed = false; iCmd = 0.0f; motor.target = 0.0f;
      Serial.println("# STALL: motor blocked (>= 8 A, not turning for 3 s) -> 0 A. Release the throttle to drive again.");
      return;
    }
  } else tStall = 0;
  motor.target = driveDir * iCmd;
}

// Quiet standstill: throttle released and the motor (almost) stopped -> DRV8353 COAST bit = all six FETs off.
// Holding 0 A with the FETs switching made the motor hiss (louder on the 45 V pack). Only at standstill for
// now: re-engaging while spinning needs the back-EMF voltage preloaded first (TODO), at standstill there is none.
// PWM and the current loop keep running underneath, so leaving COAST is immediate.
static bool coasting = false;
static void setCoast(bool on) {
  if (on == coasting) return;
  coasting = on;
  if (!on) {
    // the PWM keeps running under COAST: start from 0 V, not from the last duty cycle
    motor.PID_current_q.reset(); motor.PID_current_d.reset();
    motor.setPhaseVoltage(0.0f, 0.0f, motor.electrical_angle);
  }
  drv::write(drv::DRV_CTRL, drv::DRV_CTRL_BASE | (on ? 0x004 : 0x000));   // bit 2 = COAST, keep 3x PWM mode
}
static void coastTask() {
  // while coasting no current flows, but the current PI keeps integrating a tiny measuring offset and crept up
  // to the cap (Uq ~10 V on board #2) - hold it at 0 the whole time
  if (coasting) { motor.PID_current_q.reset(); motor.PID_current_d.reset(); }
  float v = fabsf(motor.shaft_velocity);
  bool released = !thrOn || iCmd <= 0.0f;
  if (!thrOn) { setCoast(false); return; }            // T commands: behave as before
  if (released && v < 1.0f) setCoast(true);
  else if (!released || v > 5.0f) setCoast(false);   // hysteresis: a push by hand alone doesn't toggle it
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
  if (vbus > BUS_START_MAX) halt("bus voltage too high for this firmware (bench build on a battery? wrong pack?)");
#if BATTERY_CELLS
  Serial.print("battery build: "); Serial.print(BATTERY_CELLS); Serial.print("S, ");
  Serial.print(vbus / BATTERY_CELLS, 2); Serial.println(" V per cell");
  if (vbus < BUS_UV_STOP) halt("battery below 3.0 V per cell (or not the configured cell count)");
#endif
  if (!drv::begin(Serial)) halt("DRV8353 does not answer on SPI - run step 1");
  drvReady = true;

  driver.voltage_power_supply = vbus;
  // NOT the motor limit: SimpleFOC centres the phase voltages at driver.voltage_limit / 2. With 0.5 V here the
  // phases swung 0..0.5 V (0-2 % duty, <1 us pulses eaten by the dead time) and the motor only jerked.
  driver.voltage_limit = vbus;                       // phases centred at Vbus/2; the motor limit is below
  driver.pwm_frequency = 20000;
  // No MCU dead time in 3x PWM mode: the DRV inserts its own 100 ns dead time after its VGS handshake
  // (it never turns on both FETs of a phase).
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
  motor.voltage_sensor_align = ALIGN_V_BUS;       // 0.4 V bench, 0.6 V battery build
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
#if STAGE == 5
  motor.updateVoltageLimit(min(FOC_VOLTAGE_MAX, capFraction * vbus));   // current PI limits follow
  current_sense.recon = true;                       // two-phase reconstruction from here on ("X0" = off)
  Serial.print("voltage cap "); Serial.print(motor.voltage_limit, 1); Serial.print(" V (max ");
  Serial.print(FOC_VOLTAGE_MAX, 0); Serial.print(" V, never above "); Serial.print(capFraction, 2);
  Serial.println(" x Vbus)");
  Serial.print("drive: throttle = max speed 0.."); Serial.print(SPEED_MAX_KMH, 0); Serial.print(" km/h, current 0..");
  Serial.print(CURRENT_LIMIT, 0); Serial.print(" A (L up to "); Serial.print(CURRENT_MAX, 0); Serial.print(" A), +"); Serial.print(CURRENT_RAMP_A_S, 1);
  Serial.print(" A/s, gear "); Serial.print(GEAR_RATIO, 1); Serial.print(":1, wheel "); Serial.print(WHEEL_DIAMETER_M, 3);
  Serial.println(" m");
#endif
#endif

  command.add('T', doTarget, "target");
  command.add('L', doLimit, "limit (V in steps 3-4, A in step 5)");
  command.add('D', doData, "data mode for the PC dashboard: D1 on, D0 off");
  command.add('M', doMotor, "motor");
#if STAGE == 5
  command.add('A', doThrottle, "throttle: A1 on, A0 off, AI idle=now, AF full=now");
  command.add('R', doDirection, "direction: R1 normal, R0 reversed (standstill only)");
  command.add('V', doFraction, "voltage cap fraction: V0.45 .. V0.55");
  command.add('X', doRecon, "two-phase current reconstruction: X1 on, X0 off");
#endif
  motor.useMonitoring(Serial);
  motor.monitor_downsample = 0;                      // off: it floods the terminal (hundreds of lines/s).
  motor.monitor_variables = _MON_TARGET | _MON_VEL | _MON_VOLT_Q | _MON_CURR_Q;   // "MMD500" switches it on
  Serial.println("ready. T<value> sets the target, M for all motor commands (e.g. MVP0.2 velocity P gain).");
  Serial.println("status every second: # target | speed | Uq | limit | Umax | Iq | throttle | FET | motor | Vbus");
  Serial.print("bus over-voltage stop at "); Serial.print(BUS_OV_LIMIT, 0); Serial.println(" V, no regenerative braking");
#if STAGE == 5
  if (thrOn) Serial.println("THROTTLE is ON: T commands are overridden - type A0 to use T, A for throttle status");
  char none[] = ""; doThrottle(none);
#endif
}

// Every 200 ms: temperatures (hard stop), battery empty (hard stop), PWM scaling follows the measured bus voltage.
static void temperatureTask() {
  static uint32_t t = 0, tp = 0;
  if (millis() - t < 200) return;
  t = millis();
  float tf = ain::fetCelsius(), tm = ain::motorCelsius();
  if (isnan(tf)) halt("FET temperature sensor fault (TH1 open or shorted)");
  if (tf >= FET_TEMP_STOP) halt("FET over-temperature");
#if MOTOR_TEMP_SENSOR != MOTOR_TEMP_UNKNOWN
  if (!isnan(tm) && tm >= MOTOR_TEMP_STOP) halt("motor over-temperature");
#endif
  // SimpleFOC turns volts into duty cycle with driver.voltage_power_supply: it must be the real bus voltage,
  // otherwise the motor gets more (or less) voltage than commanded
  float vb = ain::busVolts();
  if (vb > 9.0f) { driver.voltage_power_supply = vb; driver.voltage_limit = vb; }
#if BATTERY_CELLS
  static uint32_t tLow = 0;                          // battery empty: below 3.0 V/cell for more than 2 s -> stop
  if (vb > 9.0f && vb < BUS_UV_STOP) { if (!tLow) tLow = millis(); else if (millis() - tLow > 2000) halt("battery empty (below 3.0 V per cell for 2 s)"); }
  else tLow = 0;
#endif
  const float k = 1.0f;                              // no derating: full limit or stop
#if STAGE == 5
  float lim = limitBase * k;                         // A
  motor.current_limit = lim;
  if (vb > 9.0f) {                                   // voltage cap: MLU value, but never above FOC_MAX_FRACTION x Vbus
    float cap = min(mluLimit, capFraction * vb);
    if (fabsf(cap - motor.voltage_limit) > 0.1f) motor.updateVoltageLimit(cap);
  }
#else
  float lim = limitBase * k;                         // V
  motor.voltage_limit = lim;
#endif
  motor.PID_velocity.limit = lim;                    // velocity mode (MC1): its output is the torque command
  lastFet = tf; lastMot = tm; lastVb = vb; lastLim = lim;
  if (motor.controller == MotionControlType::torque) motor.target = constrain(motor.target, -lim, lim);
  if (millis() - tp >= 1000) {
    tp = millis();
    Serial.print("# target "); Serial.print(motor.target, 2);
    Serial.print(" | speed "); Serial.print(motor.shaft_velocity, 1); Serial.print(" rad/s");
    Serial.print(" | Uq "); Serial.print(motor.voltage.q, 2); Serial.print(" V");
    Serial.print(" | limit "); Serial.print(lim, 2);
#if STAGE == 5
    Serial.print(" | Umax "); Serial.print(motor.voltage_limit, 1); Serial.print(" ("); Serial.print(capFraction, 2); Serial.print(")");
    {
      static uint32_t pa = 0, pr = 0;
      uint32_t da = current_sense.nAll - pa, dr = current_sense.nRecon - pr; pa = current_sense.nAll; pr = current_sense.nRecon;
      Serial.print(current_sense.recon ? " | recon " : " | recon OFF "); Serial.print(da ? 100.0f * dr / da : 0.0f, 0); Serial.print(" %");
    }
#endif
#if STAGE == 5
    Serial.print(" | Iq "); Serial.print(motor.current.q, 2); Serial.print(" A");
    if (thrOn) {
      Serial.print(" | thr "); Serial.print(thrArmed ? "" : "(not armed) "); Serial.print(thrPct, 0);
      Serial.print(" % -> "); Serial.print(vSetKmh, 1); Serial.print(" km/h (max "); Serial.print(vMaxKmh, 0);
      Serial.print(") | motor = "); Serial.print(fwdVelocity() / RADS_PER_KMH, 1); Serial.print(" km/h");
      if (driveDir < 0) Serial.print(" REVERSED");
      if (stalled) Serial.print(" STALL (release throttle)");
    }
    if (coasting) Serial.print(" COAST");
#endif
    Serial.print(" | FET "); Serial.print(tf, 1); Serial.print(" C | motor ");
    if (isnan(tm)) { Serial.print(ain::motorSensorOhms(), 0); Serial.print(" ohm (type not set)"); }
    else { Serial.print(tm, 1); Serial.print(" C"); }
    Serial.print(" | Vbus "); Serial.print(vb, 1); Serial.println(" V");
  }
}

// Data mode: one line every 100 ms, numbers only. flags: 1 = COAST, 2 = throttle armed, 4 = throttle on,
// 8 = direction reversed, 16 = stall guard tripped, 32 = controller uses the calculated phase (X1).
// Speeds are forward-positive (step 5). At the end: unfiltered Iq of the last 100 ms, average and scatter
// (standard deviation), once from the 3 measured phases and once with the calculated phase, and the share of
// samples where a phase was calculated. Same samples -> the smaller scatter is the more accurate measurement.
static void dataTask() {
  static uint32_t t = 0;
  if (!dataOn || millis() - t < 100) return;
  t = millis();
  float thr = 0.0f, set = 0.0f; int flags = 0;
#if STAGE == 5
  const float kmh = RADS_PER_KMH;
  if (thrOn) { thr = thrPct; set = vSetKmh; flags |= 4; }
  if (thrArmed) flags |= 2;
  if (coasting) flags |= 1;
  if (driveDir < 0) flags |= 8;
  if (stalled) flags |= 16;
  const float vel = fwdVelocity();
#else
  const float kmh = 1.0f;                            // no gearing known outside step 5: km/h fields = rad/s
  const float vel = motor.shaft_velocity;
#endif
  if (current_sense.recon) flags |= 32;
  Serial.print("$D,"); Serial.print(t); Serial.print(',');
  Serial.print(thr, 0); Serial.print(','); Serial.print(set, 1); Serial.print(',');
  Serial.print(vel / kmh, 1); Serial.print(','); Serial.print(vel, 1); Serial.print(',');
  Serial.print(motor.current.q, 2); Serial.print(','); Serial.print(motor.current.d, 2); Serial.print(',');
  Serial.print(motor.voltage.q, 2); Serial.print(','); Serial.print(motor.voltage.d, 2); Serial.print(',');
  Serial.print(motor.voltage_limit, 1); Serial.print(','); Serial.print(lastLim, 1); Serial.print(',');
  Serial.print(lastFet, 1); Serial.print(','); Serial.print(lastMot, 1); Serial.print(',');
  Serial.print(lastVb, 1); Serial.print(','); Serial.print(flags);
  TwoPhaseLowside &cs = current_sense;
  const float n = cs.cmpN ? (float)cs.cmpN : 1.0f, mm = cs.mSum / n, cm = cs.cSum / n;
  Serial.print(','); Serial.print(cs.cmpRef + mm, 2); Serial.print(','); Serial.print(sqrtf(max(0.0f, cs.mSq / n - mm * mm)), 3);
  Serial.print(','); Serial.print(cs.cmpRef + cm, 2); Serial.print(','); Serial.print(sqrtf(max(0.0f, cs.cSq / n - cm * cm)), 3);
  Serial.print(','); Serial.println(cs.cmpN ? 100.0f * cs.cmpHigh / cs.cmpN : 0.0f, 0);
  cs.cmpReset(motor.current.q);
}

void loop() {
  motor.loopFOC();
  motor.move();
  command.run();
  motor.monitor();
#if STAGE == 5
  throttleTask();
  coastTask();
#endif
  noRegenTask();
  busTask();
  temperatureTask();
  dataTask();
  if (drv::faultActive()) halt("DRV8353 nFAULT");
  blink();
}
#endif
