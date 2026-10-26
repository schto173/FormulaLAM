"""V2 placement. Board = HAT over the MCU end of the NUCLEO-64 (ST-LINK/USB end left free) + power-stage extension on the
side away from the USB, + connector strips above CN10 and below CN7.  Outline x 143..241, y 44..134 (v2_pcb_prepare.py).
Nucleo pins: DRV logic / halls / CAN / wheel / temps on CN10 (top), UART / +5V / sense / throttle / VSENVM on CN7 (bottom).

Power stage = "pinwheel" around the DRV8353 (rotated 135 deg): one half-bridge cell per DRV pin group, each cell turned so
its gate entry faces those pins -> every phase gets the same short gate/sense path (phase B: gate corner, phase C: up-right
side, phase A: down-right side).  Cell (drawn for phase B, LS at 0,0):
    LS FET (source+gate left, drain right) over HS FET rotated 180 (drain left, source+gate right), 8.25 mm apart.
    Phase node + motor wire pad on the right; shunt (Kelvin, net tie under its body) on the LS source, its 1 nF filter
    above it; MLCCs between the HS drain (VDRAIN) and GND right beside them, the 1 uF one under them -> tight
    commutation loop.  Both gate pins face the gap between the FETs;
    the gate/sense bundle enters that gap from the cell's "mouth" (left, between shunt and MLCCs).
Bulk caps + battery pads + TVS in the free corners next to the cells (joined by an inner VDRAIN plane), M3 holes in the 4
board corners (the 3 Nucleo holes are kept for standoffs too).
usage (KiCad python): v2_place.py <in.kicad_pcb> <out.kicad_pcb>"""
import pcbnew, sys, math
b = pcbnew.LoadBoard(sys.argv[1]); MM = pcbnew.FromMM
fp = {f.GetReference(): f for f in b.GetFootprints()}
placed = set()

def P(ref, x, y, rot=0):
    f = fp[ref]; f.SetOrientationDegrees(rot % 360); f.SetPosition(pcbnew.VECTOR2I(MM(x), MM(y))); placed.add(ref)

def rot_for(vx, vy):                       # KiCad angle that turns the footprint's +x axis onto screen vector (vx, vy)
    return round(math.degrees(math.atan2(-vy, vx)), 3) % 360

def turn(p, a):                            # rotate an offset like KiCad rotates a footprint by a degrees
    c, s = round(math.cos(math.radians(a)), 6), round(math.sin(math.radians(a)), 6)
    return p[0] * c + p[1] * s, -p[0] * s + p[1] * c

# ---------------- DRV8353 ----------------------------------------------------------------------------------------------
UX, UY = 205.0, 95.0
P('U1', UX, UY, 135)
S2 = math.sqrt(0.5)
NRM = {1: (S2, S2), 2: (S2, -S2), 3: (-S2, -S2), 4: (-S2, S2)}   # outward normal of pin sides 1-12, 13-24, 25-36, 37-48
# side frame: d = distance from the chip centre (pads at 3.44, courtyard 4.16), t = along the side, + toward the
# lowest pin number of that side; pin k sits at t = 2.75 - 0.5 * (k - first pin of the side)
def F(side, t, d):
    nx, ny = NRM[side]
    return UX + nx * d - ny * t, UY + ny * d + nx * t
def R(ref, side, t, d, inner=1):
    x, y = F(side, t, d); nx, ny = NRM[side]
    if inner == 2: nx, ny = -nx, -ny     # radial part: pad 1 faces the chip unless inner=2
    P(ref, x, y, rot_for(nx, ny))
def D(ref, dx, dy, rot):                   # fixed offset from the DRV centre
    P(ref, UX + dx, UY + dy, rot)

# ---------------- half-bridge cells ------------------------------------------------------------------------------------
CELL = [('ls', (0, 0), 0), ('hs', (0, 8.25), 180), ('sh', (-7.6, -0.64), 180), ('nt', (-8.95, -0.64), 180),
        ('cf', (-8.0, -3.55), 180), ('cer0', (-5.5, 7.2), 90), ('cer1', (-8.9, 7.2), 90), ('c1u', (-7.2, 11.0), 0),
        ('pad', (7.2, 4.1), 0)]
MOUTH = (-11.6, 3.1)                       # gate bundle entry, between the shunt and the MLCCs (nothing else in front)
LEG = {'A': dict(hs='Q1', ls='Q2', sh='R16', cer0='C58', cer1='C59', c1u='C20', cf='C23', nt='NT1', pad='J13'),
       'B': dict(hs='Q3', ls='Q4', sh='R15', cer0='C60', cer1='C61', c1u='C19', cf='C25', nt='NT2', pad='J12'),
       'C': dict(hs='Q5', ls='Q6', sh='R14', cer0='C62', cer1='C63', c1u='C18', cf='C22', nt='NT3', pad='J6')}
