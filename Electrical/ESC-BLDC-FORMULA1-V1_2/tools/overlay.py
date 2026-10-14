"""Check pick-and-place rotation/offset: place JLC's own (EasyEDA) footprint for an LCSC part the way JLC would from
our CPL row and measure how far each of its pads lands from our KiCad pad. 0.0-0.3 mm = correct. If another extra
rotation fits much better, add that rotation to CORR in gen_jlc.py.
usage ("C:/Program Files/KiCad/10.0/bin/python.exe"):
    tools/overlay.py bldc-75v.kicad_pcb jlcpcb/bldc-75v_cpl_jlc.csv J8=C144397 U5=C38695 ...
EasyEDA footprint data is downloaded once into tools/.ee_cache/."""
import pcbnew, sys, os, json, csv, math, subprocess
b = pcbnew.LoadBoard(sys.argv[1]); TO = pcbnew.ToMM
cpl = {r['Designator']: r for r in csv.DictReader(open(sys.argv[2], encoding='utf-8'))}
cache = os.path.join(os.path.dirname(os.path.abspath(__file__)), '.ee_cache'); os.makedirs(cache, exist_ok=True)
U = 0.254  # EasyEDA unit = 10 mil
def easyeda(code):
    p = os.path.join(cache, code + '.json')
    if not os.path.exists(p):
        url = 'https://easyeda.com/api/products/%s/components?version=6.4.19.5' % code
        subprocess.run(['curl', '-s', '-m', '30', '-o', p, url], check=True)   # EasyEDA blocks Python's urllib
    return json.load(open(p, encoding='utf-8'))['result']['packageDetail']
for arg in sys.argv[3:]:
    ref, code = arg.split('=')
    pk = easyeda(code); d = pk['dataStr']
    hx, hy = float(d['head']['x']), float(d['head']['y'])
    ee = {}
    for sh in d['shape']:
        f = sh.split('~')
        if f[0] == 'PAD': ee.setdefault(f[8], []).append(((float(f[2]) - hx) * U, (float(f[3]) - hy) * U))
    kp = {}
    for p in b.FindFootprintByReference(ref).Pads():
        if p.GetNumber(): kp.setdefault(p.GetNumber(), []).append((TO(p.GetPosition().x), TO(p.GetPosition().y)))
    row = cpl[ref]; cx, cy, r0 = float(row['Mid X']), -float(row['Mid Y']), float(row['Rotation'])
    res = []
    for extra in (0, 90, 180, 270):
        a = math.radians(r0 + extra); worst = 0
        for num, pts in ee.items():
            if num not in kp: continue
            for ex, ey in pts:
                x = cx + ex * math.cos(a) + ey * math.sin(a); y = cy - ex * math.sin(a) + ey * math.cos(a)
                worst = max(worst, min(math.hypot(x - kx, y - ky) for kx, ky in kp[num]))
        res.append((worst, extra))
    print('%-4s %-9s %-34s CPL rot %5s | worst pad offset as is: %.3f mm | best extra rotation %3d -> %.3f mm' % (
        ref, code, pk['title'][:34], row['Rotation'], res[0][0], min(res)[1], min(res)[0]))
