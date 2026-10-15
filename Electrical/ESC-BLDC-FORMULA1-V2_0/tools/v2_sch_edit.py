"""V1.2 -> V2.0 schematic changes (run once on the copied V1 schematic):
 - remove LM5164 5 V supply (U4 + 14 parts), phase voltage sensing (U3 TPD4S010, R1/R2/R3, R7/R11/R12, C4/C12/C13),
   fuse F1 (battery fuse moves to the battery; J5 pin 2 wired straight to VDRAIN)
 - add TPS562201 12 V (VBUCK) -> 5 V buck (U6, L3, C69-C72, R75-R77)
 - add wheel speed sensor input J11 -> D10 / R78 / C73 -> PC6 (TIM3_CH1)
 - bus caps C11/C21 -> SMD 47 uF 100 V, + C68; TVS D8 -> SMCJ54A (12S pack); DRV buck faster: R34 180k, L1 100 uH 6x6
usage: v2_sch_edit.py <sch>"""
import sys, os, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from schlib import *
f = sys.argv[1]; s = open(f, encoding='utf-8').read()
ROOT = re.search(r'\(uuid "([^"]+)"\)', s).group(1)
LIBDIR = r"C:/Program Files/KiCad/10.0/share/kicad/symbols/"

REMOVE = {'U4', 'C51', 'C52', 'C53', 'C54', 'C55', 'C56', 'C57', 'L2', 'R63', 'R64', 'R65', 'R66', 'R67', 'R68',
          'U3', 'R1', 'R2', 'R3', 'R7', 'R11', 'R12', 'C4', 'C12', 'C13', 'F1'}
REMOVE_LABELS = {'VSENA', 'VSENB', 'VSENC'}

def parse(s):
    B = blocks(s); libp = lib_pins(s)
    return B, libp

# ---------- 1. remove symbols (remember F1's pins), VSENA/B/C labels ------------------------------------------------
B, libp = parse(s)
f1 = None; cut = []
for st, en, b in B:
    k = kind(b)
    if k == 'symbol':
        inf = symbol_info(b)
        if inf['ref'] in REMOVE:
            if inf['ref'] == 'F1': f1 = [(x, y) for n, x, y in pin_points(inf, libp)]
            cut.append((st, en))
    elif k == 'label' and re.search(r'\(label "([^"]+)"', b).group(1) in REMOVE_LABELS:
        cut.append((st, en))
for st, en in sorted(cut, reverse=True): s = s[:st] + s[en:]
assert f1 and len(f1) == 2
print('removed %d items, F1 pins at %s' % (len(cut), f1))

def wire(a, c):
    return ('\t(wire\n\t\t(pts\n\t\t\t(xy %s %s) (xy %s %s)\n\t\t)\n\t\t(stroke\n\t\t\t(width 0)\n\t\t\t(type default)\n\t\t)\n'
            '\t\t(uuid "%s")\n\t)\n') % (fmt(a[0]), fmt(a[1]), fmt(c[0]), fmt(c[1]), U())
NEW = wire(f1[0], f1[1])                                   # battery+ straight to VDRAIN (fuse lives at the battery now)

# ---------- 2. iteratively remove what was only attached to removed parts --------------------------------------------
def cleanup(s):
    while True:
        B, libp = parse(s)
        pins = []; pw = []
        for st, en, b in B:
            if kind(b) == 'symbol':
                inf = symbol_info(b); pp = [(x, y) for n, x, y in pin_points(inf, libp)]
                (pw if inf['ref'].startswith('#') else pins).append((st, en, pp))
        wires = [(st, en, wire_pts(b)) for st, en, b in B if kind(b) == 'wire']
        labels = [(st, en, item_at(b)) for st, en, b in B if kind(b) in ('label', 'global_label')]
        juncs = [(st, en, item_at(b)) for st, en, b in B if kind(b) == 'junction']
        ncs = [(st, en, item_at(b)) for st, en, b in B if kind(b) == 'no_connect']
        real_pin_pts = {p for _, _, pp in pins for p in pp}
        all_pin_pts = real_pin_pts | {p for _, _, pp in pw for p in pp}
        def touches(p, skip):
            if p in all_pin_pts or any(p == q for _, _, q in labels): return True
            for st, en, (a, c) in wires:
                if st == skip: continue
                if p in (a, c) or on_segment(p, a, c): return True
            return False
        dead = []
        for st, en, (a, c) in wires:
            if not touches(a, st) or not touches(c, st): dead.append((st, en))
        for st, en, pp in pw:                                   # power symbols / PWR_FLAGs
            p = pp[0]
            if not (p in real_pin_pts or any(p in (a, c) or on_segment(p, a, c) for _, _, (a, c) in wires)
                    or sum(1 for _, _, q in pw if q[0] == p) > 1):
                dead.append((st, en))
        for st, en, p in labels:
            if not any(p in (a, c) or on_segment(p, a, c) for _, _, (a, c) in wires) and p not in all_pin_pts:
                dead.append((st, en))
        for st, en, p in juncs:
            n = sum(1 if p in (a, c) else (2 if on_segment(p, a, c) else 0) for _, _, (a, c) in wires) + (1 if p in all_pin_pts else 0)
            if n < 3: dead.append((st, en))
        for st, en, p in ncs:
            if p not in real_pin_pts: dead.append((st, en))
        if not dead: return s
        for st, en in sorted(set(dead), reverse=True): s = s[:st] + s[en:]
