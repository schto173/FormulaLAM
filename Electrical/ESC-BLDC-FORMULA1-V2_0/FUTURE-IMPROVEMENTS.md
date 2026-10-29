# ESC – ideas for future versions

Ideas collected while designing V2 and bringing up board #2 (V1.2). None of this is needed for the car to run safely
today. The power stage follows TI's DRV8353RH-EVM; hardware ideas below that go beyond that board are options to
weigh, not plans.

## Firmware

- **Discontinuous PWM ("bottom clamping").** The motor only reacts to the voltage *differences* between its three
  phases, so all three PWM values can be shifted down together until the lowest phase sits at 0 %. That phase then
  stays switched to ground (low-side transistor on, no switching) for a third of every electrical turn, while the
  other two phases do the FOC work as before.
  Benefits: about a third less switching (roughly 30 % less switching loss, ~0.5–1 W at cruise) and longer
  current-measurement windows (at the 0.45 voltage cap the busiest phase gets ~11 µs instead of ~5.5 µs).
  Effort: ~10 lines in `CappedMotor::setPhaseVoltage` (main.cpp). The DRV8353's charge-pump high-side drive allows
  0 % / 100 % duty indefinitely. Test on the bench first, compare measured vs calculated Iq on the dashboard.
- **Merge `firmware-softstart` into `firmware`** once the launch curve (20 → 15 → 10 A) and the clutch take-up
  (1 s creep at ~1 km/h, max 4 A) have proven themselves.
- **Re-engage at wheel speed (needs V2's wheel sensor J11 / PC6).** While rolling, spin the motor up to the wheel
  speed before applying torque – no 1 s take-up delay and no clutch bang when re-engaging at speed.
- **Understand the board #2 VDS_OCP trip** (46.8 V, voltage fraction 0.48, no load, low-side FET phase B) with the
  black-box CSV before ever raising the voltage fraction above 0.45.
- **Pulse-and-glide mode:** speed band (e.g. 20–30 km/h) + burn current (~6–8 A, near the motor's best efficiency);
  burn to the upper speed, coast (motor off, freewheel) to the lower one, repeat while the throttle is held.
- **PWM 16 kHz instead of 20 kHz:** ~20 % less switching loss; may add a faint audible whine. Measure Wh/km both ways.
- **Wh/km logging** on the dashboard for every run (battery power / speed), to compare settings and drivers.

## Hardware (V3 or later)

- **Bulk capacitors: hybrid polymer** instead of the Samyoung NXA low-ESR electrolytics (1.65 A ripple, 36 mΩ each)
  if long runs at 15–20 A are wanted: ~1.5× the ripple rating per capacitor and less ageing from heat. JLC stock is
  thin (e.g. ChuangHui 220 µF 100 V 13x25, 2.4 A, 32 mΩ); otherwise Mouser/Digikey and hand soldering.
  A pricier standard electrolytic is not automatically better (Nichicon PW 330 µF: only 1.08 A).
- **Pi Zero 2 W + display supply:** give them their own 5 V converter from the battery – V2's 5 V comes from the
  DRV8353's 12 V buck (350 mA total) and cannot carry ~3 W extra.
- Ideas from V2-PLAN not built yet: magnetic encoder on the motor shaft (MT6701 / AS5047P), battery energy meter
  (INA228), reverse-polarity protection, hardware e-stop + watchdog on the DRV ENABLE line, locking connectors
  (JST-GH / Micro-Fit), isolated CAN.
- Inline phase-current sensing (INA240 per phase) is designed and parked as `tools/optional/v2_sch_inline.py` –
  only worth it if the low-side measurement ever becomes the limit.

## Motor and drivetrain

- **Gear ratio:** at cruise the 7070's spinning (iron) losses dominate, so a lower ratio is more efficient down to
  where starting still works: ~8:1 (26-tooth pinion) is close to the minimum loss; 9.41:1 (22 teeth) is fitted now.
- **Smaller motor** with less iron, e.g. Flipsky 6354 140 KV (fits ~8:1 and 12S). First measure the 7070's
  no-load battery power at ~1500 / 2500 / 3500 rpm to see how much there is to gain.
- Tyre pressure, wheel alignment, brake drag and aerodynamics are worth more per km than most electronics changes.
