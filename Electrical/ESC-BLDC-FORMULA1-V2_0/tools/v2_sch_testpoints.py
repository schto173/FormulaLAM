"""V2 schematic: scope ground loops + DC test pads (run once, after v2_sch_edit.py).
 - TP1/TP2: Keystone 5001 black test loops on GND (power stage / logic side) for the scope ground clip
 - TP3..TP7: bare 1.5 mm SMD pads on VGLS, VCP (board #1 failed with GDUV / VGLS at 3 V), VBUCK 12 V, +5V, +3V3
   VGLS/VCP get net labels at the DRV8353 pins (their nets were unnamed before)
usage: v2_sch_testpoints.py <sch>"""
import sys, os, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from schlib import *
f = sys.argv[1]; s = open(f, encoding='utf-8').read()
assert '(reference "TP1")' not in s, 'already applied'
ROOT = re.search(r'\(uuid "([^"]+)"\)', s).group(1)
LIBDIR = r"C:/Program Files/KiCad/10.0/share/kicad/symbols/"

def prop(name, val, x, y, hide=False, ang=0):
    return ('\t\t(property "%s" "%s"\n\t\t\t(at %s %s %d)\n%s\t\t\t(show_name no)\n\t\t\t(do_not_autoplace no)\n'
            '\t\t\t(effects\n\t\t\t\t(font\n\t\t\t\t\t(size 1.27 1.27)\n\t\t\t\t)\n\t\t\t)\n\t\t)\n') % (
        name, val, fmt(x), fmt(y), ang, '\t\t\t(hide yes)\n' if hide else '')
def symbol(lib_id, ref, val, x, y, rot, fp, desc, lcsc, pins, power=False, in_bom=True):
    o = ('\t(symbol\n\t\t(lib_id "%s")\n\t\t(at %s %s %d)\n\t\t(unit 1)\n\t\t(body_style 1)\n\t\t(exclude_from_sim no)\n'
         '\t\t(in_bom %s)\n\t\t(on_board yes)\n\t\t(in_pos_files %s)\n\t\t(dnp no)\n\t\t(uuid "%s")\n') % (
        lib_id, fmt(x), fmt(y), rot, 'yes' if in_bom else 'no', 'yes' if in_bom else 'no', U())
    if power:
        o += prop('Reference', ref, x, y + 3.81, hide=True) + prop('Value', val, x, y + (5.08 if 'GND' in lib_id else -3.81))
    else:
        o += prop('Reference', ref, x + 2.54, y - 1.27) + prop('Value', val, x + 2.54, y + 1.27)
    o += prop('Footprint', fp, x, y, hide=True) + prop('Datasheet', '', x, y, hide=True) + prop('Description', desc, x, y, hide=True)
    if lcsc: o += prop('LCSC', lcsc, x, y, hide=True)
    for p in pins: o += '\t\t(pin "%s"\n\t\t\t(uuid "%s")\n\t\t)\n' % (p, U())
    o += ('\t\t(instances\n\t\t\t(project "bldc-75v"\n\t\t\t\t(path "/%s"\n\t\t\t\t\t(reference "%s")\n\t\t\t\t\t(unit 1)\n'
          '\t\t\t\t)\n\t\t\t)\n\t\t)\n\t)\n') % (ROOT, ref)
    return o
pwr_n = [max(int(n) for n in re.findall(r'"#PWR0*(\d+)"', s))]
def power(kind_, x, y):
    pwr_n[0] += 1
    lib = {'GND': 'power:GND', '+3V3': 'power:+3V3'}.get(kind_, 'power:VBUS')
    return symbol(lib, '#PWR%03d' % pwr_n[0], kind_, x, y, 0, '', 'Power symbol', None, ('1',), power=True)
def label(name, p):
    return ('\t(label "%s"\n\t\t(at %s %s 0)\n\t\t(effects\n\t\t\t(font\n\t\t\t\t(size 1.27 1.27)\n\t\t\t)\n'
            '\t\t\t(justify left bottom)\n\t\t)\n\t\t(uuid "%s")\n\t)\n') % (name, fmt(p[0]), fmt(p[1]), U())
def text(t, x, y):
    return ('\t(text "%s"\n\t\t(exclude_from_sim no)\n\t\t(at %s %s 0)\n\t\t(effects\n\t\t\t(font\n\t\t\t\t(size 1.27 1.27)\n\t\t\t)\n'
            '\t\t\t(justify left bottom)\n\t\t)\n\t\t(uuid "%s")\n\t)\n') % (t, fmt(x), fmt(y), U())

