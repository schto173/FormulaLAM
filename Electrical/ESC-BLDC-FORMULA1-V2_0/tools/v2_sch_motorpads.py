"""V2 schematic: split the 3-pin motor connector J6 into three single solder-wire pads (J6 / J12 / J13), so each phase
cell of the pinwheel power stage gets its own pad. The new symbols sit with their pins exactly on J6's old pin points,
so the existing wires (MOTC / MOTB / MOTA) stay connected.  Run once.  usage: v2_sch_motorpads.py <sch>"""
import sys, os, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from schlib import *
f = sys.argv[1]; s = open(f, encoding='utf-8').read()
assert '(reference "J12")' not in s, 'already applied'
ROOT = re.search(r'\(uuid "([^"]+)"\)', s).group(1)
LIBDIR = r"C:/Program Files/KiCad/10.0/share/kicad/symbols/"
FP = 'Connector_Wire:SolderWire-2.5sqmm_1x01_D2.4mm_OD4.4mm'

def prop(name, val, x, y, hide=False):
    return ('\t\t(property "%s" "%s"\n\t\t\t(at %s %s 0)\n%s\t\t\t(show_name no)\n\t\t\t(do_not_autoplace no)\n'
            '\t\t\t(effects\n\t\t\t\t(font\n\t\t\t\t\t(size 1.27 1.27)\n\t\t\t\t)\n\t\t\t)\n\t\t)\n') % (
        name, val, fmt(x), fmt(y), '\t\t\t(hide yes)\n' if hide else '')
def symbol(ref, val, x, y, desc):
    o = ('\t(symbol\n\t\t(lib_id "Connector:Conn_01x01_Pin")\n\t\t(at %s %s 180)\n\t\t(unit 1)\n\t\t(body_style 1)\n'
         '\t\t(exclude_from_sim no)\n\t\t(in_bom yes)\n\t\t(on_board yes)\n\t\t(in_pos_files yes)\n\t\t(dnp no)\n\t\t(uuid "%s")\n') % (fmt(x), fmt(y), U())
    o += prop('Reference', ref, x + 1.27, y - 1.27) + prop('Value', val, x + 3.81, y)
    o += prop('Footprint', FP, x, y, True) + prop('Datasheet', '', x, y, True) + prop('Description', desc, x, y, True)
    o += '\t\t(pin "1"\n\t\t\t(uuid "%s")\n\t\t)\n' % U()
    o += ('\t\t(instances\n\t\t\t(project "bldc-75v"\n\t\t\t\t(path "/%s"\n\t\t\t\t\t(reference "%s")\n\t\t\t\t\t(unit 1)\n'
          '\t\t\t\t)\n\t\t\t)\n\t\t)\n\t)\n') % (ROOT, ref)
    return o

B = blocks(s)
j6 = [(st, en, b) for st, en, b in B if kind(b) == 'symbol' and symbol_info(b)['ref'] == 'J6']
assert len(j6) == 1
st, en, b = j6[0]; inf = symbol_info(b)
assert inf['lib_id'] == 'Connector:Conn_01x03_Pin' and inf['rot'] == 180 and inf['mirror'] is None
x, y = inf['x'], inf['y']                  # 01x03 at rot 180: pin 1 at y+2.54, 2 at y, 3 at y-2.54 (x-5.08)
n = (symbol('J6', 'MOTC', x, y + 2.54, 'Motor phase C wire pad (2.5 mm2)') +
     symbol('J12', 'MOTB', x, y, 'Motor phase B wire pad (2.5 mm2)') +
     symbol('J13', 'MOTA', x, y - 2.54, 'Motor phase A wire pad (2.5 mm2)'))
s = s[:st] + s[en:]
if '(symbol "Connector:Conn_01x01_Pin"' not in s:
    src = open(LIBDIR + 'Connector.kicad_sym', encoding='utf-8').read()
    i0 = src.index('\t(symbol "Conn_01x01_Pin"\n'); d = 0; j = i0
    while True:
        if src[j] == '(': d += 1
        elif src[j] == ')':
            d -= 1
            if d == 0: break
        elif src[j] == '"':
            j += 1
            while src[j] != '"': j += 1
        j += 1
    t = src[i0:j + 1].replace('(symbol "Conn_01x01_Pin"', '(symbol "Connector:Conn_01x01_Pin"', 1)
    assert '(extends' not in t
    k = s.index('\t(lib_symbols\n') + len('\t(lib_symbols\n')
    s = s[:k] + '\n'.join('\t' + ln for ln in t.split('\n')) + '\n' + s[k:]
i = s.index('\t(sheet_instances'); s = s[:i] + n + s[i:]
open(f, 'w', encoding='utf-8', newline='\n').write(s)
print('ok: J6 split at', x, y)
