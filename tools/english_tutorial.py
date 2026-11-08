"""The team screen's tutorial menus (data/adv/bin/mm00N.bin and mmpNNN.bin, on both discs) in English.

The menus' background is one 128 x 128 tile, so what is under a line of text comes back exactly from
the same place in the other tiles. The buttons' labels are covered from the face either side, row by
row. Both are written as diffs (make_english_pack.Sheet): the pack holds only the English.

Also the tutorial's full-screen pages (han*, kojin*, battle*: PAGES, their Japanese filled in from the art
round it) and the story's chapter cards (tc*: CHAPTERS, erased and drawn anew)."""
import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy import ndimage

import english_opening
import make_english_pack as pack

BIN = pack.REPO / "gamedata/common/data/adv/bin"
REL = "data/adv/bin"
BUTTON_FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
NOTE_FONT = "/usr/share/fonts/truetype/liberation/LiberationSerif-Bold.ttf"
TILE = 128

MAIN = ["About the Team Screen", "About the Roster Screen", "Training & Quest Notes", "Exit"]
TEAM = ["Getting Started", "Forming Teams", "Choosing Lectures", "Choosing Quests", "Back"]
ROSTER = ["Status Area", "Equipping Skills", "Equipping Items", "Changing Class", "Raising Stats", "Student List", "Back"]

# Each sheet: its buttons (the label's box, the English), its notes on the pink (box, lines), its title.
SHEETS = {
    "mm001": dict(buttons=[((300, 196, 650, 236), MAIN[0]), ((380, 360, 728, 400), MAIN[1]),
                           ((410, 524, 856, 568), MAIN[2]), ((640, 688, 788, 728), MAIN[3])],
                  notes=[((226, 254, 724, 324), ["An explanation of the Team screen.", "  How Teams are formed, and what's what."]),
                         ((312, 418, 840, 488), ["An explanation of the Roster screen.", "  How to equip, and what's what."]),
                         ((393, 582, 764, 652), ["Extra notes on the Quest screen.", "  What everything is called."])]),
    "mm002": dict(buttons=[((396, 282, 624, 322), TEAM[0]), ((318, 382, 706, 422), TEAM[1]),
                           ((338, 482, 686, 523), TEAM[2]), ((340, 582, 683, 622), TEAM[3]),
                           ((440, 683, 584, 722), TEAM[4])],
                  title="~ About the Team Screen ~"),
    "mm003": dict(buttons=[((84, 293, 428, 331), ROSTER[0]), ((576, 324, 960, 364), ROSTER[1]),
                           ((43, 412, 470, 452), ROSTER[2]), ((595, 444, 942, 484), ROSTER[3]),
                           ((62, 532, 450, 572), ROSTER[4]), ((595, 564, 942, 605), ROSTER[5]),
                           ((440, 683, 584, 722), ROSTER[6])],
                  title="~ About the Roster Screen ~"),
    # The lit buttons, one per 64 rows.
    "mmp001": dict(buttons=[((28, 64 * i + 6, 470, 64 * i + 54), t) for i, t in enumerate(MAIN)]),
    "mmp002": dict(buttons=[((28, 64 * i + 6, 470, 64 * i + 54), t) for i, t in enumerate(TEAM)]),
    "mmp003": dict(buttons=[((28, 64 * i + 6, 470, 64 * i + 54), t) for i, t in enumerate(ROSTER)]),
}
TITLE_BOX = (150, 150, 880, 240)     # mm002 and mm003's green ~ … ~ title
TITLE_SIZE = 46

TAB_FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSansCondensed-BoldOblique.ttf"
NAV_FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
ITALIC_FONT = "/usr/share/fonts/truetype/liberation/LiberationSans-BoldItalic.ttf"
# The callouts on the screenshots: white tabs, black italic text (the text's box, the English).
TABS = {
    "h-part001": [((716, 6, 795, 30), "Students"), ((412, 7, 491, 31), "Teacher"),
                  ((493, 419, 618, 443), "Teacher Pool"), ((764, 419, 889, 443), "Student Pool")],
    "h-part002": [((7, 208, 156, 232), "Lecture Select")],
    "h-part003": [((7, 8, 155, 32), "Quest Select")],
    "k-part001": [((680, 6, 788, 23), "Skill Pool"), ((5, 7, 69, 25), "Notes"), ((801, 7, 913, 25), "Equip Pocket")],
    "k-part002": [((6, 8, 133, 25), "Item Pool"), ((316, 138, 428, 156), "Equip Pocket"), ((7, 214, 71, 232), "Notes")],
    "k-part003": [((257, 6, 321, 24), "Notes")],
    "k-part004": [((7, 257, 71, 275), "Notes")],
}
SAVE_LABELS = {"h-part002": (60, 602, 206, 628), "h-part003": (60, 602, 206, 628)}   # green "SAVE ボタン"
# b-part001: the screenshots' ← back / next → / ↓ menu, each glowing on black.
NAV = [((2, 4, 70, 30), "\u2190 Back"), ((76, 16, 142, 42), "Next \u2192"), ((2, 56, 124, 88), "\u2193 MENU")]
NAV_BLACK = [(0, 0, 144, 48), (0, 48, 128, 96)]
# mm004: the notes page's title, its line, the box's two lines, and its own links.
MM004_TITLE = ((100, 112, 940, 205), "Notes on Training & Quests")
MM004_LINE = ((190, 238, 870, 286), "Here are some extra notes on the Training and Quest screens.")
MM004_BOX = ((192, 422, 828, 500), ["Left-click \u2018\u2190 Back\u2019 and \u2018Next \u2192\u2019 below to turn the pages.",
                                    "Left-click \u2018\u2193 MENU\u2019 to go back to the menu."])
MM004_LINKS = [((56, 700, 128, 732), "\u2190 Back"), ((396, 708, 522, 742), "\u2193 MENU"), ((870, 712, 942, 746), "Next \u2192")]
LINK_BLUE = (138, 146, 255)


