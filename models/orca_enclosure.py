"""
ORCA reader costume enclosure (CadQuery).

Two main printed pieces:
  FRONT  (black)  - reader face + side walls, holds the board, NFC, speaker
  BACK   (yellow) - back wall + the station-style hood, one piece

Reader body follows INIT's PROXmobil3 validator (210 H x 140 W x 42 D mm),
widened to 146 mm so the CrowPanel 5.0" board (137.2 mm PCB) fits.
Power: use a RIGHT-ANGLE USB-C cable; it turns down inside the hood and
exits through the hood's open bottom (22 mm side gap, no straight plug fits).

Coordinates: X = width (centered), Y = height (centered, +Y up),
Z = depth (front face at Z=0, back face at Z=42). Looking at the front
face from outside, +X is on your LEFT.
Every part is modeled in its print orientation: front face-down,
back piece back-face-down. No supports needed.

Hardware (M3 heat-set inserts are OD 4.6 into d4.0 holes; M2.5 are OD ~4.0 into d3.6):
  back -> front    4 x M3 x 5.7 insert (corner bosses)    + 4 x M3x8 ISO 10642 countersunk
  board -> front   4 x M3 x 3.0 short insert (standoffs)  + 4 x M3x5 ISO 7380 button head
  speaker -> front 4 x M3 x 3.0 short insert (pad)        + 4 x M3x6 ISO 7380 button head
  PN532 -> front   2 x M2.5 insert, <= 4.0 long (standoffs) + 2 x M2.5x5 ISO 7380 button head
  speaker: 4 ohm 3 W 70 x 31 mm enclosed box speaker, JST-PH 2.0 lead

Run:  python3 orca_enclosure.py   -> writes STL/STEP files next to this script.
"""
import math
import os
import cadquery as cq

# ---------------- overall ----------------
W, H, D = 146.0, 210.0, 42.0      # outer width, height, depth
WALL = 2.4                        # side and front wall
CORNER_R = 10.0                   # plan-view corner radius
FRONT_CHAMFER = 2.0               # front edge chamfer (prints face-down cleanly)
PLATE_T = 3.0                     # back plate thickness
SHELL_D = D - PLATE_T             # front shell depth

# ---------------- CrowPanel 5.0" (from Elecrow outline drawing + STEP) ----------------
PCB_W, PCB_H = 137.2, 79.9
HOLE_DX, HOLE_DY = 130.2, 72.9    # M3 hole pattern (4 x d3.2)
LCD_TO_PCB = 3.5                  # LCD front face to PCB front face
BOARD_Y = 53.05                   # board center Y (PCB top edge 9.6 mm below inner top wall)
LCD_DY = -0.5                     # LCD/active area center relative to PCB center
WIN_W, WIN_H = 102.0, 65.8        # visible window (real reader shows 754 of 800 px)
WIN_CHAMFER = 1.2
USBC_Y = BOARD_Y - 0.4            # USB-C on the board's left edge (viewed from front) = +X side
USBC_Z = WALL + 6.2               # center depth of the receptacle
USBC_W, USBC_H = 13.5, 7.5        # opening for the plug overmold

# ---------------- NFC tap target ----------------
TAP_Y = -23.5
# Elechouse PN532 V3: 2 mounting holes on one diagonal, estimated from a photo (~21.6 px/mm):
# centers ~13.8 / 12.4 mm from the board center, drill ~2.5-3.0 mm -> M2.5 screws.
# Mounted flat (back) side toward the front wall, component side facing in, upright as in
# Elechouse's photo (I2C header on the left seen from inside). Seen from inside (+X right),
# the holes are top-left and bottom-right; rotating the board 180 deg uses the same holes.
PN532_HOLE_DX, PN532_HOLE_DY = 27.6, 24.8
PN532_STANDOFF_H = 4.0                 # back of PCB above the front wall (clears header solder tails)
PN532_STANDOFF_D = 7.0                 # (7.0 - 3.6) / 2 = 1.7 mm wall >= 1.6
PN532_INSERT_HOLE_D, PN532_INSERT_HOLE_DEPTH = 3.6, 4.6   # M2.5 x <=4 mm insert, 1.8 mm face skin