i = s.index('\t(sheet_instances'); s = s[:i] + NEW + s[i:]
before = len(blocks(s)); s = cleanup(s); print('cleanup removed %d dangling items' % (before - len(blocks(s))))

# ---------- 3. property changes ----------------------------------------------------------------------------------------
CHANGE = {
    'C11': {'Value': '47uF 100V', 'Footprint': 'Capacitor_SMD:CP_Elec_10x10', 'LCSC': 'C164068'},
    'C21': {'Value': '47uF 100V', 'Footprint': 'Capacitor_SMD:CP_Elec_10x10', 'LCSC': 'C164068'},
    'D8':  {'Value': 'SMCJ54A', 'LCSC': 'C2943841', 'Description': 'Unidirectional TVS 54V standoff (12S pack 50.4 V), 87 V clamp'},
    'R34': {'Value': '180k', 'LCSC': 'C17923'},   # 1206, 200 V; ~480 kHz, on-time 495 ns at 50.4 V
    'L1':  {'Value': '100uH', 'Footprint': 'Inductor_SMD:L_Sunlord_SWPA6045S', 'LCSC': 'C60633'},
}
B, libp = parse(s)
for st, en, b in sorted(B, reverse=True):
    if kind(b) != 'symbol': continue
    ref = symbol_info(b)['ref']
    if ref not in CHANGE: continue
    nb = b
    for prop, val in CHANGE[ref].items():
        nb, n = re.subn(r'(\(property "%s" ")[^"]*(")' % prop, lambda m: m.group(1) + val + m.group(2), nb, count=1)
        assert n == 1, (ref, prop)
    s = s[:st] + nb + s[en:]

# ---------- 4. new symbols ---------------------------------------------------------------------------------------------
def prop(name, val, x, y, hide=False, ang=0):
    return ('\t\t(property "%s" "%s"\n\t\t\t(at %s %s %d)\n%s\t\t\t(show_name no)\n\t\t\t(do_not_autoplace no)\n'
            '\t\t\t(effects\n\t\t\t\t(font\n\t\t\t\t\t(size 1.27 1.27)\n\t\t\t\t)\n\t\t\t)\n\t\t)\n') % (
        name, val, fmt(x), fmt(y), ang, '\t\t\t(hide yes)\n' if hide else '')
def symbol(lib_id, ref, val, x, y, rot, fp, desc, lcsc, pins, power=False):
    o = ('\t(symbol\n\t\t(lib_id "%s")\n\t\t(at %s %s %d)\n\t\t(unit 1)\n\t\t(body_style 1)\n\t\t(exclude_from_sim no)\n'
         '\t\t(in_bom yes)\n\t\t(on_board yes)\n\t\t(in_pos_files yes)\n\t\t(dnp no)\n\t\t(uuid "%s")\n') % (lib_id, fmt(x), fmt(y), rot, U())
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
def junction(p):
    return '\t(junction\n\t\t(at %s %s)\n\t\t(diameter 0)\n\t\t(color 0 0 0 0)\n\t\t(uuid "%s")\n\t)\n' % (fmt(p[0]), fmt(p[1]), U())
def text(t, x, y):
    return ('\t(text "%s"\n\t\t(exclude_from_sim no)\n\t\t(at %s %s 0)\n\t\t(effects\n\t\t\t(font\n\t\t\t\t(size 1.27 1.27)\n\t\t\t)\n'
            '\t\t\t(justify left bottom)\n\t\t)\n\t\t(uuid "%s")\n\t)\n') % (t, fmt(x), fmt(y), U())