def tile_of(imgs):
    """The background tile, from every tile of the menus."""
    tiles = []
    for img in imgs:
        h, w = img.shape[:2]
        stack = img[:h // TILE * TILE, :w // TILE * TILE, :3].reshape(h // TILE, TILE, w // TILE, TILE, 3)
        tiles.append(stack.transpose(0, 2, 1, 3, 4).reshape(-1, TILE, TILE, 3))
    # The clean tiles are exact copies, so the commonest value at each texel is the tile's.
    keys = np.concatenate(tiles).astype(np.int64)
    keys = (keys[..., 0] << 16 | keys[..., 1] << 8 | keys[..., 2]).reshape(len(keys), -1)
    keys.sort(axis=0)
    n = len(keys)
    best = np.zeros(keys.shape[1], np.int64)
    best_run = np.zeros(keys.shape[1], np.int64)
    run = np.ones(keys.shape[1], np.int64)
    for i in range(1, n + 1):
        ends = (keys[i] != keys[i - 1]) if i < n else np.ones(keys.shape[1], bool)
        better = ends & (run > best_run)
        best[better], best_run[better] = keys[i - 1][better], run[better]
        run = np.where(ends, 1, run + 1)
    tile = np.stack([best >> 16 & 255, best >> 8 & 255, best & 255], axis=-1).reshape(TILE, TILE, 3)
    return tile.astype(float)


def tiled(tile, box):
    x0, y0, x1, y1 = box
    ys, xs = np.mgrid[y0:y1, x0:x1]
    return tile[ys % TILE, xs % TILE]


def clear_pink(sheet, tile, box, threshold=14):
    """What differs from the tile in box (the text, its edge and glow) goes back to the tile."""
    x0, y0, x1, y1 = box
    here = sheet.frame[y0:y1, x0:x1, :3]
    plain = tiled(tile, box)
    drawn = ndimage.binary_dilation(np.abs(here - plain).max(axis=2) > threshold, iterations=2)
    region = (slice(y0, y1), slice(x0, x1))
    base = sheet.base[region]
    base[drawn, :3] = plain[drawn]
    sheet.fixed[region] |= drawn


def clear_face(sheet, box):
    """A button's label: covered row by row from the face either side of it."""
    frame = sheet.frame
    x0, y0, x1, y1 = box
    face = frame[y0:y1, x0:x1, :3]
    row = np.median(face, axis=1, keepdims=True)
    label = ndimage.binary_dilation(np.abs(face - row).max(axis=2) > 40, iterations=2)
    ys, xs = np.where(label)
    tx0, tx1, ty0, ty1 = x0 + xs.min() - 1, x0 + xs.max() + 2, y0 + ys.min(), y0 + ys.max() + 1
    t = np.linspace(0, 1, tx1 - tx0)[:, None]
    for y in range(ty0, ty1):
        left, right = frame[y, tx0 - 4:tx0, :3].mean(axis=0), frame[y, tx1:tx1 + 4, :3].mean(axis=0)
        sheet.rebuild((y, slice(tx0, tx1)), left * (1 - t) + right * t)
    return (tx0 + tx1) / 2, (ty0 + ty1) / 2


def inpaint(sheet, mask):
    """Texels under mask filled in from those around them (the art there is a dim, busy collage)."""
    rgb = sheet.frame[..., :3]
    known = (~mask).astype(float)
    out, todo = rgb.copy(), mask.copy()
    for sigma in (2, 4, 8, 16, 32):     # the nearest texels first, wider only where they are all masked
        num = np.stack([ndimage.gaussian_filter(rgb[..., c] * known, sigma) for c in range(3)], axis=-1)
        den = ndimage.gaussian_filter(known, sigma)
        fill = todo & (den > 0.05)
        out[fill] = num[fill] / den[fill][:, None]
        todo &= ~fill
    sheet.rebuild((mask,), out[mask])


def tab(sheet, box, text):
    """A white callout tab: drawn anew (white, grey edge), the English within its width: the game cuts each
    tab from the sheet by its own rectangle, so a wider one would be split."""
    frame = sheet.frame
    x0, y0, x1, y1 = box
    near = frame[max(y0 - 8, 0):y1 + 8, max(x0 - 12, 0):x1 + 12]
    white = (near[..., 3] > 0) & (near[..., :3].min(axis=2) > 200)
    rows, cols = np.where(white.sum(axis=1) > white.shape[1] * 0.5)[0], np.where(white.sum(axis=0) > (y1 - y0) * 0.5)[0]
    tx0, tx1 = max(x0 - 12, 0) + cols.min(), max(x0 - 12, 0) + cols.max() + 1
    ty0, ty1 = max(y0 - 8, 0) + rows.min(), max(y0 - 8, 0) + rows.max() + 1
    room = tx1 - tx0 - 6
    size, least = round((y1 - y0) * 0.8), round((y1 - y0) * 0.68)
    while True:
        img = english_opening.render(text, size, (8, 8, 8), (8, 8, 8), 0, font_path=TAB_FONT)
        if img.shape[1] <= room or size <= least:
            break
        size -= 1
    if img.shape[1] > room:     # squeezed the rest of the way
        img = np.array(Image.fromarray(img).resize((room, img.shape[0]), Image.LANCZOS))
    over = sheet.over
    pack.put(over, np.ones((ty1 - ty0, tx1 - tx0)), tx0, ty0, (255, 255, 255))
    place(sheet, img, (tx0 + tx1) / 2, (ty0 + ty1) / 2)


def glow_text(sheet, text, size, cx, cy, fill, edge, glow, font=ITALIC_FONT, stroke=1, blur=3, strength=0.8):
    img = english_opening.render(text, size, fill, edge, stroke, font_path=font)
    a = np.pad(img[..., 3].astype(float), 3 * blur)
    halo = np.clip(ndimage.gaussian_filter(a, blur) / 255 * strength * 2, 0, 1)
    x, y = round(cx - a.shape[1] / 2), round(cy - a.shape[0] / 2)
    pack.put(sheet.over, halo[max(-y, 0):, max(-x, 0):], max(x, 0), max(y, 0), glow)     # clipped at the left and top
    place(sheet, img, cx, cy)
    return img


def underlined(sheet, box, text, colour, size):
    """mm004's links: blue italic, underlined."""
    x0, y0, x1, y1 = box
    img = english_opening.render(text, size, colour, colour, 0, font_path=ITALIC_FONT)
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2 - 2
    place(sheet, img, cx, cy)
    left = round(cx - img.shape[1] / 2)
    pack.put(sheet.over, np.ones((1, img.shape[1])), left, round(cy + img.shape[0] / 2 + 2), colour)


def parts():
    """The callouts laid over the tutorial's screenshots, and their nav."""
    for name, tabs in TABS.items():
        sheet = pack.Sheet(english_opening.decode(pack.frames_of(BIN / f"{name}.bin")[0]).astype(float))
        for box, text in tabs:
            tab(sheet, box, text)
        erase = None
        if name in SAVE_LABELS:
            x0, y0, x1, y1 = SAVE_LABELS[name]
            erase = np.zeros(sheet.frame.shape[:2], bool)
            erase[y0:y1, x0:x1] = sheet.frame[y0:y1, x0:x1, 3] > 0
            img = english_opening.render("SAVE Button", 20, (8, 8, 8), (90, 255, 70), 2, font_path=TAB_FONT)
            place(sheet, img, x0 + img.shape[1] / 2 + 4, (y0 + y1) / 2)
        sheet.save(f"{REL}/{name}.bin", 0)
        if erase is not None:
            mask = np.zeros((*erase.shape, 4), np.uint8)
            mask[erase] = 255
            Image.fromarray(mask, "RGBA").save(pack.PACK / "images" / REL / f"{name}.bin" / "0.erase.png", optimize=True)
    # The nav: black under it, then the English glowing as the Japanese does.
    sheet = pack.Sheet(english_opening.decode(pack.frames_of(BIN / "b-part001.bin")[0]).astype(float))
    for x0, y0, x1, y1 in NAV_BLACK:
        sheet.rebuild((slice(y0, y1), slice(x0, x1)), np.median(sheet.frame[y0:y1, x0:x1, :3].reshape(-1, 3), axis=0))
    for (x0, y0, x1, y1), text in NAV:     # thin and underlined, as the Japanese
        img = glow_text(sheet, text, 17, (x0 + x1) / 2, (y0 + y1) / 2 - 2, (226, 230, 248), (226, 230, 248), (200, 190, 120),
                        font=NAV_FONT, stroke=0, blur=2, strength=0.6)
        line = np.ones((1, img.shape[1]))
        under = round((y0 + y1) / 2 - 2 + img.shape[0] / 2 + 2)
        pack.put(sheet.over, np.clip(ndimage.gaussian_filter(np.pad(line, 3), 1.5) * 1.2, 0, 1),
                 round((x0 + x1 - img.shape[1]) / 2) - 3, under - 3, (200, 190, 120))
        pack.put(sheet.over, line, round((x0 + x1 - img.shape[1]) / 2), under, (226, 230, 248))
    sheet.save(f"{REL}/b-part001.bin", 0)


def notes_page():
    """mm004: the extra notes' page, its Japanese filled in from the collage around it."""
    sheet = pack.Sheet(english_opening.decode(pack.frames_of(BIN / "mm004.bin")[0]).astype(float))
    rgb = sheet.frame[..., :3]
    bright = rgb.max(axis=2) > 70
    for (x0, y0, x1, y1), _ in (MM004_TITLE, MM004_LINE, *MM004_LINKS):
        mask = np.zeros(bright.shape, bool)
        mask[y0:y1, x0:x1] = bright[y0:y1, x0:x1]
        inpaint(sheet, ndimage.binary_dilation(mask, iterations=3))
    (bx0, by0, bx1, by1), lines = MM004_BOX
    sheet.rebuild((slice(by0, by1), slice(bx0, bx1)), np.median(rgb[by0:by1, bx0:bx1].reshape(-1, 3), axis=0))
    (x0, y0, x1, y1), text = MM004_TITLE
    glow_text(sheet, text, 58, (x0 + x1) / 2, (y0 + y1) / 2, (255, 255, 255), (70, 30, 190), (120, 70, 255), stroke=3, blur=4)
    (x0, y0, x1, y1), text = MM004_LINE
    place(sheet, english_opening.render(text, 22, (255, 255, 255), (20, 16, 30), 1, font_path=ITALIC_FONT), (x0 + x1) / 2, (y0 + y1) / 2)
    for k, line in enumerate(lines):
        img = english_opening.render(line, 20, (255, 255, 255), (8, 8, 12), 1, font_path=ITALIC_FONT)
        pack.put(sheet.over, img[..., 3].astype(float) / 255, bx0 + 10, by0 + 12 + k * 36, img[..., :3].astype(float))
    for box, text in MM004_LINKS:
        underlined(sheet, box, text, LINK_BLUE, 19)
    sheet.save(f"{REL}/mm004.bin", 0)


# The full-screen pages (han*, kojin*, battle*): the Japanese is filled in from the art round it and the
# English drawn where it began. These are backgrounds, drawn whole, so the English may run past the
# Japanese. A box need only hold its Japanese and nothing else as bright; where the English starts (or
# centres, or ends) comes from the Japanese found in it.
BODY_FONT = "/usr/share/fonts/truetype/liberation/LiberationSans-Italic.ttf"
WHITE, INK = (255, 255, 255), (10, 10, 10)


def title(box, text, size=60, align="c"):
    return dict(kind="title", box=box, text=text, size=size, align=align, lit="title")


def label(box, text, edge=WHITE):
    """A callout: white, edged black, then white (or edge) again; its circled number is drawn anew."""
    return dict(kind="label", box=box, text=text, edge=edge, align="l", lit="white")


def note(box, head, lines, head_size=26, size=16, indent=28, hang=False, lit="white"):
    """A head (with its circled number, if any), then ": " and the first line; the rest under it, indented
    (hang: under the first line)."""
    return dict(kind="note", box=box, head=head, lines=lines, head_size=head_size, size=size, indent=indent,
                hang=hang, align="l", lit=lit)


def text(box, text, size=17, align="l", lit="white", font=BODY_FONT, stroke=1):
    """Lines of plain text, the block centred on the box's middle."""
    return dict(kind="text", box=box, text=text, size=size, align=align, lit=lit, font=font, stroke=stroke)


def head(box, text, size=24, align="l", lit="white"):
    return dict(kind="text", box=box, text=text, size=size, align=align, lit=lit, font=ITALIC_FONT, stroke=2, pad=5)


def tabbed(box, text):
    return dict(kind="tab", box=box, text=text)


def link(box, text):
    return dict(kind="link", box=box, text=text, align="c", lit="link")


NAV_LINKS = [link((58, 702, 126, 730), "← Back"), link((396, 710, 522, 742), "↓ MENU"),
             link((874, 713, 941, 743), "Next →")]
STAT_ROWS = [("Power", "max HP and physical defense"), ("Speed", "Evade and Move"), ("Skill", "Sight and Body"),
             ("Virtue", "max MP and Will"), ("Mind", "max MP and Barrier"), ("Endure", "max HP and HP Regen"),
             ("Spirit", "max MP and MP Regen")]
GETTING_READY = "Getting it ready:"
PAGES = {
    "han002": [tabbed((5, 28, 168, 52), "Team Area"),
               note((386, 142, 708, 202), "① Teams", ["Each Team set up here", "takes its Lectures as one."])],
    "han003": [tabbed((397, 30, 510, 56), "Quest Select"), tabbed((397, 309, 510, 336), "Lecture Select"),
               note((200, 130, 702, 184), "② Quests", ["Choose one to go out on the Quest MAP.",
                                                           "Good for real combat stats and learning skills."]),
               note((203, 396, 577, 452), "② Lectures", ["Raise base stats by taking", "a teacher's class."])],
    "han004": [note((158, 104, 642, 162), "③ Advance Button", ["Once every Team's plan is set,",
                                                                    "this button carries it out."])],
    "han005": [title((262, 38, 765, 112), "Team Screen Guide"),
               label((156, 122, 255, 152), "① Teacher"), label((262, 255, 372, 284), "② Team Status"),
               label((409, 122, 502, 152), "③ Students"), label((127, 424, 222, 456), "④ Student Count"),
               label((183, 465, 360, 498), "⑤ Student Sort Buttons"), label((373, 445, 520, 476), "⑥ Student Icons"),
               label((535, 481, 664, 511), "⑦ Student Pool"), label((94, 705, 205, 737), "⑧ Teacher Count"),
               label((160, 669, 340, 701), "⑨ Teacher Sort Buttons"), label((297, 615, 449, 646), "⑩ Teacher Icons"),
               label((537, 644, 667, 674), "⑪ Teacher Pool")],
    "han009": [title((228, 36, 798, 108), "Lecture Select Guide"),
               label((308, 481, 452, 511), "① Lecturer"), label((612, 373, 764, 403), "② Lecture List"),
               label((794, 399, 984, 429), "③ Lecture Stock"), label((733, 667, 837, 697), "④ Description")],
    "han011": [title((226, 36, 792, 108), "Quest Select Guide"),
               label((284, 202, 450, 231), "① Quest List"), label((714, 104, 847, 133), "② Quest Team"),
               label((798, 151, 1007, 181), "③ Forced Quest Lamp"), label((798, 199, 987, 229), "④ Quest Stock"),
               label((714, 442, 814, 471), "⑤ Description")],
    "kojin001": [title((188, 12, 788, 88), "Status Screen Guide"),
                 label((34, 430, 188, 462), "① Portrait"), label((278, 146, 450, 178), "② Main Status"),
                 label((278, 238, 467, 270), "③ Attack Status"), label((278, 323, 497, 355), "④ Defense Status"),
                 label((440, 90, 592, 122), "⑤ Unit Level"), label((610, 90, 747, 122), "⑥ Current Class"),
                 label((596, 411, 722, 443), "⑥ Base Stats"), label((770, 161, 927, 194), "⑦ Skill Pocket"),
                 label((782, 335, 952, 368), "⑧ Item Pocket")],
    "kojin002": [tabbed((600, 297, 704, 318), "Unit Details")],
    "kojin003": [tabbed((334, 32, 448, 52), "Unit LV"), tabbed((310, 177, 446, 198), "Base Stats"),
                 tabbed((198, 265, 350, 287), "Portrait"),
                 text((450, 64, 702, 106), ": The unit's level, from its total EXP.\n  The highest is 50.", 16)]
                + [text((368, y - 12, 704, y + 12), f"“{name}”: mostly {what}.", 16)
                   for y, (name, what) in zip((323, 350, 379, 407, 435, 464, 493), STAT_ROWS)]
                + [text((378, 510, 560, 534), "The sum of the above.", 16)],
    "kojin004": [note((86, 123, 520, 172), "HP", ["Falls bit by bit as the unit is hit;", "at 0 the unit “retires”."],
                      24, indent=-14),
                 note((132, 178, 600, 228), "Mental Points", ["used up by skills.", "Without enough, skills can't be used."],
                      24, indent=44),
                 note((196, 233, 704, 276), "Engage Time", ["the total time it can stay on the MAP.",
                                                           "At 0 it retires, and is out for the rest of that Quest."],
                      24, indent=22),
                 text((172, 258, 216, 278), ""),     # the start of its second line
                 note((88, 298, 520, 346), "HP Regen", ["how fast HP comes back by itself.",
                                                        "8 steps from S to F; S recovers fastest."], 24, indent=-10),
                 note((145, 353, 580, 403), "MP Regen", ["how fast MP comes back by itself.",
                                                         "8 steps from S to F; S recovers fastest."], 24, indent=5),
                 note((216, 413, 580, 461), "Sight", ["how far the unit sees. The higher", "the number, the farther."],
                      24, indent=2),
                 note((288, 472, 704, 519), "Move", ["how fast the unit moves.",
                                                     "8 steps from S to F; S moves fastest."], 24, indent=12)],
    "kojin005": [note((84, 208, 400, 242), "Hit", ["a normal attack's chance to hit."], 24),
                 note((132, 256, 440, 288), "Power", ["the strength of a normal attack that HITs."], 24),
                 note((198, 300, 580, 332), "Extra", ["shown when an item adds an effect."], 24),
                 note((86, 372, 560, 406), "Range", ["how far a normal attack reaches."], 24),
                 note((138, 426, 560, 460), "Area", ["how many enemies round it are hit."], 24),
                 note((200, 478, 700, 512), "Recovery", ["the pause after a normal attack."], 24)],
    "kojin006": [note((110, 43, 420, 77), "Evade", ["affects dodging attacks."], 24),
                 note((162, 98, 440, 132), "Armor", ["defense against “physical attacks”."], 24),
                 note((228, 153, 510, 188), "Barrier", ["defense against “magic attacks”."], 24),
                 note((116, 328, 575, 362), "Body", ["lessens the damage from BAD conditions."], 24),
                 note((178, 383, 635, 417), "Will", ["shortens how long BAD conditions last."], 24)],
    "kojin007": [tabbed((458, 26, 505, 46), "Class"), tabbed((550, 293, 650, 315), "Equipped Skills"),
                 tabbed((600, 319, 692, 340), "+ Items")],
    "kojin008": [title((205, 22, 830, 108), "Skill Equip Guide"),
                 label((94, 316, 242, 348), "① Skills Button"), label((248, 348, 382, 380), "② Equip Limit"),
                 label((404, 316, 550, 348), "③ Equip Pocket"), label((238, 661, 382, 693), "④ Skill Pool"),
                 label((486, 665, 647, 697), "⑤ Skill Icon"), label((566, 358, 747, 390), "⑥ Owned Button"),
                 label((756, 320, 924, 352), "⑦ Learn Button"), label((806, 434, 967, 466), "⑧ Class EXP"),
                 label((804, 590, 892, 622), "⑨ Description")],
    "kojin011": [note((198, 86, 704, 124), "Usable Skill", ["can be equipped on the unit."]),
                 note((198, 138, 704, 174), "Learnable Skill", ["pay EXP to make it “usable”."]),
                 note((198, 183, 704, 220), "Unusable Skill", ["can't be equipped."]),
                 note((198, 226, 704, 266), "Unfound Skill", ["not found yet."]),
                 tabbed((474, 345, 582, 366), "Skill Pool")],
    "kojin012": [note((74, 101, 704, 152), "Opposed", ["skills of opposed elements can't be equipped together."]),
                 *[head(box, word, 30, align) for y, pair in zip((191, 247, 307, 367, 425), (
                     ("Heavy", "Swift"), ("Front", "Back"), ("Holy", "Dark"), ("Earth", "Wind"), ("Water", "Fire")))
                   for box, word, align in (((166, y - 22, 216, y + 22), pair[0], "r"), ((280, y - 22, 334, y + 22), pair[1], "l"))]],
    "kojin017": [title((180, 38, 862, 112), "Item Equip Guide"),
                 label((94, 363, 242, 395), "① Equip Button"), label((248, 341, 382, 373), "② Equip Limit"),
                 label((410, 368, 552, 400), "③ Equip Pocket"), label((190, 696, 352, 728), "④ Item Pool"),
                 label((402, 656, 582, 688), "⑤ Item Icon"), label((572, 338, 750, 370), "⑥ Owned Button"),
                 label((694, 674, 792, 706), "⑦ Description")],
    "kojin020": [note((250, 96, 704, 132), "Equippable Item", ["can be equipped on the unit."]),
                 note((250, 146, 704, 182), "Unequippable Item", ["requirements not met."]),
                 tabbed((172, 320, 298, 341), "Item Pool")],
    "kojin022": [title((188, 32, 772, 110), "Change Class Guide"),
                 label((98, 402, 274, 434), "① Class Button"), label((318, 383, 447, 415), "② Class Pool"),
                 label((464, 656, 610, 688), "③ Class Icon"), label((553, 363, 682, 395), "④ Class Notes"),
                 label((718, 373, 847, 405), "⑤ OK Button")],
    "kojin026": [title((188, 36, 787, 110), "Raise Stats Guide"),
                 label((108, 366, 290, 398), "① Stats Up Button"), label((338, 338, 480, 370), "② Stats Up Table"),
                 label((676, 363, 772, 395), "③ Description"), label((803, 528, 937, 560), "④ Class EXP"),
                 label((696, 660, 827, 692), "⑤ OK Button")],
    "kojin031": [title((188, 32, 747, 110), "Student List Guide"),
                 label((128, 648, 297, 680), "① Student List Button"), label((338, 360, 467, 392), "② Student Pool"),
                 label((298, 535, 442, 567), "③ Student Icons")],
    "battle001": [title((30, 15, 305, 88), "Screen Layout", 52, "l"),
                  label((212, 142, 368, 186), "① MENU Button", (230, 200, 40)),
                  label((370, 60, 502, 104), "② Timer", (60, 220, 90)),
                  text((503, 76, 782, 102), "Shows time left, time elapsed, etc.", 18),
                  text((243, 190, 478, 215), "Pause or quit the game", 18),
                  text((405, 256, 762, 287), "Check the Quest's goal / PAUSE", 20, "r"),
                  text((495, 303, 762, 334), "Chat mode ON / OFF", 20, "r"),
                  label((172, 458, 312, 504), "③ Status Panel", (60, 90, 255)),
                  text((190, 503, 375, 529), "The selected unit's details", 18),
                  label((422, 432, 598, 478), "③ Unit Actions", (240, 60, 40)),
                  text((446, 481, 728, 507), "Change AI, use skills, etc.", 18),
                  text((743, 474, 1012, 537), "Green → your units\nYellow → neutral / no side\nRed → enemy units", 17),
                  label((718, 570, 832, 614), "⑤ Mini MAP", (60, 220, 220)),
                  label((548, 652, 642, 696), "④ Notes", WHITE),
                  text((568, 696, 822, 722), "Hover over things for details", 18)] + NAV_LINKS,
    "battle002": [title((30, 12, 708, 88), "Player Unit AI", 54, "l"),
                  note((223, 168, 680, 217), "Attack AI", ["attacks enemies within sight."], 30, 18),
                  note((223, 303, 615, 350), "Guard AI", ["its sight is its AI move range."], 30, 18),
                  note((220, 437, 625, 489), "Hold AI", ["attacks only enemies in range."], 30, 18),
                  note((220, 583, 905, 630), "Wait AI", ["does nothing; acts once when the player gives an order."], 30, 18)]
                 + NAV_LINKS,
    "battle003": [title((30, 12, 518, 88), "The Side Board", 54, "l"),
                  head((172, 128, 398, 174), "Unit on the Field", 30),
                  text((218, 178, 605, 204), ": A unit out on the field.", 18),
                  head((172, 243, 468, 292), "Unit on the Field, Selected", 30),
                  text((218, 303, 665, 330), ": A unit on the field, selected.", 18),
                  head((172, 363, 400, 407), "Unit Not Out", 30),
                  text((218, 413, 555, 440), ": A unit not out on the field.", 18),
                  head((175, 480, 472, 524), "Unit Not Out, Selected", 30),
                  text((218, 534, 645, 560), ": A unit not on the field, selected.", 18),
                  head((175, 596, 438, 644), "Unit That Can't Go Out", 30),
                  text((214, 650, 780, 677), ": At 0 HP. Once fully healed it can go out again.", 18)] + NAV_LINKS,
    "battle004": [title((30, 12, 438, 88), "Cursors", 54, "l"),
                  text((160, 111, 645, 137), "On a Quest, the “cursor” changes shape.", 18),
                  head((122, 163, 268, 199), "Main Cursor"), text((158, 204, 322, 230), "The basic cursor", 18),
                  head((128, 276, 230, 312), "Basic Attack"),
                  text((143, 314, 382, 360), "One unit selected,\nover an enemy unit", 18),
                  head((136, 396, 238, 434), "Basic Heal"),
                  text((138, 429, 400, 478), "A Priest selected,\nover an ally unit", 18),
                  head((464, 146, 680, 185), "Enemy Skill Cursor"),
                  text((480, 190, 612, 213), GETTING_READY, 18), text((480, 209, 661, 231), "over an enemy, it turns", 18, "r"),
                  text((688, 207, 757, 235), "", 18), text((480, 229, 712, 254), "and the skill can be used.", 18),
                  head((466, 278, 702, 317), "Area Skill Cursor"),
                  text((486, 316, 782, 342), "Usable when in range.", 18),
                  head((466, 376, 624, 412), "Ally Skill"),
                  text((480, 408, 612, 431), GETTING_READY, 18), text((480, 427, 689, 449), "over an ally, it turns", 18, "r"),
                  text((712, 431, 778, 459), "", 18), text((480, 452, 702, 477), "and the skill can be used.", 18),
                  head((466, 498, 667, 535), "Self-Centred Skill"),
                  text((484, 538, 614, 561), GETTING_READY, 18), text((484, 559, 687, 581), "over yourself, it turns", 18, "r"),
                  text((712, 561, 780, 589), "", 18), text((484, 582, 714, 606), "and the skill can be used.", 18),
                  # each cursor's three lines, round its picture
                  *[text(box, "") for box in ((478, 186, 662, 254), (690, 186, 758, 254), (662, 229, 690, 254),
                                              (478, 404, 688, 478), (714, 404, 780, 478), (688, 461, 714, 478),
                                              (482, 536, 686, 606), (714, 536, 780, 606), (686, 581, 714, 606))],
                  head((468, 613, 627, 652), "Skill Unusable"),
                  text((488, 654, 812, 680), "The selected skill can't be used.", 18)] + NAV_LINKS,
    "battle005": [title((30, 12, 328, 88), "Hotkeys", 54, "l"),
                  text((103, 136, 282, 162), ": Opens the MENU.", 17),
                  head((83, 206, 450, 236), "Double-click a Side Board unit", 20),
                  text((98, 233, 358, 259), ": Moves the view to that unit.", 17),
                  head((83, 310, 328, 340), "Ctrl + click an ally unit", 20),
                  text((98, 341, 560, 368), ": Adds an unselected unit, or drops a selected one.", 17),
                  head((83, 436, 368, 467), "Ctrl + number key (0-9)", 20),
                  text((98, 465, 228, 491), ": Sets a party.", 17),
                  head((86, 563, 302, 594), "Number key (0-9)", 20),
                  text((98, 593, 385, 619), ": Selects that party.", 17),
                  text((606, 150, 943, 197), ": With one unit selected, skill mode.\n   Let go for normal mode.", 17),
                  head((580, 256, 675, 284), "F1-F8", 20),
                  text((606, 282, 922, 308), ": Switches skills. Works with several selected.", 17)] + NAV_LINKS,
    "battle006": [title((100, 28, 640, 102), "Engage Time", 56, "l"),
                  text((352, 131, 828, 157), "The higher a unit's LV, the less of this time it has.", 19, "c", "any"),
                  text((120, 230, 785, 257), "On a Quest, the time a unit can stay on the field is its “Engage Time”.", 19, "c", "any"),
                  text((145, 266, 833, 292), "Units on the Side Board use up no Engage Time.", 19, "c", "any"),
                  label((170, 346, 350, 381), "Engage Time", (40, 150, 255)),
                  label((112, 388, 242, 420), "Side Board", (200, 180, 120)),
                  text((145, 430, 883, 457), "A “Return” from the field to the “Side Board” costs [60 sec].", 19, "c", "any"),
                  text((115, 465, 903, 492), "With under 60 sec of Engage Time left, you can't “Return”: take care.", 19, "c", "any"),
                  text((137, 511, 896, 537), "“Retiring” at zero HP costs 50% of Engage Time as a penalty.", 19, "c", "any"),
                  text((203, 553, 778, 600), "* “Retire” with under 60 sec left, and Engage Time\n   drops to zero: the unit can't go out again.", 19, "l", "any")]
                 + NAV_LINKS,
    "battle007": [title((115, 12, 845, 88), "Skills by Class", 54, "l"),
                  text((143, 93, 615, 142), "Some skills can't be equipped together because of their elements.\nOn top of that, classes limit skills.", 17),
                  text((143, 151, 412, 177), "This page covers the latter.", 17),
                  note((168, 192, 880, 260), "Swordsman", ["can equip other classes' skills up to LV4."], 30, 17, hang=True),
                  note((168, 272, 880, 345), "Warrior", ["Swordsman and Priest skills up to [LV4],",
                                                         "other classes' skills up to [LV1]."], 30, 17, hang=True),
                  note((168, 357, 880, 428), "Hunter", ["Swordsman and Priest skills up to [LV1],",
                                                        "other classes' skills up to [LV3]."], 30, 17, hang=True),
                  note((168, 430, 880, 503), "Mage", ["can't equip Priest skills;",
                                                      "other classes' skills up to [LV2]."], 30, 17, hang=True),
                  note((168, 510, 880, 581), "Priest", ["can't equip Mage skills;",
                                                        "other classes' skills up to [LV3]."], 30, 17, hang=True),
                  text((488, 598, 893, 645), "[Non-elemental magic]: up to [LV7] for Mages,\nup to [LV4] for other classes.", 17),
                  text((488, 654, 938, 682), "[Secret Arts] are the exception: anyone can equip [all LV].", 17)] + NAV_LINKS,
    "battle008": [title((145, 20, 660, 98), "Range and Area", 56, "l"),
                  text((168, 101, 738, 147), "Besides Range, some skills also have an “Area”.\nIt is not normally shown on a Quest: take care.", 18),
                  head((66, 180, 320, 217), "For example, an “Area Skill”", 22),
                  note((543, 201, 928, 242), "Area", ["where the skill takes effect"], 24, 17),
                  note((646, 310, 988, 347), "Cast Point", ["the player chooses it freely"], 24, 17),
                  head((268, 538, 342, 580), "Caster", 26), head((516, 555, 564, 593), "Range", 26),
                  text((188, 650, 703, 684), "* [Area] is given in each skill's description.", 22)] + NAV_LINKS,
    "battle009": [title((100, 20, 765, 98), "Terrain and Movement", 54, "l"),
                  text((138, 98, 448, 124), "About moving on the MAP.", 18),
                  note((66, 133, 600, 197), "Terrain", ["terrain changes how far a unit moves.", "Keep to paved roads."],
                       24, 17, hang=True),
                  note((66, 363, 600, 427), "Movement Type", ["where a unit can go", "depends on its movement type."],
                       24, 17, hang=True),
                  note((196, 443, 980, 507), "Land", ["students and most units. No deep water or cliffs."], 34, 17),
                  note((196, 523, 930, 587), "Water", ["water units keep to the water's edge: fewer places than land."], 34, 17),
                  note((196, 603, 875, 667), "Air", ["air units can also go where land units can't."], 34, 17)] + NAV_LINKS,
    "battle010": [title((100, 32, 740, 108), "Team Mood Icons", 56, "l"),
                  text((133, 118, 643, 174), "A Team's mood changes with who is in it,\nand so does the EXP it gets from Quests / Lectures.", 18),
                  text((236, 263, 408, 292), "The best mood.", 19), text((236, 373, 443, 402), "A very good mood.", 19),
                  text((236, 493, 383, 520), "A good mood.", 19), text((236, 613, 423, 642), "A fairly good mood.", 19),
                  text((696, 383, 823, 410), "Average.", 19), text((696, 511, 928, 539), "A little unsettled.", 19),
                  text((696, 620, 888, 647), "An awkward mood.", 19)] + NAV_LINKS,
    "battle011": [title((50, 12, 685, 88), "Good Status", 56, "l"),
                  text((98, 120, 703, 147), "· Magic can give a [GOOD status], which helps a Quest along.", 17),
                  head((188, 190, 323, 227), "[Attack Up]", 24), text((193, 230, 518, 257), "Raises physical [Power / Hit].", 17),
                  head((188, 290, 323, 327), "[Armor Up]", 24), text((193, 330, 458, 357), "Raises [Armor / Evade] and more.", 17),
                  head((188, 390, 323, 427), "[Magic Guard Up]", 24), text((193, 430, 533, 457), "Raises [Barrier / Will] and more.", 17),
                  head((188, 490, 328, 527), "[All Guard Up]", 24), text((193, 530, 458, 572), "Raises every stat.\nIt doesn't last long.", 17),
                  head((188, 590, 323, 627), "[Sight Up]", 24), text((193, 630, 413, 657), "Widens [Sight].", 17),
                  head((580, 190, 668, 227), "[Acrobatics]", 24), text((593, 230, 878, 257), "Raises [Move / Evade] a little.", 17),
                  head((580, 290, 668, 327), "[Haste]", 24),
                  text((593, 330, 871, 387), "Raises [Move].\nAlso shortens attacks'\n[Prep / Recovery] time.", 17),
                  head((623, 596, 933, 630), "* Effects last for a set time.", 22)] + NAV_LINKS,
    "battle012": [title((50, 12, 640, 98), "Bad Status", 56, "l"),
                  text((113, 106, 703, 147), "· A “BAD status” is a harmful effect from an attack or magic.\n  A [Return] cures BAD statuses.", 17),
                  head((178, 190, 243, 227), "[Poison]", 24), text((186, 230, 408, 254), "Lowers HP for a while.", 17),
                  head((178, 290, 268, 327), "[Deadly Poison]", 24), text((186, 330, 468, 354), "Lowers HP a lot for a while.", 17),
                  head((178, 390, 268, 427), "[Fetters]", 24), text((186, 430, 413, 454), "Lowers MP for a while.", 17),
                  head((178, 490, 268, 527), "[Curse]", 24), text((186, 530, 468, 554), "Lowers MP a lot for a while.", 17),
                  head((178, 590, 268, 627), "[Frostbite]", 24), text((186, 630, 478, 654), "Slows movement for a while.", 17),
                  head((578, 190, 668, 227), "[Slow]", 24),
                  text((586, 230, 903, 270), "Slows not just movement, but\nattacks' prep and recovery too.", 17),
                  head((578, 290, 758, 327), "[Physical Down]", 24), text((588, 330, 938, 354), "Physical attack and armor are lowered.", 17),
                  head((578, 390, 758, 427), "[Magic Down]", 24), text((588, 430, 898, 454), "Magic attack and Barrier are lowered.", 17),
                  head((623, 596, 933, 630), "* Effects last for a set time.", 22)] + NAV_LINKS,
}


def lit_in(frame, box, kind):
    """What of box is the Japanese: white strokes, or (any) coloured ones too; the title's anything not
    black; the links' blue."""
    x0, y0, x1, y1 = box
    rgb = frame[y0:y1, x0:x1, :3]
    lit = {"white": rgb.min(axis=2) > 150, "any": rgb.max(axis=2) > 140,
           "title": rgb.max(axis=2) > 60, "link": rgb.max(axis=2) > 100}[kind]
    grow = 4 if kind == "title" else 2
    mask = np.zeros(frame.shape[:2], bool)
    mask[y0:y1, x0:x1] = lit
    return ndimage.binary_dilation(mask, iterations=grow) & box_mask(frame, box, grow if kind == "title" else 0), lit


def box_mask(frame, box, grow=0):
    x0, y0, x1, y1 = box
    m = np.zeros(frame.shape[:2], bool)
    m[max(y0 - grow, 0):y1 + grow, max(x0 - grow, 0):x1 + grow] = True
    return m


def anchor(item, lit):
    """Where the English goes: the Japanese's left (from its middle rows for a label, as leader lines come
    in above and below), centre or right."""
    x0, y0, x1, y1 = item["box"]
    if item["kind"] in ("label", "note"):
        mid = (y1 - y0) // 2 if item["kind"] == "label" else min(18, (y1 - y0) // 2)
        cols = lit[mid - 3:mid + 4].any(axis=0)
    else:
        cols = lit.any(axis=0)
    if not cols.any():
        return {"l": x0, "c": (x0 + x1) / 2, "r": x1}[item["align"]]
    xs = np.where(cols)[0]
    return {"l": x0 + xs[0], "c": x0 + (xs[0] + xs[-1] + 1) / 2, "r": x0 + xs[-1] + 1}[item["align"]]


CIRCLED = "①②③④⑤⑥⑦⑧⑨⑩⑪⑫⑬⑭⑮"


def circled(sheet, n, d, x, cy, edge=WHITE):
    """A circled number d across, left edge at x, in the labels' look (DejaVu stops at 10)."""
    big = d * 4
    im = Image.new("L", (big + 8, big + 8))
    draw = ImageDraw.Draw(im)
    draw.ellipse((4, 4, big + 4, big + 4), outline=255, width=round(big * 0.09))
    font = ImageFont.truetype(ITALIC_FONT, round(big * (0.68 if n < 10 else 0.56)))
    draw.text((big / 2 + 4, big / 2 + 5), str(n), font=font, fill=255, anchor="mm")
    a = np.array(im.resize(((big + 8) // 4, (big + 8) // 4), Image.LANCZOS)).astype(float) / 255
    a = np.pad(a, 3)
    for grow, colour in ((2, edge), (1, INK), (0, WHITE)):
        cover = ndimage.grey_dilation(a, size=2 * grow + 1) if grow else a
        pack.put(sheet.over, cover, x - 3, round(cy - a.shape[0] / 2), colour)
    return a.shape[1] - 6


def left_text(sheet, text, size, x, cy, font=ITALIC_FONT, edge=None, stroke=2, align="l"):
    """text with its left edge (or centre, or right) at x, centred on cy: white edged black, and as the
    labels are, edged again in edge."""
    looks = (((WHITE, edge, stroke + 2),) if edge else ()) + ((WHITE, INK, stroke),)
    if text[:1] in CIRCLED:     # the number, then the rest
        w = circled(sheet, CIRCLED.index(text[0]) + 1, round(size * 1.3), x, cy, edge or WHITE)
        return w + 2 + left_text(sheet, text[1:].strip(), size, x + w + 2, cy, font, edge, stroke)
    imgs = [english_opening.render(text, size, colour, e, s, font_path=font) for colour, e, s in looks]
    w = imgs[0].shape[1]
    left = {"l": x, "c": x - w / 2, "r": x - w}[align]
    for img in imgs:
        place(sheet, img, left + w / 2, cy)
    return w


def draw_item(sheet, item, x):
    x0, y0, x1, y1 = item["box"]
    cy = (y0 + y1) / 2
    kind = item["kind"]
    if kind == "title":
        img = english_opening.render(item["text"], item["size"], WHITE, INK, 2, font_path=ITALIC_FONT)
        cx = {"l": x + img.shape[1] / 2, "c": x}[item["align"]]
        glow_text(sheet, item["text"], item["size"], cx, cy, WHITE, INK, (0, 0, 0), stroke=2, blur=3)
    elif kind == "label":
        left_text(sheet, item["text"], round((y1 - y0) * 0.66), x + 1, cy, edge=item["edge"])
    elif kind == "note":
        two = len(item["lines"]) > 1
        mid = y0 + item["head_size"] * 0.75 if two else cy
        w = left_text(sheet, item["head"], item["head_size"], x + 1, mid)
        size = item["size"]
        left_text(sheet, ":" + item["lines"][0], size, x + w + 3, mid + item["head_size"] * 0.15, stroke=1)
        under = x + w + 3 + size * 0.5 if item["hang"] else x + item["indent"]
        for k, line in enumerate(item["lines"][1:]):
            left_text(sheet, line, size, under, mid + item["head_size"] * 0.15 + size * 1.3 * (k + 1), stroke=1)
    elif kind == "text":
        lines = item["text"].split("\n")
        step = item["size"] * 1.2
        for k, line in enumerate(lines):
            if line.strip():
                left_text(sheet, line, item["size"], x, cy + (k - (len(lines) - 1) / 2) * step, item["font"],
                          stroke=item["stroke"], align=item["align"])
    elif kind == "link":
        underlined(sheet, item["box"], item["text"], LINK_BLUE, 19)


def page(name, items, preview=None):
    sheet = pack.Sheet(english_opening.decode(pack.frames_of(BIN / f"{name}.bin")[0]).astype(float))
    frame = sheet.frame
    mask = np.zeros(frame.shape[:2], bool)
    anchors = []
    for item in items:
        if item["kind"] == "tab":
            tab(sheet, item["box"], item["text"])
            anchors.append(None)
            continue
        pad = item.get("pad", 5 if item["kind"] in ("label", "note") else 0)
        x0, y0, x1, y1 = item["box"]
        m, _ = lit_in(frame, (x0, max(y0 - pad, 0), x1, y1 + pad), item["lit"])
        _, lit = lit_in(frame, item["box"], item["lit"])
        mask |= m
        anchors.append(anchor(item, lit))
    if mask.any():
        inpaint(sheet, mask)
    for item, x in zip(items, anchors):
        if x is not None:
            draw_item(sheet, item, x)
    sheet.save(f"{REL}/{name}.bin", 0, preview, name)


# The story's chapter cards (data/adv/bin/tcMMWW.bin, the month and week in the name; most only on the JP
# disc): white text edged dark on nothing, so the Japanese is erased whole and the English drawn anew.
CHAPTERS = {
    "tc0101": "Scattering, White Wings", "tc0102": "The Thirteenth King's Cup", "tc0103": "Showdown",
    "tc0104": "The Round Table", "tc0105": "Here, Neither Light nor Dark", "tc0201": "Training",
    "tc0202": "In a Faint Sigh", "tc0203": "Hand in Hand", "tc0204": "What It Means to Believe",
    "tc0205": "Passion, Staked on the Sword", "tc0301": "Words of Power", "tc0302": "Premonition",
    "tc0303": "Toward a Master's Distant Heights", "tc0304": "One Heart", "tc0401": "Return",
    "tc0402": "Each Their Own Beginning", "tc0403": "Into the Maelstrom", "tc0404": "Feelings Kept Close",
    "tc0405": "Between Chaos and Truth", "tc0501": "Dawn", "tc0502": "The Noble Four", "tc0503": "A Wish Out of Reach",
    "tc0504": "One Who Finds Oneself", "tc0505": "Memory", "tc0601": "Snowflowers of the North Wall",
    "tc0602": "Because I Care", "tc0603": "Gazing at the Silent Moon", "tc0604": "Eternal Dark", "tc0605": "The Scales",
    "tc0701": "Doubt", "tc0702": "Before the Battle", "tc0703": "Resolve", "tc0704": "The Climax",
    "tc0705": "In the Dark Depths", "tc0801": "A Brief Rest", "tc0802": "The Seal", "tc0803": "Taking Endymion",
    "tc0804": "Home Sweet Home", "tc0805": "True Intent", "tc0901": "The Rose and the Young Lord",
    "tc0902": "Resolve of White Petals", "tc0903": "Sortie from the Unmoving Bridge", "tc0904": "Living for Justice",
    "tc0905": "Stirrings", "tc1001": "Bonds Undone", "tc1002": "Interlude", "tc1003": "Thaw", "tc1004": "The Two of Us",
    "tc1005": "Feelings, in Each Heart", "tc1101": "Where the Vow Leads", "tc1102": "A Smile in Deep Wrinkles",
    "tc1103": "The Foe's Name Is Friend", "tc1104": "The School's Darkness", "tc1105": "Budding",
    "tc1201": "Living for Justice", "tc1202": "Looking to the Distant Sky", "tc1203": "Hall of Contemplation",
    "tc1204": "Forward", "tc1205": "Festival of Thanks",
}
MONTH_NAMES = ["January", "February", "March", "April", "May", "June", "July", "August", "September", "October",
               "November", "December"]
CARD_FONT = "/usr/share/fonts/truetype/liberation/LiberationSerif-Regular.ttf"
CARD_DATE, CARD_TITLE = (5, 28), (43, 87)      # the rows of the date and the title; both centred on x 511


def chapter_cards(preview=None):
    for name, title in CHAPTERS.items():
        path = BIN / f"{name}.bin"
        if not path.exists():
            path = pack.GAME / REL / f"{name}.bin"
        frame = english_opening.decode(pack.frames_of(path)[0])
        sheet = pack.Sheet(frame.astype(float))
        date = f"{MONTH_NAMES[int(name[2:4]) - 1]}, Week {int(name[4:6])}"
        for text, (y0, y1), size in ((date, CARD_DATE, 17), (title, CARD_TITLE, 38)):
            img = english_opening.render(text, size, WHITE, (40, 40, 40), 2, font_path=CARD_FONT)
            if img.shape[1] > 1000:
                img = np.array(Image.fromarray(img).resize((1000, img.shape[0]), Image.LANCZOS))
            place(sheet, img, 511, (y0 + y1) / 2)
        sheet.save(f"{REL}/{name}.bin", 0, preview, name)
        mask = np.zeros(frame.shape, np.uint8)
        mask[frame[..., 3] > 0] = 255
        Image.fromarray(mask, "RGBA").save(pack.PACK / "images" / REL / f"{name}.bin" / "0.erase.png", optimize=True)


def place(sheet, img, cx, cy):
    """img (RGBA from english_opening.render) centred on (cx, cy)."""
    img = img.astype(float)
    pack.put(sheet.over, img[..., 3] / 255, round(cx - img.shape[1] / 2), round(cy - img.shape[0] / 2), img[..., :3])


def button_text(text, width):
    size = 22
    while True:
        img = english_opening.render(text, size, (255, 255, 255), (40, 28, 24), 1, font_path=BUTTON_FONT, tracking=1.2)
        if img.shape[1] <= width or size <= 13:
            return img
        size -= 1


def build(preview=None):
    frames = {name: english_opening.decode(pack.frames_of(BIN / f"{name}.bin")[0]).astype(float) for name in SHEETS}
    tile = tile_of([frames[n] for n in ("mm001", "mm002", "mm003")])
    for name, spec in SHEETS.items():
        frame = frames[name]
        sheet = pack.Sheet(frame)
        for box, text in spec.get("buttons", []):
            cx, cy = clear_face(sheet, box)
            place(sheet, button_text(text, box[2] - box[0] - 8), (box[0] + box[2]) / 2, cy)
        for box, lines in spec.get("notes", []):
            clear_pink(sheet, tile, box)
            for k, line in enumerate(lines):     # left-aligned, the second line indented, as the Japanese is
                img = english_opening.render(line.strip(), 21, (255, 255, 255), (16, 12, 12), 2, font_path=NOTE_FONT)
                indent = 28 if line.startswith(" ") else 0
                pack.put(sheet.over, img[..., 3].astype(float) / 255, box[0] + indent, box[1] + k * 30,
                         img[..., :3].astype(float))
        if "title" in spec:
            clear_pink(sheet, tile, TITLE_BOX, threshold=10)
            img = english_opening.render(spec["title"], TITLE_SIZE, (200, 255, 190), (20, 40, 20), 2, font_path=NOTE_FONT)
            glow = ndimage.gaussian_filter(img[..., 3].astype(float), 3) / 255 * 0.6
            cx, cy = 512, (TITLE_BOX[1] + TITLE_BOX[3]) / 2
            pack.put(sheet.over, np.clip(glow, 0, 1), round(cx - img.shape[1] / 2), round(cy - img.shape[0] / 2), (120, 220, 120))
            place(sheet, img, cx, cy)
        sheet.save(f"{REL}/{name}.bin", 0)
    parts()
    notes_page()
    for name, spec in PAGES.items():
        page(name, spec, preview)
    chapter_cards(preview)