# ---------------- speaker ----------------
# 4 ohm 3 W enclosed box speaker ("7030"), JST-PH 2.0 lead -> CrowPanel PH2.0 speaker header.
# Flange 69.8 x 30.8 x 3.2, body 51.2 long, 16.6 deep overall, 4 x d3.3 holes on 63.8 x 24.3,
# cone opening 38.4 x 27 oval. Long axis horizontal, cone face down on a solid pad inside
# the front wall; screws go through the flange into heat-set inserts in the pad.
SPK_Y = -80.6                    # body top edge ~20 mm below the PN532 (keep the magnet off the antenna)
SPK_HOLE_DX, SPK_HOLE_DY = 63.8, 24.3
SPK_CONE_W, SPK_CONE_H = 38.4, 27.0
SPK_PAD_W, SPK_PAD_H, SPK_PAD_R = 74.0, 35.0, 3.0   # >= 1.6 mm wall around each insert hole
SPK_PAD_T = LCD_TO_PCB          # same height as the board standoffs -> same short inserts
GRILLE_RX, GRILLE_RY = 17.0, 11.5   # hole centers stay inside this ellipse (inside the cone)
GRILLE_HOLE, GRILLE_PITCH = 2.0, 3.4

# ---------------- back plate fasteners ----------------
# corner bosses (x, y, diameter). Top pair stays clear of the board's top edge;
# bottom pair sits further in so the back's countersinks keep >3 mm of plastic.
BOSSES = [(-66.2, 100.6, 9.0), (66.2, 100.6, 9.0), (-64.0, -98.0, 10.0), (64.0, -98.0, 10.0)]
BOSS_PTS = [(x, y) for x, y, _ in BOSSES]
# ---------------- heat-set inserts ----------------
# Both inserts: OD 4.6, recommended hole 4.0 mm, min wall 1.6 mm (ruthex / CNC Kitchen).
INSERT_HOLE_D = 4.0
INSERT_LEADIN = 0.3                    # 45 deg lead-in chamfer at the hole mouth
# back-to-front: M3 x 5.7 standard insert in the 4 corner bosses, M3x8 ISO 10642 screws
BACK_INSERT_LEN, BACK_INSERT_HOLE_DEPTH = 5.7, 7.5
# board-to-front: M3 x 3.0 short insert in the 4 board standoffs, M3x5 ISO 7380 screws
# speaker-to-front: the same M3 x 3.0 short insert in the speaker pad, M3x6 ISO 7380 screws
# (6 - 3.2 flange = 2.8 mm into the insert, tip stays above the 4.4 mm hole bottom)
BOARD_INSERT_LEN, BOARD_INSERT_HOLE_DEPTH = 3.0, 4.4
STANDOFF_D = 8.0                       # (8.0 - 4.0) / 2 = 2.0 mm wall >= 1.6
SCREW_D, CSK_D = 3.4, 6.5
STRAP_W, STRAP_L, STRAP_X, STRAP_Y = 4.0, 28.0, 45.0, 60.0

# ---------------- yellow hood (proportions estimated from a station photo) ----------------
HOOD_W = 196.0          # outer width (~1.35x the reader)
HOOD_TOP = 135.0        # top outer edge, Y (30 mm above the reader)
HOOD_BOTTOM = -5.0      # sides stop just below the reader's middle
HOOD_WALL = 3.0
HOOD_TOP_R = 30.0       # outer radius of the two top corners
HOOD_REACH = 30.0       # how far the rim sits in front of the reader's face
HOOD_RIM_FILLET = 1.5

OUT = os.path.dirname(os.path.abspath(__file__))


def rounded_box(w, h, d, r, z0=0.0):
    return (cq.Workplane("XY").workplane(offset=z0)
            .rect(w, h).extrude(d).edges("|Z").fillet(r))


def arc_segment(r_in, r_out, a0, a1, cx=0.0, cy=0.0, n=48):
    """2D annular sector polygon (degrees), as a point list."""
    pts = []
    for i in range(n + 1):
        a = math.radians(a0 + (a1 - a0) * i / n)
        pts.append((cx + r_out * math.cos(a), cy + r_out * math.sin(a)))
    for i in range(n, -1, -1):
        a = math.radians(a0 + (a1 - a0) * i / n)
        pts.append((cx + r_in * math.cos(a), cy + r_in * math.sin(a)))
    return pts


