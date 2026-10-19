"""V2 schematic: inline phase current sensing instead of low-side shunts (run once, after v2_sch_motorpads.py).
 - R14/R15/R16 (2 mOhm) leave the low-side source path: SPx is tied to GND by net ties NT4/NT5/NT6 right at the
   low-side FET source (SPx stays the DRV's low-side gate return / VDS sense, Kelvin). The 1 nF caps C22/C23/C25 go.
   NT1-NT3 (SNx -> GND) stay; the DRV's own current amplifiers are unused, SOA/SOB/SOC get no-connect flags.
 - Each phase: phase node MOTx -> R1x 2 mOhm -> MOTx_OUT -> motor pad (J13 = A, J12 = B, J6 = C).
   INA240A2 (gain 50, -4..80 V common mode, PWM rejection) across R1x: IN+ = MOTx, IN- = MOTx_OUT,
   VS = +3V3, REF1 = +3V3 / REF2 = GND -> output 1.65 V at 0 A, 0.1 V/A (+-16 A), positive = current into the motor.
 - Outputs ISENA/B/C replace SOA/SOB/SOC at the RC filters R57/R56/R55 -> PA4 / PA1 / PA0 (same ADC pins as before).
usage: v2_sch_inline.py <sch>"""
import sys, os, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from schlib import *
f = sys.argv[1]; s = open(f, encoding='utf-8').read()
assert '(reference "U7")' not in s, 'already applied'
ROOT = re.search(r'\(uuid "([^"]+)"\)', s).group(1)
LIBDIR = r"C:/Program Files/KiCad/10.0/share/kicad/symbols/"

# ---------- helpers ---------------------------------------------------------------------------------------------------
def prop(name, val, x, y, hide=False):
    return ('\t\t(property "%s" "%s"\n\t\t\t(at %s %s 0)\n%s\t\t\t(show_name no)\n\t\t\t(do_not_autoplace no)\n'
            '\t\t\t(effects\n\t\t\t\t(font\n\t\t\t\t\t(size 1.27 1.27)\n\t\t\t\t)\n\t\t\t)\n\t\t)\n') % (
        name, val, fmt(x), fmt(y), '\t\t\t(hide yes)\n' if hide else '')
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
    lib = {'GND': 'power:GND', '+3V3': 'power:+3V3'}[kind_]
    return symbol(lib, '#PWR%03d' % pwr_n[0], kind_, x, y, 0, '', 'Power symbol', None, ('1',), power=True)
def label(name, p):
    return ('\t(label "%s"\n\t\t(at %s %s 0)\n\t\t(effects\n\t\t\t(font\n\t\t\t\t(size 1.27 1.27)\n\t\t\t)\n'
            '\t\t\t(justify left bottom)\n\t\t)\n\t\t(uuid "%s")\n\t)\n') % (name, fmt(p[0]), fmt(p[1]), U())
def wire(a, c):
    return ('\t(wire\n\t\t(pts\n\t\t\t(xy %s %s) (xy %s %s)\n\t\t)\n\t\t(stroke\n\t\t\t(width 0)\n\t\t\t(type default)\n\t\t)\n'
            '\t\t(uuid "%s")\n\t)\n') % (fmt(a[0]), fmt(a[1]), fmt(c[0]), fmt(c[1]), U())
def nc_at(p):
    return '\t(no_connect\n\t\t(at %s %s)\n\t\t(uuid "%s")\n\t)\n' % (fmt(p[0]), fmt(p[1]), U())
def text(t, x, y):
    return ('\t(text "%s"\n\t\t(exclude_from_sim no)\n\t\t(at %s %s 0)\n\t\t(effects\n\t\t\t(font\n\t\t\t\t(size 1.27 1.27)\n\t\t\t)\n'
            '\t\t\t(justify left bottom)\n\t\t)\n\t\t(uuid "%s")\n\t)\n') % (t, fmt(x), fmt(y), U())
def cleanup(s):                            # same rules as v2_sch_edit.py
    while True:
        B = blocks(s); libp = lib_pins(s)
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
        for st, en, pp in pw:
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

PH = {'A': dict(sh='R16', cf='C23', nt='NT4', ina='U7', c='C74', pad='J13', rc='R57'),
      'B': dict(sh='R15', cf='C25', nt='NT5', ina='U8', c='C75', pad='J12', rc='R56'),
      'C': dict(sh='R14', cf='C22', nt='NT6', ina='U9', c='C76', pad='J6', rc='R55')}

