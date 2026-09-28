// Minimal DRV8353RS SPI driver: 16-bit frames, SPI mode 1, register map from datasheet SLVSDY6A section 8.6.
#pragma once
#include <Arduino.h>
#include <SPI.h>
#include "board.h"

namespace drv {

static SPIClass spi(PIN_DRV_MOSI, PIN_DRV_MISO, PIN_DRV_SCK);
static const SPISettings cfg(1000000, MSBFIRST, SPI_MODE1);   // SDI captured on the falling edge

enum Reg : uint8_t { FAULT1 = 0x00, FAULT2 = 0x01, DRV_CTRL = 0x02, GATE_HS = 0x03, GATE_LS = 0x04,
                     OCP_CTRL = 0x05, CSA_CTRL = 0x06, DRV_CONF = 0x07 };

inline uint16_t xfer(uint16_t word) {
  spi.beginTransaction(cfg);
  digitalWrite(PIN_DRV_CS, LOW);
  delayMicroseconds(1);
  uint16_t r = spi.transfer16(word);
  digitalWrite(PIN_DRV_CS, HIGH);
  spi.endTransaction();
  delayMicroseconds(1);
  return r & 0x7FF;
}
inline uint16_t read(uint8_t a)             { return xfer(0x8000 | (uint16_t(a & 0xF) << 11)); }
inline void     write(uint8_t a, uint16_t d) { xfer((uint16_t(a & 0xF) << 11) | (d & 0x7FF)); }

// Settings written at start-up. Everything else stays at the datasheet default (6x PWM mode).
struct Setting { uint8_t reg; uint16_t val; const char *what; };
static const Setting SETTINGS[] = {
  // IDRIVE for CSD19532Q5B (Qgd ~8 nC): 150 mA source / 300 mA sink -> ~50 ns / ~25 ns edges.
  // The default 1000 / 2000 mA switches very hard and rings; raise only if the FETs run hot.
  { GATE_HS,  0x333, "gate drive HS: unlocked, IDRIVEP 150 mA, IDRIVEN 300 mA" },
  { GATE_LS,  0x733, "gate drive LS: CBC, TDRIVE 4 us, IDRIVEP 150 mA, IDRIVEN 300 mA" },
  // VDS_LVL 0.4 V ~ 50 A (hot) .. 100 A (cold) across a 4-8 mOhm FET. Default 1 V would never trip.
  { OCP_CTRL, 0x157, "OCP: dead time 100 ns, auto-retry, deglitch 2 us, VDS_LVL 0.4 V" },
  // Bidirectional (VREF/2 = 1.65 V centre), 20 V/V: +-30 A * 2 mOhm * 20 = +-1.2 V -> 0.45..2.85 V
  { CSA_CTRL, 0x280, "CSA: VREF/2 bidirectional, gain 20 V/V, sense OCP 0.25 V" },
  { DRV_CTRL, 0x000, "driver control: 6x PWM, faults enabled" },
};

inline void printFaults(Stream &out) {
  static const char *f1[] = {"VDS_LC", "VDS_HC", "VDS_LB", "VDS_HB", "VDS_LA", "VDS_HA",
                             "OTSD", "UVLO", "GDF", "VDS_OCP", "FAULT"};
  static const char *f2[] = {"VGS_LC", "VGS_HC", "VGS_LB", "VGS_HB", "VGS_LA", "VGS_HA",
                             "GDUV", "OTW", "SC_OC", "SB_OC", "SA_OC"};
  uint16_t a = read(FAULT1), b = read(FAULT2);
  out.print("DRV faults: 0x"); out.print(a, HEX); out.print(" 0x"); out.print(b, HEX);
  if (!a && !b) { out.println("  (none)"); return; }
  for (int i = 10; i >= 0; i--) if (a & (1 << i)) { out.print(' '); out.print(f1[i]); }
  for (int i = 10; i >= 0; i--) if (b & (1 << i)) { out.print(' '); out.print(f2[i]); }
  out.println();
}

// Enable the DRV, write + verify the settings, clear faults. Returns false if SPI does not answer.
inline bool begin(Stream &out) {
  pinMode(PIN_DRV_CS, OUTPUT);
  digitalWrite(PIN_DRV_CS, HIGH);
  pinMode(PIN_DRV_FAULT, INPUT_PULLUP);
  pinMode(PIN_DRV_EN, OUTPUT);
  digitalWrite(PIN_DRV_EN, HIGH);
  delay(10);                                   // wake-up time before SPI
  spi.begin();
  bool ok = true;
  for (const Setting &s : SETTINGS) {
    write(s.reg, s.val);
    uint16_t rb = read(s.reg);
    bool match = (rb == s.val);
    ok &= match;
    out.print(match ? "  ok   " : "  FAIL ");
    out.print("reg 0x"); out.print(s.reg, HEX); out.print(" = 0x"); out.print(rb, HEX);
    out.print("  ("); out.print(s.what); out.println(")");
  }
  write(DRV_CTRL, 0x001);                      // CLR_FLT
  return ok;
}

inline bool faultActive() { return digitalRead(PIN_DRV_FAULT) == LOW; }

}  // namespace drv