R06, C06 = 'Resistor_SMD:R_0603_1608Metric', 'Capacitor_SMD:C_0603_1608Metric'
def two_pin(lib_id, ref, val, x, y, fp, desc, lcsc, top, bot, half):
    """vertical 2-pin part: top/bot = ('pwr', name) or ('lbl', name)"""
    o = symbol(lib_id, ref, val, x, y, 0, fp, desc, lcsc, ('1', '2'))
    for (t, name), py in ((top, y - half), (bot, y + half)):
        o += power(name, x, py) if t == 'pwr' else label(name, (x, py))
    return o

n = ''
# 5 V buck: TPS562201 (TPS562200 symbol, same pinout) at (190.5, 254)
ux, uy = 190.5, 254.0
n += symbol('Regulator_Switching:TPS562200', 'U6', 'TPS562201', ux, uy, 0, 'Package_TO_SOT_SMD:TSOT-23-6',
            '12 V (VBUCK) -> 5 V buck, 2 A, 580 kHz, 4.5-17 V in', 'C90061', ('1', '2', '3', '4', '5', '6'))
n += power('GND', ux, uy + 7.62)                                              # pin 1 GND
for (px, py), nm in (((ux - 10.16, uy - 2.54), None), ((ux - 10.16, uy + 2.54), '5V_EN'),
                     ((ux + 10.16, uy - 2.54), '5V_SW'), ((ux + 10.16, uy), '5V_BST'), ((ux + 10.16, uy + 2.54), '5V_FB')):
    ex = px - 5.08 if px < ux else px + 5.08
    n += wire((px, py), (ex, py)) + (power('VBUCK', ex, py) if nm is None else label(nm, (ex, py)))
n += two_pin('Device:C_Small', 'C69', '10uF 25V', 165.1, 254, 'Capacitor_SMD:C_0805_2012Metric', 'Buck input cap', 'C15850', ('pwr', 'VBUCK'), ('pwr', 'GND'), 2.54)
n += two_pin('Device:R', 'R77', '100k', 152.4, 254, R06, 'EN pull-up', 'C25803', ('pwr', 'VBUCK'), ('lbl', '5V_EN'), 3.81)
n += two_pin('Device:C_Small', 'C70', '100nF', 215.9, 254, C06, 'Bootstrap cap', 'C14663', ('lbl', '5V_BST'), ('lbl', '5V_SW'), 2.54)
n += two_pin('Device:L_Iron', 'L3', '4.7uH', 226.06, 254, 'Inductor_SMD:L_Sunlord_SWPA4030S', '5 V buck inductor 4.7 uH 2 A', 'C57269', ('lbl', '5V_SW'), ('pwr', '+5V'), 3.81)
n += two_pin('Device:C_Small', 'C71', '22uF 25V', 236.22, 254, 'Capacitor_SMD:C_1206_3216Metric', '5 V output cap', 'C12891', ('pwr', '+5V'), ('pwr', 'GND'), 2.54)
n += two_pin('Device:C_Small', 'C72', '22uF 25V', 243.84, 254, 'Capacitor_SMD:C_1206_3216Metric', '5 V output cap', 'C12891', ('pwr', '+5V'), ('pwr', 'GND'), 2.54)
n += two_pin('Device:R', 'R75', '54.9k', 254, 254, R06, 'FB top: 0.768 V x (1 + 54.9k/10k) = 4.98 V', 'C23077', ('pwr', '+5V'), ('lbl', '5V_FB'), 3.81)
n += two_pin('Device:R', 'R76', '10k', 261.62, 254, R06, 'FB bottom', 'C25804', ('lbl', '5V_FB'), ('pwr', 'GND'), 3.81)
n += text('5 V supply (V2): TPS562201 from VBUCK 12 V (DRV8353 buck). Replaces the LM5164 58 V -> 5 V buck.', 152.4, 241.3 + 3.81)

# third bus cap where U3 used to be
n += two_pin('Device:C_Polarized', 'C68', '47uF 100V', 198.12, 101.6, 'Capacitor_SMD:CP_Elec_10x10', 'Bus bulk cap', 'C164068', ('pwr', 'VDRAIN'), ('pwr', 'GND'), 3.81)

# wheel speed sensor: J11 1 = +5V, 2 = SIG, 3 = GND -> D10 (blocks 5 V) -> pull-up R78 to 3.3 V, filter C73 -> WHEEL -> PC6
jy = 228.6
n += symbol('Connector:Conn_01x03_Pin', 'J11', 'Wheel', 152.4, jy, 0, 'Connector_JST:JST_XH_B3B-XH-A_1x03_P2.50mm_Vertical',
            'Wheel speed sensor (hall/reed, open collector): 1=+5V 2=SIG 3=GND', 'C144394', ('1', '2', '3'))
