"""Tiny KiCad 10 schematic (.kicad_sch) editing helpers for scripted changes on a single-sheet design.
Top-level items are handled as text blocks; pin positions are computed from the embedded lib_symbols."""
import re, math, uuid

def fmt(v):
    return ('%.4f' % v).rstrip('0').rstrip('.')

def blocks(s):
    """top-level '\\t(' items of the root list: [(start, end, text)]"""
    out, i = [], 0
    while True:
        i = s.find('\n\t(', i)
        if i < 0: break
        st = i + 1; d = 0; j = st
        while True:
            c = s[j]
            if c == '(':
                d += 1
            elif c == ')':
                d -= 1
                if d == 0: break
            elif c == '"':                      # skip strings
                j += 1
                while s[j] != '"':
                    if s[j] == '\\': j += 1
                    j += 1
            j += 1
        out.append((st, j + 1, s[st:j + 1]))
        i = j + 1
    return out

def kind(b):
    return re.match(r'\t\((\w+)', b).group(1)

def lib_pins(s):
    """lib_id -> {unit: [(number, x, y)]} ; unit 0 = common to all units"""
    lib = s[s.index('\t(lib_symbols'):]
    res = {}
    for m in re.finditer(r'\n\t\t\(symbol "([^"]+)"', lib):
        name = m.group(1); st = m.start() + 1
        d = 0; j = st
        while True:
            if lib[j] == '(': d += 1
            elif lib[j] == ')':
                d -= 1
                if d == 0: break
            elif lib[j] == '"':
                j += 1
                while lib[j] != '"': j += 1
            j += 1
        body = lib[st:j]
        short = name.split(':')[-1]
        units = {}
        for sm in re.finditer(r'\(symbol "%s_(\d+)_(\d+)"' % re.escape(short), body):
            u = int(sm.group(1)); k = sm.end(); d = 1
            while d:
                if body[k] == '(': d += 1
                elif body[k] == ')': d -= 1
                k += 1
            sub = body[sm.start():k]
            for pm in re.finditer(r'\(pin \w+ \w+\s*\(at ([-\d.]+) ([-\d.]+) [-\d.]+\).*?\(number "([^"]*)"', sub, re.S):
                units.setdefault(u, []).append((pm.group(3), float(pm.group(1)), float(pm.group(2))))
        res[name] = units
        if lib.find('\n\t)', j) == -1: break
    return res

def symbol_info(b):
    lib_id = re.search(r'\(lib_id "([^"]+)"\)', b).group(1)
    at = re.search(r'\n\t\t\(at ([-\d.]+) ([-\d.]+) ([-\d.]+)\)', b)
    x, y, r = float(at.group(1)), float(at.group(2)), int(float(at.group(3)))
    mir = re.search(r'\n\t\t\(mirror (\w)\)', b)
    unit = int(re.search(r'\n\t\t\(unit (\d+)\)', b).group(1))
    ref = re.search(r'\(property "Reference" "([^"]*)"', b).group(1)
    return dict(lib_id=lib_id, x=x, y=y, rot=r, mirror=mir.group(1) if mir else None, unit=unit, ref=ref)

def pin_points(info, libp):
    pts = []
    units = libp.get(info['lib_id'], {})
    for u in (0, info['unit']):
        for num, px, py in units.get(u, []):
            dx, dy = px, -py
            r = info['rot'] % 360
            if r == 90: dx, dy = dy, -dx
            elif r == 180: dx, dy = -dx, -dy
            elif r == 270: dx, dy = -dy, dx
            if info['mirror'] == 'x': dy = -dy
            elif info['mirror'] == 'y': dx = -dx
            pts.append((num, round(info['x'] + dx, 3), round(info['y'] + dy, 3)))
    return pts

def wire_pts(b):
    m = re.search(r'\(xy ([-\d.]+) ([-\d.]+)\) \(xy ([-\d.]+) ([-\d.]+)\)', b)
    return (round(float(m.group(1)), 3), round(float(m.group(2)), 3)), (round(float(m.group(3)), 3), round(float(m.group(4)), 3))

def item_at(b):
    m = re.search(r'\n\t\t\(at ([-\d.]+) ([-\d.]+)', b)
    return (round(float(m.group(1)), 3), round(float(m.group(2)), 3))

def on_segment(p, a, c):
    (px, py), (ax, ay), (cx, cy) = p, a, c
    if abs((cx - ax) * (py - ay) - (cy - ay) * (px - ax)) > 1e-3: return False
    return min(ax, cx) - 1e-3 <= px <= max(ax, cx) + 1e-3 and min(ay, cy) - 1e-3 <= py <= max(ay, cy) + 1e-3

def U():
    return str(uuid.uuid4())
