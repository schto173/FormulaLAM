"""JLC-style manufacturing checks that KiCad's DRC does not do (run after a clean DRC, with zones refilled):
  1. silkscreen closer than 0.13 mm (JLC 'danger') to any pad
  2. silkscreen closer than 0.20 mm to any drilled hole
  3. annular rings of vias / plated holes
  4. solder-mask opening of a pad closer than 0.15 mm to a track/via of another net
  5. vias inside SMD pads (only U1's centre pad has intended thermal vias)
  6. GND pour pieces not connected to the main GND plane (In1) through a via / plated pin
  7. GND stitching: distance from GND pour to the nearest GND via / plated GND pin
Run with KiCad's Python:  "C:/Program Files/KiCad/10.0/bin/python.exe" tools/dfm_checks.py bldc-75v.kicad_pcb"""
import pcbnew, sys, math, collections
b = pcbnew.LoadBoard(sys.argv[1]); MM, TO = pcbnew.FromMM, pcbnew.ToMM
fps = list(b.GetFootprints()); tracks = list(b.GetTracks())
vias = [t for t in tracks if t.GetClass() == 'PCB_VIA']

def gap(sa, sb, hi=0.3):
    """smallest clearance (mm) between two shapes, None if more than hi"""
    if not sa.Collide(sb, MM(hi)): return None
    lo = 0.0
    for _ in range(12):
        mid = (lo + hi) / 2
        if sa.Collide(sb, MM(mid)): hi = mid
        else: lo = mid
    return round(lo, 3)

silk = []
for f in fps:
    silk += [(f.GetReference(), g) for g in f.GraphicalItems() if g.GetLayer() == pcbnew.F_SilkS]
    for fld in (f.Reference(), f.Value()):
        if fld.IsVisible() and fld.GetLayer() == pcbnew.F_SilkS: silk.append((f.GetReference(), fld))
silk += [('board', d) for d in b.GetDrawings() if d.GetLayer() == pcbnew.F_SilkS]

# 1. silkscreen to pads
pads = [(f.GetReference(), p) for f in fps for p in f.Pads() if p.IsOnLayer(pcbnew.F_Cu)]
hits = {}
for owner, g in silk:
    sh = g.GetEffectiveShape(pcbnew.F_SilkS); bb = g.GetBoundingBox(); bb.Inflate(MM(1))
    for pr, p in pads:
        if bb.Intersects(p.GetBoundingBox()):
            d = gap(sh, p.GetEffectiveShape(pcbnew.F_Cu), 0.13)
            if d is not None: hits[(owner, pr)] = min(hits.get((owner, pr), 9), d)
print('1. silkscreen < 0.13 mm from a pad: %d' % len(hits))
for (o, pr), d in sorted(hits.items(), key=lambda kv: kv[1])[:10]: print('     %s silk -> pad of %s  %.3f mm' % (o, pr, d))

# 2. silkscreen to holes
holes = [(t.GetPosition(), t.GetDrillValue() / 2, 'via ' + t.GetNetname()) for t in vias]
holes += [(p.GetPosition(), max(p.GetDrillSize().x, p.GetDrillSize().y) / 2, '%s.%s' % (f.GetReference(), p.GetNumber()))
          for f in fps for p in f.Pads() if p.GetDrillSize().x > 0]
found = []
for owner, g in silk:
    sh = g.GetEffectiveShape(pcbnew.F_SilkS); bb = g.GetBoundingBox(); bb.Inflate(MM(1))
    for pos, r, name in holes:
        if bb.Contains(pos):
            d = gap(sh, pcbnew.SHAPE_CIRCLE(pos, int(r)), 0.20)
            if d is not None: found.append((d, owner, name, round(TO(pos.x), 2), round(TO(pos.y), 2)))
print('2. silkscreen < 0.20 mm from a drilled hole: %d' % len(found))
for x in sorted(found)[:10]: print('    ', x)

# 3. annular rings
rings = collections.Counter()
for t in vias: rings['via %.2f/%.2f -> ring %.3f' % (TO(t.GetWidth(pcbnew.F_Cu)), TO(t.GetDrillValue()), TO(t.GetWidth(pcbnew.F_Cu) - t.GetDrillValue()) / 2)] += 1
for f in fps:
    for p in f.Pads():
        if p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH:
            r = TO(min(p.GetSize(pcbnew.F_Cu).x, p.GetSize(pcbnew.F_Cu).y) - p.GetDrillSize().x) / 2
            if r < 0.16: rings['plated pad %s ring %.3f' % (f.GetReference(), r)] += 1
print('3. annular rings (JLC warns at 0.15 mm, harmless for their standard 0.6/0.3 via):')
for k, n in sorted(rings.items()): print('     %-40s x%d' % (k, n))