# =====================================================================
# FRONT SHELL
# =====================================================================
def front_shell():
    outer = rounded_box(W, H, SHELL_D, CORNER_R)
    outer = outer.faces("<Z").edges().chamfer(FRONT_CHAMFER)
    inner = rounded_box(W - 2 * WALL, H - 2 * WALL, SHELL_D, CORNER_R - WALL, z0=WALL)
    shell = outer.cut(inner)

    # --- internal features (unioned) ---
    z_in = WALL
    feats = cq.Workplane("XY")

    # board standoffs
    sx, sy = HOLE_DX / 2, HOLE_DY / 2
    standoff_pts = [(x, BOARD_Y + y) for x in (-sx, sx) for y in (-sy, sy)]
    standoffs = (cq.Workplane("XY").workplane(offset=z_in)
                 .pushPoints(standoff_pts).circle(STANDOFF_D / 2).extrude(LCD_TO_PCB))
    shell = shell.union(standoffs)

    # PN532 standoffs
    nfc_pts = [(-PN532_HOLE_DX / 2, TAP_Y + PN532_HOLE_DY / 2),
               (PN532_HOLE_DX / 2, TAP_Y - PN532_HOLE_DY / 2)]
    shell = shell.union(cq.Workplane("XY").workplane(offset=z_in).pushPoints(nfc_pts)
                        .circle(PN532_STANDOFF_D / 2).extrude(PN532_STANDOFF_H))

    # speaker mounting pad (flange seats on it, sealing the cone to the grille)
    spk_pad = (cq.Workplane("XY").workplane(offset=z_in).center(0, SPK_Y)
               .rect(SPK_PAD_W, SPK_PAD_H).extrude(SPK_PAD_T).edges("|Z").fillet(SPK_PAD_R))
    shell = shell.union(spk_pad)

    # corner bosses for the back plate (clipped to the outer body)
    bosses = None
    for x, y, d in BOSSES:
        b = (cq.Workplane("XY").workplane(offset=z_in).center(x, y)
             .circle(d / 2).extrude(SHELL_D - z_in))
        bosses = b if bosses is None else bosses.union(b)
    bosses = bosses.intersect(rounded_box(W, H, SHELL_D, CORNER_R))
    shell = shell.union(bosses)

    # --- cuts ---
    win_y = BOARD_Y + LCD_DY
    window = (cq.Workplane("XY").center(0, win_y).rect(WIN_W, WIN_H).extrude(WALL + 1))
    win_chamfer = (cq.Workplane("XY").center(0, win_y)
                   .rect(WIN_W + 2 * WIN_CHAMFER, WIN_H + 2 * WIN_CHAMFER)
                   .workplane(offset=WIN_CHAMFER).rect(WIN_W, WIN_H).loft())
    shell = shell.cut(window).cut(win_chamfer)

    def insert_holes(pts, top_z, depth, d=INSERT_HOLE_D):
        holes = (cq.Workplane("XY").workplane(offset=top_z - depth)
                 .pushPoints(pts).circle(d / 2).extrude(depth + 0.5))
        for x, y in pts:
            cone = cq.Solid.makeCone(d / 2, d / 2 + INSERT_LEADIN + 0.05,
                                     INSERT_LEADIN + 0.05, pnt=cq.Vector(x, y, top_z - INSERT_LEADIN))
            holes = holes.union(cq.Workplane().add(cone))
        return holes

    # board standoffs: short M3 inserts (hole bottom leaves a 1.5 mm skin on the face)
    shell = shell.cut(insert_holes(standoff_pts, z_in + LCD_TO_PCB, BOARD_INSERT_HOLE_DEPTH))
    # corner bosses: standard M3 inserts for the back piece
    shell = shell.cut(insert_holes(BOSS_PTS, SHELL_D, BACK_INSERT_HOLE_DEPTH))
    # PN532 standoffs: M2.5 inserts
    shell = shell.cut(insert_holes(nfc_pts, z_in + PN532_STANDOFF_H, PN532_INSERT_HOLE_DEPTH,
                                   PN532_INSERT_HOLE_D))
    # speaker pad: short M3 inserts, sound opening through the pad down to the front wall
    spk_pts = [(x, SPK_Y + y) for x in (-SPK_HOLE_DX / 2, SPK_HOLE_DX / 2)
               for y in (-SPK_HOLE_DY / 2, SPK_HOLE_DY / 2)]
    shell = shell.cut(insert_holes(spk_pts, z_in + SPK_PAD_T, BOARD_INSERT_HOLE_DEPTH))
    shell = shell.cut(cq.Workplane("XY").workplane(offset=z_in).center(0, SPK_Y)
                      .slot2D(SPK_CONE_W + 0.6, SPK_CONE_H + 0.6).extrude(SPK_PAD_T + 0.5))

    # engraved contactless icon (generic arcs opening to the viewer's right = -X), 0.6 mm deep
    icx, icy = 6.0, TAP_Y
    for r in (4.0, 8.0, 12.0, 16.0):
        engr = (cq.Workplane("XY").workplane(offset=-0.5)
                .polyline(arc_segment(r - 0.7, r + 0.7, 138, 222, icx, icy, n=32)).close()
                .extrude(1.1))
        shell = shell.cut(engr)

    # speaker grille (hex grid of holes in an oval matching the cone)
    pts = []
    row_pitch = GRILLE_PITCH * math.sqrt(3) / 2
    nrows, ncols = int(GRILLE_RY / row_pitch), int(GRILLE_RX / GRILLE_PITCH) + 1
    for row in range(-nrows, nrows + 1):          # centered on the cone, symmetric both ways
        y = row * row_pitch
        off = (GRILLE_PITCH / 2) if row % 2 else 0.0
        for col in range(-ncols, ncols + 1):
            x = col * GRILLE_PITCH + off
            if (x / GRILLE_RX) ** 2 + (y / GRILLE_RY) ** 2 <= 1.0:
                pts.append((x, SPK_Y + y))
    grille = (cq.Workplane("XY").workplane(offset=-0.5)
              .pushPoints(pts).circle(GRILLE_HOLE / 2).extrude(WALL + 1))
    shell = shell.cut(grille)

    # USB-C opening in the left wall as seen from the front (+X side)
    usbc = (cq.Workplane("YZ").workplane(offset=W / 2 - WALL - 2)
            .center(USBC_Y, USBC_Z).slot2D(USBC_W, USBC_H).extrude(WALL + 4))
    shell = shell.cut(usbc)

    return shell, len(pts)