# ---------- 1. collect, then remove: shunts, 1 nF caps, motor pads, SOx labels at the DRV -----------------------------
B = blocks(s); libp = lib_pins(s)
sym = {symbol_info(b)['ref']: (st, en, b) for st, en, b in B if kind(b) == 'symbol'}
U1 = symbol_info(sym['U1'][2]); u1p = {n: (x, y) for n, x, y in pin_points(U1, libp)}
shunt = {}
for ph, d in PH.items():
    inf = symbol_info(sym[d['sh']][2]); assert inf['rot'] == 0
    b = sym[d['sh']][2]
    shunt[ph] = dict(x=inf['x'], y=inf['y'], lcsc=re.search(r'"LCSC" "([^"]*)"', b).group(1),
                     fp=re.search(r'"Footprint" "([^"]*)"', b).group(1))
cut = [sym[d[k]][:2] for d in PH.values() for k in ('sh', 'cf', 'pad')]
for st, en, b in B:
    if kind(b) == 'label':
        nm = re.search(r'\(label "([^"]+)"', b).group(1)
        if nm in ('SOA', 'SOB', 'SOC') and item_at(b)[0] < 110: cut.append((st, en))     # the DRV-side ones (x 101.6)
for st, en in sorted(cut, reverse=True): s = s[:st] + s[en:]
print('removed', len(cut), 'items')

# ---------- 2. rename the RC-filter-side SOx labels -> ISENx ---------------------------------------------------------
for so, isen in (('SOA', 'ISENA'), ('SOB', 'ISENB'), ('SOC', 'ISENC')):
    s, k = re.subn(r'\(label "%s"' % so, '(label "%s"' % isen, s); assert k == 1, (so, k)

# ---------- 3. new items -------------------------------------------------------------------------------------------------
n = ''
for p in ('23', '24', '25'): n += nc_at(u1p[p])                                    # SOC SOB SOA unused
# net ties SPx -> GND where the shunts were (pins on the old shunt pin points, via 1.27 mm stubs)
NTL = libp['Device:NetTie_2'][0] if 0 in libp['Device:NetTie_2'] else libp['Device:NetTie_2'][1]
for ph, d in PH.items():
    x, y = shunt[ph]['x'], shunt[ph]['y']
    rot = 90
    inf = dict(lib_id='Device:NetTie_2', x=x, y=y, rot=rot, mirror=None, unit=1, ref=d['nt'])
    pts = {num: (px, py) for num, px, py in pin_points(inf, libp)}
    if pts['1'][1] > pts['2'][1]: rot = 270; inf['rot'] = 270; pts = {num: (px, py) for num, px, py in pin_points(inf, libp)}
    n += symbol('Device:NetTie_2', d['nt'], 'NetTie_2', x, y, rot, 'NetTie:NetTie-2_SMD_Pad0.5mm',
                'Kelvin tie: SP%s (DRV low-side source sense / gate return) to GND at the low-side FET source' % ph, None, ('1', '2')).replace('(in_bom yes)', '(in_bom no)').replace('(in_pos_files yes)', '(in_pos_files no)')
    n += wire((x, y - 3.81), pts['1']) + wire(pts['2'], (x, y + 3.81))
i = s.index('	(sheet_instances'); s = s[:i] + n + s[i:]; n = ''
before = len(blocks(s)); s = cleanup(s); print('cleanup removed', before - len(blocks(s)))
# inline sensing blocks: one 72 x 32 mm free spot per phase, nearest to the power section of the sheet
occ = []
for st, en, b in blocks(s):
    k = kind(b)
    if k in ('symbol', 'label', 'junction', 'no_connect', 'text', 'global_label'): occ.append(item_at(b))
    elif k == 'wire': occ += list(wire_pts(b))
BW, BH = 72, 32                           # block spans x bx-2..bx+70, y by-11..by+21
spots = []
for gx in range(5, 415 - BW, 5):
    for gy in range(5, 292 - BH, 5):
        if not any(gx - 4 <= x <= gx + BW + 3 and gy - 4 <= y <= gy + BH + 3 for x, y in occ): spots.append((gx, gy))
spots.sort(key=lambda g: (g[0] - 250) ** 2 + (g[1] - 90) ** 2)
chosen = []
for g in spots:
    if all(abs(g[0] - c[0]) >= BW + 5 or abs(g[1] - c[1]) >= BH + 5 for c in chosen): chosen.append(g)
    if len(chosen) == 3: break