# ---- free area check (x 205..285, y 222..240) ------------------------------------------------------------------------
B = blocks(s); libp = lib_pins(s)
for st, en, b in B:
    if kind(b) in ('symbol', 'label', 'junction', 'no_connect', 'text'):
        x, y = item_at(b)
        assert not (205 <= x <= 285 and 222 <= y <= 240), ('area not free', b[:80])
    elif kind(b) == 'wire':
        for x, y in wire_pts(b): assert not (205 <= x <= 285 and 222 <= y <= 240), 'area not free (wire)'

# ---- labels on the DRV8353 VGLS / VCP pins ---------------------------------------------------------------------------
n = ''
for st, en, b in B:
    if kind(b) == 'symbol' and symbol_info(b)['ref'] == 'U1':
        pts = {num: (x, y) for num, x, y in pin_points(symbol_info(b), libp)}
n += label('VGLS', pts['2']) + label('VCP', pts['7'])

# ---- test points -----------------------------------------------------------------------------------------------------
LOOP, PAD = 'TestPoint:TestPoint_Keystone_5000-5004_Miniature', 'TestPoint:TestPoint_Pad_D1.5mm'
y = 232.41
for i, (ref, net, fp_, desc, lcsc) in enumerate((
        ('TP1', 'GND', LOOP, 'Scope ground loop at the power stage (Keystone 5001, black)', 'C238122'),
        ('TP2', 'GND', LOOP, 'Scope ground loop at the logic side (Keystone 5001, black)', 'C238122'),
        ('TP3', 'VGLS', PAD, 'Test pad VGLS (low-side gate supply, ~11 V when enabled)', None),
        ('TP4', 'VCP', PAD, 'Test pad VCP (charge pump, VDRAIN + ~11 V)', None),
        ('TP5', 'VBUCK', PAD, 'Test pad VBUCK 12 V (DRV8353 buck)', None),
        ('TP6', '+5V', PAD, 'Test pad +5V', None),
        ('TP7', '+3V3', PAD, 'Test pad +3V3 (from the Nucleo)', None))):
    x = 210.82 + 10.16 * i
    loop = fp_ == LOOP
    if net == 'GND':                      # TP points up, GND symbol below the pin point
        n += symbol('Connector:TestPoint', ref, 'GND', x, y, 0, fp_, desc, lcsc, ('1',)) + power('GND', x, y)
    elif net in ('VGLS', 'VCP'):
        n += symbol('Connector:TestPoint', ref, net, x, y, 0, fp_, desc, lcsc, ('1',), in_bom=False) + label(net, (x, y))
    else:                                 # TP points down, supply symbol above the pin point
        n += symbol('Connector:TestPoint', ref, net, x, y, 180, fp_, desc, lcsc, ('1',), in_bom=False) + power(net, x, y)
n += text('Scope: clip the probe ground to TP1 (power stage) or TP2 (logic). DC test pads: TP3 VGLS, TP4 VCP, TP5 12 V, TP6 5 V, TP7 3.3 V', 205.74, 223.52)

# ---- library symbol, write -------------------------------------------------------------------------------------------
if '(symbol "Connector:TestPoint"' not in s:
    src = open(LIBDIR + 'Connector.kicad_sym', encoding='utf-8').read()
    i0 = src.index('\t(symbol "TestPoint"\n'); d = 0; j = i0
    while True:
        if src[j] == '(': d += 1
        elif src[j] == ')':
            d -= 1
            if d == 0: break
        elif src[j] == '"':
            j += 1
            while src[j] != '"': j += 1
        j += 1
    t = src[i0:j + 1].replace('(symbol "TestPoint"', '(symbol "Connector:TestPoint"', 1)
    k = s.index('\t(lib_symbols\n') + len('\t(lib_symbols\n')
    s = s[:k] + '\n'.join('\t' + ln for ln in t.split('\n')) + '\n' + s[k:]
i = s.index('\t(sheet_instances'); s = s[:i] + n + s[i:]
open(f, 'w', encoding='utf-8', newline='\n').write(s)
print('ok, VGLS/VCP label at', pts['2'], pts['7'], 'last #PWR', pwr_n[0])
