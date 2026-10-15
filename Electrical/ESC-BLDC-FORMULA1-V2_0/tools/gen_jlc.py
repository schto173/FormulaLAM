"""JLCPCB BOM + CPL (pick and place) from the board.   Run with KiCad's Python:
    "C:/Program Files/KiCad/10.0/bin/python.exe" tools/gen_jlc.py bldc-75v.kicad_pcb jlcpcb/bldc-75v
Rotation / centre corrections were derived by matching each KiCad footprint's pads against the pads of the
EasyEDA footprint JLC uses for the chosen LCSC part (see overlay.py). Check every new footprint the same way."""
import pcbnew, sys, os, csv, math, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lcsc_map import LCSC, NOT_ASSEMBLED

# footprint name -> (rotation offset deg CCW, centre offset in footprint-local mm)
CORR = {
    'TDSON-8-1':                                   (270, (0.0, 0.0)),    # JLC SON-8: pins 1-4 along bottom edge
    'IC_TPD4S010DQAR':                             (270, (0.0, 0.0)),    # JLC DFN2510-10: pins 1-5 along bottom edge
    'HSOP-8-1EP_3.9x4.9mm_P1.27mm_EP2.41x3.1mm_ThermalVias': (270, (0.0, 0.0)),  # JLC SO-8-EP: pin 1 bottom-left
    'SOIC-8_3.9x4.9mm_P1.27mm':                    (270, (0.0, 0.0)),    # C38695 TJA1051T/3
    'SOT-23':                                      (180, (0.0, 0.0)),    # C15771 PESD1CAN
    'JST_XH_B3B-XH-A_1x03_P2.50mm_Vertical':       (0,   (2.5, 0.0)),    # origin = pin 1, JLC centre = pin 2
    'JST_XH_B4B-XH-A_1x04_P2.50mm_Vertical':       (180, (3.75, 0.0)),   # C144395: JLC pin 1 on the other end
    'JST_XH_B5B-XH-A_1x05_P2.50mm_Vertical':       (0,   (5.0, 0.0)),
    'JST_XH_B6B-XH-A_1x06_P2.50mm_Vertical':       (0,   (6.25, 0.0)),   # C144397, verified 0.001 mm
    'CP_Radial_D16.0mm_P7.50mm':                   (0,   (3.75, 0.0)),   # KiCad origin = + pad, JLC = can centre
    'FuseHolder_Blade_ATO_Littelfuse_FLR_178.6165': (0,  (6.4, 1.25)),   # KiCad origin = pad 1 corner
    'D_PowerDI-123':                               (0,   (0.3375, 0.0)), # KiCad origin = cathode pad
}

b = pcbnew.LoadBoard(sys.argv[1]); TO = pcbnew.ToMM
out = sys.argv[2]

def rot(v, deg):  # CCW as seen from the top, KiCad Y-down coordinates
    a = math.radians(deg); x, y = v
    return (x * math.cos(a) + y * math.sin(a), -x * math.sin(a) + y * math.cos(a))

bom = collections.OrderedDict(); cpl = []; skipped = []
fps = sorted(b.GetFootprints(), key=lambda f: (f.GetReference()[0], int(''.join(c for c in f.GetReference() if c.isdigit()) or 0)))
for fp in fps:
    ref = fp.GetReference()
    if ref in NOT_ASSEMBLED or fp.IsExcludedFromPosFiles():
        skipped.append(ref); continue
    code = LCSC[ref]
    name = str(fp.GetFPID().GetLibItemName())
    r_off, d = CORR.get(name, (0, (0.0, 0.0)))
    rk = fp.GetOrientationDegrees()
    dx, dy = rot(d, rk)
    x = TO(fp.GetPosition().x) + dx; y = TO(fp.GetPosition().y) + dy
    layer = 'Bottom' if fp.IsFlipped() else 'Top'
    cpl.append((ref, '%.4f' % x, '%.4f' % -y, layer, '%g' % ((rk + r_off) % 360)))
    if code not in bom:
        bom[code] = [fp.GetValue(), [], name, code]
    bom[code][1].append(ref)

# Nucleo morpho sockets: part of the U2 footprint, so place them from its pad blocks.
# The Nucleo plugs in from the TOP (its morpho pins stick out of its underside), so the sockets sit on our top side.
u2 = b.FindFootprintByReference('U2')
for conn in ('CN7', 'CN10'):
    pts = [(TO(p.GetPosition().x), TO(p.GetPosition().y)) for p in u2.Pads() if p.GetNumber().startswith(conn + '_')]
    assert len(pts) == 38, (conn, len(pts))
    cx = sum(p[0] for p in pts) / 38; cy = sum(p[1] for p in pts) / 38
    # 90 deg was verified with the Nucleo upright (U2 at 0 deg, header rows vertical); the sockets turn with U2
    cpl.append((conn, '%.4f' % cx, '%.4f' % -cy, 'Top', '%g' % ((90 + u2.GetOrientationDegrees()) % 360)))
bom['C42372543'] = ['2x19 female socket 2.54mm 8.5mm (Nucleo morpho)', ['CN7', 'CN10'], 'PinSocket_2x19_P2.54mm_Vertical', 'C42372543']

with open(out + '_bom_jlc.csv', 'w', newline='', encoding='utf-8') as f:
    w = csv.writer(f); w.writerow(['Comment', 'Designator', 'Footprint', 'LCSC Part #'])
    for v, refs, name, code in bom.values():
        w.writerow([v, ','.join(refs), name, code])
with open(out + '_cpl_jlc.csv', 'w', newline='', encoding='utf-8') as f:
    w = csv.writer(f); w.writerow(['Designator', 'Mid X', 'Mid Y', 'Layer', 'Rotation'])
    for row in cpl:
        w.writerow(row)
print('BOM lines:', len(bom), '| placements:', len(cpl), '| not assembled:', skipped)