ax, ay = F(1, -1.75, 13.0)
ENTRY = {'B': (F(2, 2.0, 9.0), 0),         # phase B pins 13-17 at the gate corner -> cell to the right
         'C': (F(2, -0.75, 15.8), 90),     # phase C pins 18-22 on the up-right side -> cell above, mouth down
         'A': ((ax + 4.5, ay), 270)}       # phase A pins 8-12 on the down-right side -> cell below, mouth up
                                           # (3.5 mm right of the straight line: clears the VGLS/VCP caps of pins 1-7)
LS = {}
for ph, ((ex, ey), a) in ENTRY.items():
    mx, my = turn(MOUTH, a); lx, ly = ex - mx, ey - my; LS[ph] = (lx, ly)
    for key, off, r in CELL:
        dx, dy = turn(off, a)
        P(LEG[ph][key], lx + dx, ly + dy, r + a)
P('TH1', LS['B'][0] + 1.5, LS['B'][1] - 3.6, 0)          # FET temperature, above the phase-B low-side FET

# ---------------- DRV satellites --------------------------------------------------------------------------------------
# side 1 (pins 1-7 = GND VGLS CPL CPH VM VDRAIN VCP; 8-12 = phase A gate/sense -> keep t < -0.5 free)
R('C27', 1, 0.5, 6.3, inner=1)             # VDRAIN (pin 6 straight in) - VCP (pin 7 passes on the gate side)
R('C29', 1, 3.0, 6.3, inner=2)             # VGLS (board #1 failure) - nearest cap; GND pin 1 joins the pad 49
R('C28', 1, 0.6, 9.6, inner=2)             # VM (= VBUCK); VM/CPH/CPL leave between C27 and C29
R('C30', 1, 2.2, 9.6, inner=1)             # CPL-CPH flying cap
R('C33', 1, 5.0, 9.6, inner=1)            # VDRAIN 1 uF 100 V
# side 3 (VREF next to AGND)
R('C26', 3, 1.95, 6.3, inner=2)
# side 4 (37-39 logic -> escape toward t+, 42 SW -> straight out, 43-48 VIN VCC BST RCL RT FB fan out toward t-)
R('C32', 4, 1.45, 6.3, inner=1)            # DVDD
R('C31', 4, -1.0, 6.3, inner=2)            # VCC
R('R33', 4, -4.6, 6.3, inner=2)            # FB divider (VBUCK side)
R('R29', 4, -6.3, 6.3, inner=1)            # FB divider (GND side)
R('C35', 4, -1.05, 10.0, inner=1)          # buck VIN 100 nF 100 V: VIN pin 43 runs straight into it beside SW
R('C37', 4, -2.9, 9.6, inner=1)            # BST-SW (BST fans out, SW end via a short jump)
R('R36', 4, -4.6, 9.6, inner=2)            # RCL
R('R34', 4, -6.8, 10.3, inner=2)           # RT/SD 180k
R('C34', 4, -8.85, 9.6, inner=1)           # FB ripple injection
# DRV buck power parts (SW leaves pin 42 straight down-left)
D('L1', -13.4, 12.0, 180)                  # SW pad right, VBUCK pad left
D('D2', -8.6, 13.0, 270)                   # SW pad top, GND pad bottom
D('C24', -13.4, 16.7, 180)
D('R37', -13.3, 6.9, 0); D('C36', -17.4, 6.9, 0)
D('TP5', -16.7, 16.65, 0)                  # VBUCK 12 V
D('TP3', -4.0, 19.5, 0); D('TP4', -1.3, 19.5, 0)        # VGLS, VCP (below the DRV, outside the cap rings)

# ---------------- power corners: battery, TVS, bulk caps ----------------------------------------------------------------
P('J5', 221.0, 50.5, 0)                    # battery wires from the top edge: GND pad left, VDRAIN pad right
P('D8', 209.0, 50.5, 180)                  # TVS SMCJ54A beside the battery pads (VDRAIN end toward J5)
# bus caps: 3x 330 uF 100 V THT (16 mm, footprint origin = + pad, body centre 3.75 mm to the right)
P('C68', 234.0 - 3.75, 66.0, 0)            # beside cell C
P('C11', 243.6 - 3.75, 83.0, 0)            # right of cell B
P('C21', 236.5 - 3.75, 108.5, 0)           # beside cell A
P('TP1', 233.0, 122.0, 0)                  # scope GND loop beside the power stage
P('R13', 205.5, 129.0, 0)                  # VSENVM top resistor (383k from VDRAIN)

# ---------------- 5 V buck (TPS562201) below-left of the DRV, output toward CN7_6 ----------------------------------------
def B5(ref, x, y, rot):
    P(ref, x + 35.0, y - 3.0, rot)