# =====================================================================
# BACK PIECE = back wall + hood, one yellow print
# Modeled back-face-down at Z=0; walls rise +Z. Assembled: z_asm = D - z.
# Fastens to the front with 4 x M3x8 countersunk screws into the heat-set
# inserts in the front's corner bosses.
# =====================================================================
def back_piece():
    wall_h = PLATE_T + SHELL_D + HOOD_REACH          # back face -> hood rim

    def u_profile(w, top, bottom, r, h, z0=0.0):
        body = (cq.Workplane("XY").workplane(offset=z0)
                .center(0, (top + bottom) / 2).rect(w, top - bottom).extrude(h))
        return body.edges("|Z").edges(">Y").fillet(r)   # round the two top corners

    hood = u_profile(HOOD_W, HOOD_TOP, HOOD_BOTTOM, HOOD_TOP_R, wall_h)
    hood = hood.cut(u_profile(HOOD_W - 2 * HOOD_WALL, HOOD_TOP - HOOD_WALL,
                              HOOD_BOTTOM - 5.0, HOOD_TOP_R - HOOD_WALL,
                              wall_h, z0=PLATE_T))
    try:
        hood = hood.faces(">Z").edges().fillet(HOOD_RIM_FILLET)   # soft front rim
    except Exception:
        pass

    # back wall below the hood (the reader's own back)
    body = hood.union(rounded_box(W, H, PLATE_T, CORNER_R))

    # alignment lip that drops inside the front's walls
    lip_out_w, lip_out_h = W - 2 * WALL - 0.6, H - 2 * WALL - 0.6
    lip_r = CORNER_R - WALL - 0.3
    lip = rounded_box(lip_out_w, lip_out_h, 3.0, lip_r, z0=PLATE_T)
    lip = lip.cut(rounded_box(lip_out_w - 3.2, lip_out_h - 3.2, 3.0, lip_r - 1.6, z0=PLATE_T))
    # notch the lip's four corners out completely around the bosses (no thin slivers)
    for x, y, d in BOSSES:
        r = d / 2 + 0.8
        cx = x - math.copysign(r, x) + math.copysign(20, x)
        cy = y - math.copysign(r, y) + math.copysign(20, y)
        lip = lip.cut(cq.Workplane("XY").workplane(offset=PLATE_T - 0.1)
                      .center(cx, cy).rect(40, 40).extrude(3.2))
    body = body.union(lip)

    # countersunk M3 holes, countersink on the bed face (prints as a 45 deg cone)
    for x, y in BOSS_PTS:
        hole = (cq.Workplane("XY").workplane(offset=-0.5).center(x, y)
                .circle(SCREW_D / 2).extrude(PLATE_T + 1))
        csk = (cq.Workplane("XY").center(x, y).circle(CSK_D / 2)
               .workplane(offset=(CSK_D - SCREW_D) / 2).circle(SCREW_D / 2).loft())
        body = body.cut(hole).cut(csk)

    # strap slots for 25 mm webbing
    for x in (-STRAP_X, STRAP_X):
        slot = (cq.Workplane("XY").workplane(offset=-0.5).center(x, STRAP_Y)
                .slot2D(STRAP_L, STRAP_W, angle=90).extrude(PLATE_T + 1))
        body = body.cut(slot)
    return body