n += wire((157.48, jy - 2.54), (160.02, jy - 2.54)) + power('+5V', 160.02, jy - 2.54)
n += wire((157.48, jy + 2.54), (160.02, jy + 2.54)) + power('GND', 160.02, jy + 2.54)
n += wire((157.48, jy), (166.37, jy))
n += symbol('Device:D_Schottky', 'D10', '1N5819WS', 170.18, jy, 0, 'Diode_SMD:D_SOD-323', 'Wheel input protection', 'C191023', ('1', '2'))
n += wire((173.99, jy), (179.07, jy)) + wire((179.07, jy), (185.42, jy)) + junction((179.07, jy))
n += symbol('Device:R', 'R78', '4.7k', 179.07, jy - 3.81, 0, R06, 'Wheel input pull-up', 'C23162', ('1', '2')) + power('+3V3', 179.07, jy - 7.62)
n += symbol('Device:C_Small', 'C73', '1nF', 185.42, jy + 2.54, 0, C06, 'Wheel input filter', 'C1588', ('1', '2')) + power('GND', 185.42, jy + 5.08)
n += wire((185.42, jy), (190.5, jy)) + label('WHEEL', (190.5, jy)) + junction((185.42, jy))
n += text('Wheel speed sensor (J11) -> PC6 = TIM3_CH1 input capture (same input circuit as the halls)', 152.4, jy - 10.16)

# ---------- 5. Nucleo pins: PC6 -> WHEEL; PB0/PC0/PC2 (freed by phase sensing removal) -> no-connect ------------------
B, libp = parse(s)
u2 = {}
for st, en, b in B:
    if kind(b) == 'symbol' and symbol_info(b)['ref'] == 'U2':
        inf = symbol_info(b)
        lib = s[s.index('(symbol "%s"' % inf['lib_id']):]
        for num, x, y in pin_points(inf, libp): u2[num] = (x, y)
pin_name = {}
for m in re.finditer(r'\(pin \w+ \w+\s*\(at [-\d.]+ [-\d.]+ \d+\).*?\(name "([^"]*)".*?\(number "(CN\d+_\d+)"', s, re.S):
    pin_name[m.group(2)] = m.group(1)
def nc_at(p):
    return '\t(no_connect\n\t\t(at %s %s)\n\t\t(uuid "%s")\n\t)\n' % (fmt(p[0]), fmt(p[1]), U())
for cn, want in (('CN7_34', 'PB0'), ('CN7_38', 'PC0'), ('CN7_35', 'PC2')):
    assert pin_name[cn].startswith(want), (cn, pin_name[cn])
    n += nc_at(u2[cn])
assert pin_name['CN10_4'] == 'PC6', pin_name['CN10_4']
p = u2['CN10_4']
m = re.search(r'\t\(no_connect\n\t\t\(at %s %s\)\n\t\t\(uuid "[^"]+"\)\n\t\)\n' % (re.escape(fmt(p[0])), re.escape(fmt(p[1]))), s)
assert m, 'no NC on PC6'
s = s[:m.start()] + s[m.end():]
side = 1 if p[0] > 67.31 else -1
e = (p[0] + 5.08 * side, p[1])
n += wire(p, e) + label('WHEEL', e if side > 0 else (e[0] - 0.0, e[1]))

# ---------- 6. library symbol for the buck, write ------------------------------------------------------------------------
if '(symbol "Regulator_Switching:TPS562200"' not in s:
    src = open(LIBDIR + 'Regulator_Switching.kicad_sym', encoding='utf-8').read()
    i0 = src.index('\t(symbol "TPS562200"\n'); d = 0; j = i0 + 1
    while True:
        if src[j] == '(': d += 1
        elif src[j] == ')':
            d -= 1
            if d == 0: break
        elif src[j] == '"':
            j += 1
            while src[j] != '"': j += 1
        j += 1
    t = src[i0:j + 1].replace('(symbol "TPS562200"', '(symbol "Regulator_Switching:TPS562200"', 1)
    k = s.index('\t(lib_symbols\n') + len('\t(lib_symbols\n')
    s = s[:k] + '\n'.join('\t' + ln for ln in t.split('\n')) + '\n' + s[k:]
i = s.index('\t(sheet_instances'); s = s[:i] + n + s[i:]
open(f, 'w', encoding='utf-8', newline='\n').write(s)
print('ok, last #PWR', pwr_n[0])
