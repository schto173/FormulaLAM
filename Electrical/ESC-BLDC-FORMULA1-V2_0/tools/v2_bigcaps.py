"""V2: bus caps back to V1.2's big THT electrolytics (330 uF 100 V, 16x25 mm, Samyoung NXA C346987, 1.65 A ripple)
usage: v2_bigcaps.py <project dir>"""
import sys, re, os
d = sys.argv[1]
sys.path.insert(0, os.path.join(d, 'tools'))
from schlib import *
p = os.path.join(d, 'bldc-75v.kicad_sch'); s = open(p, encoding='utf-8').read()
n = 0
for ref in ('C11', 'C21', 'C68'):
    for st, en, b in blocks(s):
        if kind(b) == 'symbol' and symbol_info(b)['ref'] == ref:
            nb = re.sub(r'(\(property "Value" ")[^"]*', r'\g<1>330uF 100V', b, count=1)
            nb = re.sub(r'(\(property "Footprint" ")[^"]*', r'\g<1>Capacitor_THT:CP_Radial_D16.0mm_P7.50mm', nb, count=1)
            nb = re.sub(r'(\(property "LCSC" ")[^"]*', r'\g<1>C346987', nb, count=1)
            nb = re.sub(r'(\(property "Description" ")[^"]*', r'\g<1>Bus bulk cap 330 uF 100 V, 16x25 mm, 1.65 A ripple (Samyoung NXA, as V1.2)', nb, count=1)
            s = s[:st] + nb + s[en:]; n += 1; break
open(p, 'w', encoding='utf-8', newline='\n').write(s)
p = os.path.join(d, 'tools', 'v2_pcb_prepare.py'); s = open(p, encoding='utf-8').read()
a = 'x0, y0, x1, y1 = 143.00, 44.00, 241.00, 134.00'; assert a in s; s = s.replace(a, 'x0, y0, x1, y1 = 143.00, 44.00, 253.50, 134.00')
open(p, 'w', encoding='utf-8', newline='\n').write(s)
p = os.path.join(d, 'tools', 'v2_place.py'); s = open(p, encoding='utf-8').read()
for a, c in (("P('C68', 234.0, 65.5, 0); P('C11', 234.0, 77.0, 0)       # bulk caps, top-right corner (+ pad toward the cells)",
              "# bus caps: 3x 330 uF 100 V THT (16 mm, footprint origin = + pad, body centre 3.75 mm to the right)\n"
              "P('C68', 234.0 - 3.75, 66.0, 0)            # beside cell C\n"
              "P('C11', 243.6 - 3.75, 83.0, 0)            # right of cell B"),
             ("P('C21', 234.0, 104.5, 0)                  # bulk cap, bottom-right corner",
              "P('C21', 236.5 - 3.75, 108.5, 0)           # beside cell A"),
             ("P('TP1', 232.0, 118.0, 0)", "P('TP1', 233.0, 122.0, 0)"),
             ("('H2', (237.0, 48.0))", "('H2', (249.5, 48.0))"), ("('H4', (237.0, 130.0))", "('H4', (249.5, 130.0))")):
    assert s.count(a) == 1, a; s = s.replace(a, c)
open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('changed', n, 'caps + outline + placement')
