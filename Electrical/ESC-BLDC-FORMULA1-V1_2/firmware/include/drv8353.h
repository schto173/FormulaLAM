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
  // IDRIVE = gate charge current, i.e. how hard the DRV switches the FETs. Chosen with TI's rule
  // (E2E FAQ "Selecting the Best IDRIVE setting"): IDRIVE = Qgd / edge time, edge time 100-300 ns.
  // CSD19532Q5B Qgd = 8.7 nC -> 50 mA source (~175 ns edge) / 100 mA sink (~87 ns edge) = the gentlest setting.
  // TI's E2E engineers name a too high IDRIVE as one way to damage the DRV8353's VGLS supply (charge coupled
  // through the FET's Cgd into GLx pumps up VGLS) - board #1 died with VGLS damaged at 150/300 mA.
  // Cost: a little more switching loss (roughly 1-2 W total at 10 A, 45 V, 20 kHz). Raise only after scoping.
  { GATE_HS,  0x311, "gate drive HS: unlocked, IDRIVEP 50 mA, IDRIVEN 100 mA" },
  { GATE_LS,  0x711, "gate drive LS: CBC, TDRIVE 4 us, IDRIVEP 50 mA, IDRIVEN 100 mA" },
  // VDS_LVL 0.3 V ~ 40 A (hot) .. 75 A (cold) across a 4-8 mOhm FET - far above our 10 A. LATCHED: one
  // over-current switches the bridge off and it stays off (no repeated hard cut-offs = no repeated spikes).
  { OCP_CTRL, 0x116, "OCP: dead time 100 ns, LATCHED shutdown, deglitch 2 us, VDS_LVL 0.3 V" },
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