B5('U6', 131.0, 117.0, 180)
B5('C69', 133.3, 114.1, 0); B5('L3', 136.0, 117.9, 0); B5('C70', 131.0, 119.55, 0)
B5('C71', 139.6, 118.0, 90); B5('C72', 142.2, 118.0, 90)
B5('R75', 127.3, 116.5, 90); B5('R76', 125.5, 116.5, 90); B5('R77', 127.3, 113.0, 0)
B5('TP6', 145.3, 119.0, 0)                 # +5V

# ---------------- between the headers -------------------------------------------------------------------------------------
P('R69', 164.2, 66.8, 90); P('C64', 166.0, 66.8, 90)          # NTC_TEMP -> CN10_13
P('R73', 167.8, 66.8, 90); P('R74', 169.6, 66.8, 90); P('C67', 171.4, 66.8, 90)   # MOT_TEMP -> CN10_15
P('R58', 176.0, 66.8, 90); P('R25', 177.8, 66.8, 90)          # nFAULT / SDO pull-ups next to CN10_25 / _27
for i, ref in enumerate(('R55', 'C44', 'R56', 'C46', 'R57', 'C45')):   # current-sense RC right above PA0/PA1/PA4
    P(ref, 183.0 + 1.7 * i, 117.7, 90)
P('TP2', 175.0, 92.0, 0)                   # scope GND loop, logic side (middle of the Nucleo)

# ---------------- top strip (above CN10): signals that land on CN10 --------------------------------------------------
YJ = 47.4                                  # JST pin row
P('J11', 153.5, YJ, 0)                     # wheel sensor -> CN10_4 (PC6)
P('D10', 153.0, 54.0, 0); P('R78', 156.8, 54.0, 0); P('C73', 156.8, 56.2, 0)
P('J10', 165.0, YJ, 0)                     # CAN -> U5 -> CAN_RX CN10_16 / CAN_TX CN10_17 right below
P('U5', 167.5, 55.6, 180)                  # CANH/L pins left (toward J10), TXD/RXD right
P('C66', 162.3, 55.6, 90); P('C65', 172.8, 55.6, 90)
P('D9', 176.0, 47.9, 0); P('JP1', 176.0, 52.3, 0); P('R72', 176.0, 55.4, 0)
P('J8', 181.5, YJ, 0)                      # halls + motor temp -> HALLA/B CN10_22/24, HALLC CN10_34
for i in range(3):
    P(('D5', 'D6', 'D7')[i], 186.5 + 2.5 * i, 53.6, 270)
    P(('R59', 'R60', 'R61')[i], 185.0 + 3.5 * i, 57.4, 90); P(('C47', 'C48', 'C49')[i], 186.75 + 3.5 * i, 57.4, 90)

# ---------------- bottom strip (below CN7): signals that land on CN7 -------------------------------------------------
YB = 129.3
P('R70', 151.3, YB, 90); P('R71', 153.0, YB, 90)        # UART series resistors right at CN7_1 / CN7_2
P('J9', 157.0, YB, 0)                      # UART
P('D3', 169.3, 129.0, 0); P('R41', 172.6, 129.0, 0)     # power LED (+3V3)
P('D4', 176.3, 129.0, 0); P('R40', 179.6, 129.0, 0)     # LP LED (CN7_21)
P('TP7', 183.0, 129.0, 0)                  # +3V3
P('R62', 185.2, YB, 90); P('C50', 186.9, YB, 90)        # throttle filter
P('J7', 191.0, YB, 0)                      # throttle -> CN7_36 (PC1)
P('R17', 200.0, 129.0, 90); P('C17', 201.7, 129.0, 90)  # VSENVM divider -> CN7_37

missing = sorted(set(fp) - placed - {'U2'})
print('placed %d, not placed: %s' % (len(placed), missing))
for ph in 'CBA': print('cell %s: LS at (%.2f, %.2f)' % (ph, LS[ph][0], LS[ph][1]))

# ---------------- M3 holes in the 4 board corners (board-only, no schematic symbol) -----------------------------------------
for ref, (x, y) in (('H1', (147.0, 48.0)), ('H2', (249.5, 48.0)), ('H3', (147.0, 130.0)), ('H4', (249.5, 130.0))):
    h = pcbnew.FootprintLoad(r"C:/Program Files/KiCad/10.0/share/kicad/footprints/MountingHole.pretty", 'MountingHole_3.2mm_M3')
    h.SetReference(ref); h.SetValue('M3'); h.SetBoardOnly(True); h.SetExcludedFromBOM(True); h.SetExcludedFromPosFiles(True)
    h.SetPosition(pcbnew.VECTOR2I(MM(x), MM(y))); b.Add(h)
b.Save(sys.argv[2])
