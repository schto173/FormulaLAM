"""V2 board from the V1 board: strip routing/zones/graphics, sync footprints + nets with the V2 netlist, new outline.
Existing footprints are kept (they carry V1 fixes, e.g. 1.0 mm JST holes); only changed/new ones are loaded.
Reads every value first and removes items before adding new ones (SWIG proxies go stale after Remove).
usage (KiCad python): v2_pcb_prepare.py <in.kicad_pcb> <netlist.xml> <out.kicad_pcb>"""
import pcbnew, sys, os
import xml.etree.ElementTree as ET
b = pcbnew.LoadBoard(sys.argv[1]); MM, TO = pcbnew.FromMM, pcbnew.ToMM
here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STD = r"C:/Program Files/KiCad/10.0/share/kicad/footprints/"
def fplib(nick):
    if nick == 'bldc-hat': return os.path.join(here, 'bldc-hat.pretty')
    return STD + nick + '.pretty'

r = ET.parse(sys.argv[2]).getroot()
comps = {}
for c in r.find('components'):
    fields = {fl.get('name'): (fl.text or '') for fl in c.find('fields')} if c.find('fields') is not None else {}
    comps[c.get('ref')] = dict(value=c.findtext('value'), fp=c.findtext('footprint'), uuid=c.findtext('tstamps').strip(),
                               lcsc=fields.get('LCSC', ''), desc=c.findtext('description') or '')
pinnet = {}
import re
def kicad_name(nm):                       # KiCad stores '/' inside auto-generated net names as {slash}
    m = re.match(r'^(Net-|unconnected-)\((.*)\)$', nm)
    return '%s(%s)' % (m.group(1), m.group(2).replace('/', '{slash}')) if m else nm
for n in r.find('nets'):
    for x in n: pinnet[(x.get('ref'), x.get('pin'))] = kicad_name(n.get('name'))

# ---- 1. strip -------------------------------------------------------------------------------------------------------
tracks = list(b.GetTracks()); zones = list(b.Zones()); draws = list(b.GetDrawings())
fps = {f.GetReference(): f for f in b.GetFootprints()}
drop = [f for ref, f in fps.items() if ref not in comps]
swap = [ref for ref, f in fps.items() if ref in comps and f.GetFPIDAsString() != comps[ref]['fp']]
keep_pos = {ref: (fps[ref].GetPosition().x, fps[ref].GetPosition().y, fps[ref].GetOrientationDegrees()) for ref in swap}
print('remove: %d tracks/vias, %d zones, %d drawings, %d footprints %s' % (len(tracks), len(zones), len(draws), len(drop),
      sorted(f.GetReference() for f in drop)))
print('swap footprints:', {ref: comps[ref]['fp'].split(':')[1] for ref in swap})
for item in tracks + zones + draws + drop + [fps[ref] for ref in swap]: b.Remove(item)

# ---- 2. add swapped + new footprints ---------------------------------------------------------------------------------
present = {f.GetReference() for f in b.GetFootprints()}
added = []
for ref, c in sorted(comps.items()):
    if ref in present: continue
    nick, name = c['fp'].split(':')
    f = pcbnew.FootprintLoad(fplib(nick), name)
    assert f is not None, c['fp']
    f.SetFPIDAsString(c['fp']); f.SetReference(ref); f.SetValue(c['value'])
    x, y, rot = keep_pos.get(ref, (MM(100), MM(140), 0))       # new parts start below the board
    f.SetPosition(pcbnew.VECTOR2I(int(x), int(y))); f.SetOrientationDegrees(rot)
    f.SetPath(pcbnew.KIID_PATH('/' + c['uuid'])); f.SetSheetfile('bldc-75v.kicad_sch'); f.SetSheetname('')
    if c['lcsc']: f.SetField('LCSC', c['lcsc'])
    if c['desc']: f.SetField('Description', c['desc'])
    b.Add(f); added.append(ref)
print('added:', added)

# ---- 3. values/fields + all pad nets from the netlist ------------------------------------------------------------------
def net(name):
    n = b.FindNet(name)
    if n is None:
        n = pcbnew.NETINFO_ITEM(b, name); b.Add(n)
    return n
missing = []
for f in b.GetFootprints():
    ref = f.GetReference(); c = comps[ref]
    f.SetValue(c['value'])
    if c['lcsc']: f.SetField('LCSC', c['lcsc'])
    if c['desc']: f.SetField('Description', c['desc'])
    if ref in ('J5', 'J6', 'J12', 'J13'): f.SetExcludedFromBOM(False)   # wire pads: symbol is in the BOM (not assembled via lcsc_map)
    for fld in ('LCSC', 'Description'):
        if f.HasField(fld): f.GetField(fld).SetVisible(False)
    for p in f.Pads():
        num = p.GetNumber()
        if not num: continue
        nm = pinnet.get((ref, num))
        if nm is None: missing.append((ref, num)); continue
        p.SetNet(net(nm))
print('pads without a netlist entry:', missing[:20])

# ---- 4. outline: HAT over the Nucleo MCU end + power-stage extension away from the USB -----------------------------------
x0, y0, x1, y1 = 143.00, 44.00, 253.50, 134.00     # from just left of the Nucleo holes (ST-LINK end free) to the power-stage extension; connector strips above CN10 and below CN7
for (ax, ay), (bx, by) in (((x0, y0), (x1, y0)), ((x1, y0), (x1, y1)), ((x1, y1), (x0, y1)), ((x0, y1), (x0, y0))):
    s = pcbnew.PCB_SHAPE(b); s.SetShape(pcbnew.SHAPE_T_SEGMENT); s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(MM(0.1))
    s.SetStart(pcbnew.VECTOR2I(MM(ax), MM(ay))); s.SetEnd(pcbnew.VECTOR2I(MM(bx), MM(by))); b.Add(s)
b.Save(sys.argv[3]); print('saved, footprints:', len(list(b.GetFootprints())))