def placed_back(bk):
    """Back piece moved from print orientation into assembled position."""
    return bk.mirror("XY").translate((0, 0, D))


def speaker_dummy():
    """Bought speaker, for fit checks in the assembly only (flange 69.8 x 30.8 x 3.2, 16.6 deep)."""
    z0 = WALL + SPK_PAD_T
    flange = (cq.Workplane("XY").workplane(offset=z0).center(0, SPK_Y)
              .rect(69.8, 30.8).extrude(3.2))
    body = (cq.Workplane("XY").workplane(offset=z0 + 3.2).center(0, SPK_Y)
            .rect(51.2, 30.8).extrude(16.6 - 3.2))
    holes = (cq.Workplane("XY").workplane(offset=z0 - 0.1).center(0, SPK_Y)
             .rect(SPK_HOLE_DX, SPK_HOLE_DY, forConstruction=True).vertices()
             .circle(3.3 / 2).extrude(3.4))
    return flange.union(body).cut(holes)


def pn532_dummy():
    """Elechouse PN532 V3 (42.7 x 40.4 x 1.6 PCB) on its standoffs, for fit checks only."""
    pcb = (cq.Workplane("XY").workplane(offset=WALL + PN532_STANDOFF_H).center(0, TAP_Y)
           .rect(42.7, 40.4).extrude(1.6).edges("|Z").fillet(2.0))
    holes = (cq.Workplane("XY").workplane(offset=WALL + PN532_STANDOFF_H - 0.1)
             .pushPoints([(-PN532_HOLE_DX / 2, TAP_Y + PN532_HOLE_DY / 2),
                          (PN532_HOLE_DX / 2, TAP_Y - PN532_HOLE_DY / 2)])
             .circle(2.8 / 2).extrude(1.8))
    return pcb.cut(holes)


if __name__ == "__main__":
    front, nholes = front_shell()
    back = back_piece()

    for obj, name in ((front, "orca_front.stl"), (back, "orca_back.stl")):
        cq.exporters.export(obj, os.path.join(OUT, name), tolerance=0.02, angularTolerance=0.15)

    asm = (cq.Assembly()
           .add(front, name="front", color=cq.Color(0.15, 0.15, 0.17))
           .add(placed_back(back), name="back_hood", color=cq.Color(0.98, 0.7, 0.05))
           .add(speaker_dummy(), name="speaker_ref", color=cq.Color(0.4, 0.4, 0.45))
           .add(pn532_dummy(), name="pn532_ref", color=cq.Color(0.8, 0.1, 0.1)))
    asm.save(os.path.join(OUT, "orca_enclosure_assembly.step"))
    print("grille holes:", nholes)
    for name, obj in (("front", front), ("back", back)):
        bb = obj.val().BoundingBox()
        print(name, "bbox", round(bb.xlen, 2), round(bb.ylen, 2), round(bb.zlen, 2),
              "valid", obj.val().isValid(), "solids", len(obj.solids().vals()))