assert len(chosen) == 3, chosen
print('blocks at', chosen)
INA = 'Amplifier_Current:INA240A2PW'
for i, ph in enumerate('ABC'):
    d = PH[ph]; bx, by = round((chosen[i][0] + 2) / 2.54) * 2.54, round((chosen[i][1] + 11) / 2.54) * 2.54
    mot, out, isen = 'MOT' + ph, 'MOT%s_OUT' % ph, 'ISEN' + ph
    # shunt (vertical): pin 1 = phase node, pin 2 = toward the motor pad
    n += symbol('Device:R', d['sh'], '2m', bx, by + 3.81, 0, shunt[ph]['fp'],
                'Phase %s inline shunt 2 mOhm (0.2 W at 10 A); Kelvin-route IN+/IN- from its pads' % ph, shunt[ph]['lcsc'], ('1', '2'))
    n += label(mot, (bx, by)) + label(out, (bx, by + 7.62))
    # motor pad
    n += symbol('Connector:Conn_01x01_Pin', d['pad'], mot, bx + 15.24, by + 7.62, 180,
                'Connector_Wire:SolderWire-2.5sqmm_1x01_D2.4mm_OD4.4mm', 'Motor phase %s wire pad (2.5 mm2)' % ph, None, ('1',))
    n += label(out, (bx + 10.16, by + 7.62))
    # INA240A2
    sx, sy = bx + 40.64, by + 5.08
    n += symbol(INA, d['ina'], 'INA240A2PW', sx, sy, 0, 'Package_SO:TSSOP-8_4.4x3mm_P0.65mm',
                'Phase %s current amplifier, gain 50, -4..80 V CM, PWM rejection' % ph, 'C129949', tuple('12345678'))
    n += wire((sx - 7.62, sy - 2.54), (sx - 12.7, sy - 2.54)) + label(mot, (sx - 12.7, sy - 2.54))      # IN+
    n += wire((sx - 7.62, sy + 2.54), (sx - 12.7, sy + 2.54)) + label(out, (sx - 12.7, sy + 2.54))      # IN-
    n += wire((sx - 2.54, sy - 7.62), (sx - 2.54, sy - 10.16)) + power('+3V3', sx - 2.54, sy - 10.16)  # V+
    n += wire((sx - 2.54, sy + 7.62), (sx - 2.54, sy + 10.16)) + power('GND', sx - 2.54, sy + 10.16)   # GND (1+4)
    n += wire((sx, sy + 7.62), (sx, sy + 10.16)) + power('GND', sx, sy + 10.16)                        # REF2
    n += (wire((sx + 2.54, sy + 7.62), (sx + 2.54, sy + 10.16)) + wire((sx + 2.54, sy + 10.16), (sx + 7.62, sy + 10.16))
          + wire((sx + 7.62, sy + 10.16), (sx + 7.62, sy + 7.62)) + power('+3V3', sx + 7.62, sy + 7.62))     # REF1
    n += wire((sx + 7.62, sy), (sx + 12.7, sy)) + label(isen, (sx + 12.7, sy))                          # OUT
    # supply cap
    cx = sx + 22.86
    n += symbol('Device:C_Small', d['c'], '100nF', cx, sy, 0, 'Capacitor_SMD:C_0603_1608Metric', 'INA240 supply decoupling', 'C14663', ('1', '2'))
    n += wire((cx, sy - 2.54), (cx, sy - 5.08)) + power('+3V3', cx, sy - 5.08)
    n += wire((cx, sy + 2.54), (cx, sy + 5.08)) + power('GND', cx, sy + 5.08)
n += text('Inline phase current sense (V2): 2 mOhm + INA240A2 per phase, 1.65 V at 0 A, 0.1 V/A, + = into the motor. '
          'ISENA/B/C -> R57/R56/R55 RC -> PA4/PA1/PA0. DRV CSAs unused.', round(chosen[0][0] / 2.54) * 2.54, round(chosen[0][1] / 2.54) * 2.54)

# ---------- 4. library symbol (INA240A2PW flattened from INA240A1PW), write ---------------------------------------------
if '(symbol "%s"' % INA not in s:
    src = open(LIBDIR + 'Amplifier_Current.kicad_sym', encoding='utf-8').read()
    i0 = src.index('\t(symbol "INA240A1PW"\n'); dpt = 0; j = i0
    while True:
        if src[j] == '(': dpt += 1
        elif src[j] == ')':
            dpt -= 1
            if dpt == 0: break
        elif src[j] == '"':
            j += 1
            while src[j] != '"':
                if src[j] == '\\': j += 1
                j += 1
        j += 1
    t = src[i0:j + 1]
    assert '(extends' not in t
    t = t.replace('(symbol "INA240A1PW"', '(symbol "%s"' % INA, 1).replace('"INA240A1PW_', '"INA240A2PW_')
    t = t.replace('(property "Value" "INA240A1PW"', '(property "Value" "INA240A2PW"', 1)
    k = s.index('\t(lib_symbols\n') + len('\t(lib_symbols\n')
    s = s[:k] + '\n'.join('\t' + ln for ln in t.split('\n')) + '\n' + s[k:]
i = s.index('\t(sheet_instances'); s = s[:i] + n + s[i:]
open(f, 'w', encoding='utf-8', newline='\n').write(s)
print('ok, last #PWR', pwr_n[0])