# 4. mask openings vs other-net copper
for side, cu, mk in (('top', pcbnew.F_Cu, pcbnew.F_Mask), ('bottom', pcbnew.B_Cu, pcbnew.B_Mask)):
    cop = [t for t in tracks if (t.GetClass() == 'PCB_VIA' or t.GetLayer() == cu)]
    res = []
    for f in fps:
        for p in f.Pads():
            if not p.IsOnLayer(mk): continue
            ap = pcbnew.SHAPE_POLY_SET(); p.TransformShapeToPolygon(ap, cu, 0, MM(0.005), pcbnew.ERROR_OUTSIDE)
            bb = p.GetBoundingBox(); bb.Inflate(MM(0.5))
            for t in cop:
                if t.GetNetCode() == p.GetNetCode() or not bb.Intersects(t.GetBoundingBox()): continue
                d = gap(ap, t.GetEffectiveShape(cu), 0.15)
                if d is not None: res.append((d, f.GetReference() + ':' + p.GetNumber(), t.GetNetname()))
    print('4. %s mask opening < 0.15 mm from other-net track/via: %d' % (side, len(res)))
    for r in sorted(res)[:10]: print('    ', r)

# 5. vias in SMD pads
vip = [(f.GetReference(), p.GetNumber(), round(TO(t.GetPosition().x), 2), round(TO(t.GetPosition().y), 2)) for t in vias for f in fps for p in f.Pads()
       if p.GetAttribute() == pcbnew.PAD_ATTRIB_SMD and p.HitTest(t.GetPosition()) and not (f.GetReference() == 'U1' and p.GetNumber() == '49')]
print('5. vias inside SMD pads (other than U1 centre pad): %d' % len(vip), vip[:10])

# 6. isolated GND pour pieces
cu_layers = [pcbnew.F_Cu, pcbnew.In1_Cu, pcbnew.In2_Cu, pcbnew.B_Cu]
nodes = []
for z in b.Zones():
    if z.GetIsRuleArea() or z.GetNetname() != 'GND': continue
    for l in cu_layers:
        if z.IsOnLayer(l):
            fp = z.GetFilledPolysList(l)
            for i in range(fp.OutlineCount()): nodes.append((l, fp.Outline(i)))
parent = list(range(len(nodes)))
def find(a):
    while parent[a] != a: parent[a] = parent[parent[a]]; a = parent[a]
    return a
gnd_drills = [t.GetPosition() for t in vias if t.GetNetname() == 'GND']
gnd_drills += [p.GetPosition() for f in fps for p in f.Pads() if p.GetNetname() == 'GND' and p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH]
for pt in gnd_drills:
    hit = [i for i, (l, o) in enumerate(nodes) if o.PointInside(pt) or o.SquaredDistance(pt) < MM(0.8) ** 2]
    for i in hit[1:]: parent[find(i)] = find(hit[0])
main = find(max(range(len(nodes)), key=lambda i: nodes[i][1].Area() if nodes[i][0] == pcbnew.In1_Cu else 0))
iso = [(pcbnew.LayerName(l), o) for i, (l, o) in enumerate(nodes) if find(i) != main]
print('6. GND pour pieces not tied to the GND plane: %d (slivers < 0.1 mm2 between two GND pads are harmless)' % len(iso))
for l, o in iso:
    bb = o.BBox(); print('     %-6s (%.1f, %.1f)-(%.1f, %.1f)  %.2f mm2' % (l, TO(bb.GetLeft()), TO(bb.GetTop()), TO(bb.GetRight()), TO(bb.GetBottom()), o.Area() / 1e12))

# 7. stitching density
pts = [(TO(p.x), TO(p.y)) for p in gnd_drills]
for name, lid in (('top', pcbnew.F_Cu), ('inner2', pcbnew.In2_Cu), ('bottom', pcbnew.B_Cu)):
    ds = []
    for l, o in nodes:
        if l != lid: continue
        bb = o.BBox(); x = TO(bb.GetLeft())
        while x <= TO(bb.GetRight()):
            y = TO(bb.GetTop())
            while y <= TO(bb.GetBottom()):
                if o.PointInside(pcbnew.VECTOR2I(MM(x), MM(y))): ds.append(min(math.hypot(px - x, py - y) for px, py in pts))
                y += 2.0
            x += 2.0
    ds.sort()
    if ds: print('7. %-6s GND pour: median %.1f mm, 95%% within %.1f mm, worst %.1f mm to the nearest GND via/pin' % (name, ds[len(ds) // 2], ds[int(len(ds) * .95)], ds[-1]))
