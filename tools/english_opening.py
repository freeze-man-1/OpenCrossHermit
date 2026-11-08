#!/usr/bin/env python3
"""The English text of the opening cinematic, for tools/make_english_pack.py.

The opening draws its words from sheets in data/op/opdata.bin: frame 0 (captions), frame 1
(title and staff names), frame 13 (the map's place names) and frame 22 (the title card).

Vertical Japanese captions can't hold horizontal English, so the words go on a new frame
and <bank>/layout.txt tells the game where (lang::spriteParts). The old pixels are
erased, not covered, so nothing of the Japanese is left around the new text.

The map's place names are drawn on plates cut from the map itself, so erasing a plate shows
the same picture: only the icon is left, found by comparing the plate with the map.

Frame 22 draws its text over art. The TW disc's copy has other text on the same art, which
gives back most of what's under the Japanese; the rest is filled from its neighbours. The
rebuilt art is saved as 22.diff.png, its difference from the JP texels, so the pack holds none.
"""
import struct
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy import ndimage

BODY = "/usr/share/fonts/opentype/urw-base35/P052-Bold.otf"
CREDIT = "/usr/share/fonts/truetype/liberation/LiberationSerif-Bold.ttf"
SS = 4
GUTTER = 3

CAPTION, TITLE, PLATES, CARD = 0, 1, 13, 22
WORDS = 48      # a new frame past the bank's last holds all the English
WORDS_W, WORDS_H = 2048, 1024
MAP = 43
MAP_Y = 128     # the map's top on screen

# Frame 0 keeps these: they're English already.
KEEP = [(0, 858, 414, 894), (0, 894, 388, 954), (484, 424, 640, 512), (678, 142, 804, 168), (804, 142, 922, 168)]

SMALL, BIG = 21, 31

# Sheet rectangle -> text, size, and where its centre goes as (old x, old y, x, y) on screen
# (the old top left, then the new centre). Without one it stays centred on the old rectangle.
CAPTIONS = {
    (796, 58, 914, 78): ("The wind howls.", SMALL),
    (0, 40, 408, 82): ("Dust whirls up, slips through the heart's cracks,\nand grinds them with madness.", SMALL),
    (0, 142, 256, 160): ("That day, the green looked faded.", SMALL),
    (258, 122, 496, 142): ("The world brims with Chaos.", SMALL),
    (716, 82, 952, 102): ("The wise have all set out.", SMALL),
    (478, 0, 954, 20): ("How many times now have I written to you?", SMALL),
    (496, 122, 812, 140): ("What were you doing just now, I wonder?", SMALL),
    (0, 0, 478, 20): ("Has your warm hand touched this letter?", SMALL),
    (0, 82, 378, 102): ("Lightning in dark clouds splits the land into light and shadow.", SMALL),
    (298, 102, 594, 122): ("The lamp is about to go out.", SMALL),
    (594, 102, 872, 122): ("Clear eyes are about to cloud over.", SMALL),
    (0, 102, 298, 122): ("The wings of dreams are about to be torn away.", SMALL),
    (378, 82, 716, 102): ("Now, only your words can move this heart.", SMALL),
    (0, 122, 258, 142): ("We parted, wet with rain.", SMALL),
    (408, 40, 796, 82): ("\"If you're ever in trouble, call for me,\"\nyou said, at the last.", SMALL),
    (438, 20, 874, 40): ("I have not once forgotten your eyes that day.", SMALL),
    (874, 20, 972, 38): ("One glance is all I ask.", SMALL),
    (0, 20, 438, 40): ("In the forest of holy stars and angels' bells, just once more.", SMALL),
    (954, 0, 988, 18): ("Yes.", SMALL),
    (796, 40, 932, 58): ("I want to see you.", SMALL),
    (0, 160, 678, 212): ("A name of dread in the Clermont Scripture", 34),
    # The words that fly past the title card.
    (0, 212, 60, 332): ("Revenge", 30),
    (60, 212, 120, 332): ("Eradication", 30),
    (120, 212, 180, 332): ("Fresh Blood", 30),
    (180, 212, 240, 332): ("Sunlight", 30),
    (240, 212, 300, 332): ("Return", 30),
    (300, 212, 360, 332): ("Blessing", 30),
    (360, 212, 420, 332): ("Despair", 30),
    (420, 212, 480, 332): ("Rose", 30),
    (480, 212, 540, 332): ("Curse", 30),
    (540, 212, 600, 332): ("Smile", 30),
    (600, 212, 912, 256): ("That day───", BIG),
    (0, 424, 484, 514): ("The Sages' school, Elmenas,\nwas cloaked in Chaos's darkness.", BIG),
    (600, 256, 906, 292): ("What crawled forth", BIG),
    (600, 292, 856, 328): ("was monstrous Chaos.", BIG),
    (548, 684, 590, 912): ("The\nSages", BIG, (875, 159, 948, 186)),
    (850, 598, 894, 998): ("had gone\nout into\nthe world.", BIG, (107, 187, 98, 330)),
    (980, 494, 1024, 956): ("Those who\nwould reach\nout a hand", BIG, (784, 158, 890, 300)),
    (628, 684, 670, 1004): ("were not there.", BIG, (723, 220, 890, 410)),
    (590, 684, 628, 932): ("But───", BIG, (503, 273, 522, 397)),
    (760, 598, 804, 960): ("they never\ngave up.", BIG, (499, 201, 521, 382)),
    (0, 394, 694, 424): ("All for the home they loved───", BIG),
    (0, 364, 694, 394): ("And above all...", BIG),
    (0, 332, 938, 364): ("because they believed one Sage would return───", BIG),
    (670, 684, 714, 1004): ("Shining in clear eyes,", BIG, (859, 147, 835, 300)),
    (714, 598, 760, 950): ("a fleeting,\nthoughtful wish───", BIG, (803, 246, 835, 365)),
    (504, 770, 548, 938): ("Some,", BIG),
    (804, 598, 850, 992): ("lit a flame\nwith their\nlast life.", BIG, (117, 200, 130, 397)),
    (894, 598, 936, 1008): ("with nothing\nbut a devoted\nheart", BIG, (138, 177, 130, 330)),
    (980, 18, 1024, 148): ("in hand.", BIG, (78, 388, 130, 410)),
    (936, 598, 980, 1020): ("And some gathered\ntheir bonds of friendship.", BIG, (499, 184, 521, 395)),
    (0, 770, 472, 814): ("Thoughts, far beyond dreams.", BIG),
    (0, 728, 518, 770): ("Truth, in the dim abyss of the heart.", BIG),
    (0, 954, 382, 996): ("The lamp was taken.", BIG),
    # What flies past "Beginnings come unbidden".
    (382, 982, 562, 1024): ("There was light", BIG),
    (388, 940, 570, 982): ("There was dark", BIG),
    (718, 556, 948, 598): ("Twilight of Thought", BIG),
    (640, 454, 1002, 494): ("The wish of Elmenas", BIG),
    (718, 514, 950, 556): ("Mighty words", BIG),
    (694, 364, 1016, 406): ("A duty to fulfill", BIG),
    (678, 168, 998, 212): ("A hidden will", BIG),
    (0, 814, 448, 858): ("Beginnings come unbidden.", BIG),
    (0, 684, 548, 728): ("The time to set out is near.", BIG),
    (640, 424, 1020, 454): ("This is the tale of the farthest place.", BIG),
    (0, 598, 672, 684): ("In a dying school, hearts joined:\nthe tale of young Sages.", BIG),
    (0, 514, 718, 598): ("Please, tell me\nof the home you love───", BIG),
}

# Frame 1's staff names, spelled as in the ending's credits (eddata.bin frame 0).
NAMES = {
    (548, 718, 716, 742): "Mizuho Azumino", (716, 718, 854, 742): "Shinya Doi", (854, 718, 976, 742): "Hiroto Saito",
    (548, 742, 700, 766): "Mizuho Azumino", (700, 742, 824, 766): "Takahiro Okuda", (824, 742, 920, 766): "Ken Yoshida",
    (548, 766, 700, 790): "Shinichiro Yoshimaru", (700, 766, 824, 790): "Youhei Kadota", (824, 766, 916, 790): "Taku Hatakeyama",
    (548, 790, 684, 814): "Akiyoshi Yamazaki", (684, 790, 806, 814): "Noritaka Numata",
}
SUBTITLE_BOX = (190, 436, 556, 476)
SUBTITLE = "GUARDIAN OF THE FARTHEST"

# Frame 13: plate -> name, its top left on screen, and where its icon ends.
PLACES = {
    (826, 888, 962, 930): ("Golden Bough Hunt", 242, 160, 31),
    (842, 974, 988, 1008): ("Campnero", 269, 209, 32),
    (190, 934, 330, 974): ("Saints' Tomb", 670, 192, 36),
    (446, 810, 652, 842): ("Daleos Undercity", 591, 246, 35),
    (0, 974, 168, 1012): ("Keneone Forest", 708, 278, 40),
    (788, 934, 948, 972): ("Stargazing Canopy", 329, 253, 38),
    (0, 888, 160, 934): ("Baleos Wasteland", 493, 289, 42),
    (356, 974, 520, 1010): ("Elberois", 685, 336, 50),
    (612, 934, 788, 972): ("Hermes Plain", 715, 378, 51),
    (698, 974, 842, 1008): ("Storm Hill", 566, 379, 55),
    (446, 844, 612, 876): ("Sera's Water-Weave", 677, 424, 40),
    (160, 888, 288, 932): ("Forest of Wandering", 190, 387, 43),
    (168, 974, 356, 1012): ("Amanjio Battlefield", 299, 440, 38),
    (768, 844, 898, 876): ("Sea God's Shrine", 494, 415, 40),
    (898, 844, 1014, 876): ("Scales Cave", 493, 484, 43),
    (330, 934, 448, 974): ("Payan", 192, 479, 38),
    (814, 810, 962, 844): ("Razenis Strait", 382, 513, 35),
    (612, 844, 768, 878): ("Old Imperial Library", 490, 570, 38),
    (0, 934, 190, 974): ("Central Schoolhouse", 359, 364, 56),
    (520, 974, 698, 1010): ("The Watchtower", 190, 288, 38),
    (288, 888, 488, 930): ("Hall of Contemplation", 437, 196, 53),
    (636, 888, 826, 928): ("Unmoving Bridge", 287, 539, 50),
    (652, 810, 814, 842): ("The Pale Fortress", 562, 450, 38),
}

# Moved left, off the ornament at the map's edge.
PLACE_SHIFT = {"Hermes Plain": -10}

# Frame 22: the rows of the card, and the two rows of words below them.
CARD_ROWS = [
    ((196, 158, 440, 196), "To my dear friend"),
    ((196, 208, 400, 246), "The home you love"),
    ((196, 258, 440, 296), "The Sages' school"),
    ((196, 306, 846, 346), "A name of dread in the Clermont Scripture"),
    ((196, 358, 470, 396), "The Crawlers"),
    ((196, 406, 740, 446), "Monstrous Chaos from the depths of the dark"),
]
CARD_WORDS_BOX = (228, 504, 830, 604)
CARD_GLOW = [(196, 150, 318, 270), (196, 270, 306, 290), (196, 290, 298, 300), (196, 300, 256, 345)]    # text over the glow, short of the rings
CARD_GLOW_AXIS = 207        # the glow and the X in it are mirror images about this x
CARD_GLOW_CY = 230          # the glow's centre
CARD_GLOW_R = 92            # and how far it reaches
CARD_GLOW_TEXT_X = 197      # none of the text starts left of this
CARD_GLOW_BARS_X = 158      # the orange bars end here
CARD_GLOW_ARCH = (276, 300) # rows where the X's inner edge is between the text's start and its mirror
CARD_RINGS = [(495.0, 444.0, 222.1, 226.6), (494.9, 444.0, 238.1, 242.9)]    # centre and radii, fitted to the orange
RING_ORANGE = (255, 148, 0)
CARD_WORDS = ["Rose  Sunlight  Smile  Blessing  Revenge", "Fresh Blood  Eradication  Despair  Curse  Return"]


def frames_of(path):
    b = Path(path).read_bytes()
    n = struct.unpack_from("<I", b, 4)[0]
    off = list(struct.unpack_from(f"<{n}I", b, 8)) + [len(b)]
    return [b[off[i]:off[i + 1]] for i in range(n)]


def decode(e):
    w, h, tw, th = struct.unpack_from("<4H", e, 4)
    p = np.frombuffer(e, "<u2", tw * th, 12).reshape(th, tw)[:h, :w].astype(np.uint32)
    five = lambda v: (v << 3 | v >> 2).astype(np.uint8)
    return np.dstack((five(p >> 10 & 31), five(p >> 5 & 31), five(p & 31), ((p >> 15) * 255).astype(np.uint8)))


def colours_of(sheet, box):
    """The fill and edge colours of the Japanese in a rectangle: the inside of its strokes, and what's around them."""
    x0, y0, x1, y1 = box
    a = sheet[y0:y1, x0:x1]
    solid = a[..., 3] > 0
    lum = a[..., :3].astype(int).sum(axis=2)
    inner = ndimage.binary_erosion(solid, iterations=2)
    core = lum[inner] if inner.any() else lum[solid]
    light = (core > 3 * 128).mean() >= 0.5
    pick = a[solid & (lum > 3 * 128)] if light else a[solid & (lum <= 3 * 128)]
    fill = tuple(int(v) for v in np.median(pick[:, :3], axis=0)) if len(pick) else (255, 255, 255)
    return fill, (8, 8, 8) if light else (255, 255, 255)


def render(text, size, fill, edge, stroke, font_path=BODY, spacing=1.24, tracking=0):
    """Text as an RGBA image, drawn large and scaled down so the edges stay smooth."""
    font = ImageFont.truetype(font_path, size * SS)
    lines = text.split("\n")
    pad = (stroke + 2) * SS
    widths = [sum(font.getlength(c) + tracking * SS for c in l) if tracking else font.getlength(l) for l in lines]
    w = int(max(widths)) + pad * 2
    ascent, descent = font.getmetrics()
    line_h = int(size * SS * spacing)
    h = line_h * (len(lines) - 1) + ascent + descent + pad * 2
    w += -w % SS
    h += -h % SS
    im = Image.new("RGBA", (w, h), (*edge, 0))
    d = ImageDraw.Draw(im)
    for layer, sw in (((*edge, 255), stroke * SS), ((*fill, 255), 0)):
        for i, line in enumerate(lines):
            x = pad + (w - pad * 2 - widths[i]) / 2
            y = pad + line_h * i
            if tracking:
                for c in line:
                    d.text((x, y), c, font=font, fill=layer, stroke_width=sw, stroke_fill=layer)
                    x += font.getlength(c) + tracking * SS
            else:
                d.text((x, y), line, font=font, fill=layer, stroke_width=sw, stroke_fill=layer)
    small = im.convert("RGBa").resize((w // SS, h // SS), Image.LANCZOS).convert("RGBA")
    a = np.array(small)
    ys, xs = np.where(a[..., 3] > 0)
    return a[ys.min():ys.max() + 1, xs.min():xs.max() + 1]


class Packer:
    """Places images on a sheet, in rows."""

    def __init__(self):
        self.x, self.y, self.row = 0, 0, 0

    def place(self, w, h):
        if self.x + w > WORDS_W:
            self.x, self.y, self.row = 0, self.y + self.row + GUTTER, 0
        if self.y + h > WORDS_H:
            raise ValueError("the sheet of English is full")
        box = (self.x, self.y, self.x + w, self.y + h)
        self.x += w + GUTTER
        self.row = max(self.row, h)
        return box


def blit(canvas, img, x, y):
    canvas[y:y + img.shape[0], x:x + img.shape[1]] = img


def erase_png(mask):
    a = np.zeros((*mask.shape, 4), np.uint8)
    a[mask] = (255, 255, 255, 255)
    return Image.fromarray(a, "RGBA")


def diff_png(game, new, where):
    """new's colours where `where` is set, as their difference from the game's texels (mod 256):
    meaningless without the game's own file, so a pack can hold it."""
    a = np.zeros((*where.shape, 4), np.uint8)
    a[..., :3] = (np.round(new[..., :3]).astype(int) - game[..., :3].astype(int)) % 256
    a[..., 3] = 255
    a[~where] = 0
    return Image.fromarray(a, "RGBA")


def mirrored_glow(jp):
    """The glow's right half, as RGBA, as far as its left half turned over reaches."""
    rgb = jp.astype(int)
    guess = rgb.copy()
    last = CARD_GLOW_TEXT_X - 1
    for x in range(CARD_GLOW_TEXT_X, 2 * CARD_GLOW_AXIS - CARD_GLOW_BARS_X):
        guess[:, x] = rgb[:, min(2 * CARD_GLOW_AXIS - x, last)]
    return guess


def clean_card(jp, tw):
    """Frame 22 without its Japanese: what's under it, as RGBA."""
    boxes = [b for b, _ in CARD_ROWS] + [CARD_WORDS_BOX]
    in_text = np.zeros(jp.shape[:2], bool)
    for x0, y0, x1, y1 in boxes:
        in_text[y0:y1, x0:x1] = True

    def texty(a):   # the text and its edges: pale or near black, and solid
        rgb = a[..., :3].astype(int)
        grey = rgb.max(axis=2) - rgb.min(axis=2) < 48
        return in_text & (a[..., 3] > 0) & (grey | (rgb[..., 0] > 200) & (rgb[..., 1] > 190) & (rgb[..., 2] > 150))

    t_jp, t_tw = texty(jp), texty(tw)
    clean = jp.copy()
    # The TW copy shows the art wherever its text isn't.
    from_tw = t_jp & ~t_tw
    clean[from_tw] = tw[from_tw]
    unknown = t_jp & t_tw

    # The TW glow isn't the same drawing, so the JP's is used on its own: its
    # left half has no text and is the mirror image of the right.
    glow = np.zeros(in_text.shape, bool)
    for x0, y0, x1, y1 in CARD_GLOW:
        glow[y0:y1, x0:x1] = True
    clean[glow] = jp[glow]
    from_tw &= ~glow
    unknown = t_jp & t_tw & ~glow
    t_jp = t_jp & ~glow

    # The two rings are plain orange: drawn back where the text crossed them.
    h, w = in_text.shape
    yy, xx = np.mgrid[0:h, 0:w]
    ring = np.zeros(in_text.shape, bool)
    near_ring = np.zeros(in_text.shape, bool)
    for cx, cy, r0, r1 in CARD_RINGS:
        r = np.hypot(xx - cx, yy - cy)
        ring |= (r >= r0) & (r <= r1)
        near_ring |= (r >= r0 - 3) & (r <= r1 + 3)
    crossed = ndimage.binary_dilation(unknown | from_tw, iterations=1) & in_text & ~glow
    on_ring = crossed & ring
    clean[on_ring] = (*RING_ORANGE, 255)

    fill = unknown & ~on_ring
    known = ~fill & ~ring
    idx = ndimage.distance_transform_edt(~known, return_distances=False, return_indices=True)
    clean[fill] = clean[idx[0][fill], idx[1][fill]]
    # A ring's edge next to what the text crossed goes back to what's beside it.
    edge = crossed & ~ring & near_ring
    clean[edge] = (0, 0, 0, 0)

    guess = mirrored_glow(jp)
    rgb = jp[..., :3].astype(int)
    greyish = (rgb.max(axis=2) - rgb.min(axis=2) < 48) & (jp[..., 3] > 0)
    yy, xx = np.mgrid[0:h, 0:w]
    from_centre = np.hypot(xx - CARD_GLOW_AXIS, yy - CARD_GLOW_CY)
    mirrors = xx < 2 * CARD_GLOW_AXIS - CARD_GLOW_BARS_X
    # Where the X's inner edge crosses the gap the mirror can't see.
    y0, y1 = CARD_GLOW_ARCH
    arch = np.zeros(glow.shape, bool)
    arch[y0:y1, CARD_GLOW_TEXT_X:2 * CARD_GLOW_AXIS - CARD_GLOW_TEXT_X] = True
    keep_out = arch | ~mirrors

    text = in_text & glow & ~keep_out & (np.abs(jp.astype(int) - guess).sum(axis=2) > 12)
    text = ndimage.binary_dilation(text, iterations=2) & glow & ~keep_out
    clean[text] = guess[text]

    # Elsewhere: past the glow's edge only the pale text needs going (its dark edge is on
    # dark), and inside it the text is the only thing that isn't blue.
    pale = greyish & (rgb[..., 0] > 60)
    inside = from_centre <= CARD_GLOW_R
    near = in_text & glow & keep_out & (greyish & (inside | arch) | pale)
    near = ndimage.binary_dilation(near, iterations=2) & glow & keep_out
    # Each colour is what's around it, weighted by distance; alpha is the nearest pixel's.
    solid = ~near & (clean[..., 3] > 0)
    weight = ndimage.gaussian_filter(solid.astype(float), 3.0)
    fill_alpha = ndimage.distance_transform_edt(near, return_distances=False, return_indices=True)
    alpha = clean[..., 3][fill_alpha[0], fill_alpha[1]]
    for c in range(3):
        top = ndimage.gaussian_filter(clean[..., c].astype(float) * solid, 3.0)
        clean[..., c][near] = np.clip((top / np.maximum(weight, 1e-3))[near].round(), 0, 255).astype(np.uint8)
    clean[..., 3][near] = alpha[near]

    # What's left of the text on its own, off the art.
    parts, count = ndimage.label(clean[..., 3] > 0, structure=np.ones((3, 3), bool))
    sizes = ndimage.sum_labels(np.ones_like(parts), parts, index=np.arange(count + 1))
    specks = (sizes[parts] <= 8) & (parts > 0) & in_text
    clean[specks] = (0, 0, 0, 0)
    return clean, in_text


def build(pack_images, game, preview=None):
    """Writes the frames and layout.txt under <pack_images>/data/op/opdata.bin."""
    out = Path(pack_images) / "data/op/opdata.bin"
    out.mkdir(parents=True, exist_ok=True)
    jp = frames_of(Path(game) / "jp/data/op/opdata.bin")
    tw_path = Path(game) / "tw/data/op/opdata.bin"
    caption, title, plates = decode(jp[CAPTION]), decode(jp[TITLE]), decode(jp[PLATES])
    layout = ["# frame x0 y0 x1 y1  new-frame x0 y0 x1 y1  dx dy"]

    # ---- frame 0: everything but KEEP goes.
    keep = np.zeros((1024, 1024), bool)
    for x0, y0, x1, y1 in KEEP:
        keep[y0:y1, x0:x1] = True
    erase_png((caption[..., 3] > 0) & ~keep).save(out / f"{CAPTION}.erase.png", optimize=True)
    sheet = np.zeros((WORDS_H, WORDS_W, 4), np.uint8)
    packer = Packer()

    for box, (text, size, *target) in CAPTIONS.items():
        # The small lines' strokes are too thin to tell by the inside.
        fill, edge = ((206, 214, 222), (8, 8, 8)) if size == SMALL else colours_of(caption, box)
        stroke = 2 if size >= 28 else 1
        img = render(text, size, fill, edge, stroke)
        h, w = img.shape[:2]
        x, y, _, _ = packer.place(w, h)
        blit(sheet, img, x, y)
        if target:
            old_x, old_y, cx, cy = target[0]
            dx, dy = cx - w // 2 - old_x, cy - h // 2 - old_y
        else:
            dx, dy = (box[2] - box[0] - w) // 2, (box[3] - box[1] - h) // 2
        layout.append(f"{CAPTION} {' '.join(map(str, box))} {WORDS} {x} {y} {x + w} {y + h} {dx} {dy}")

    # ---- frame 1: the title's subtitle, in place; the names go to frame 0.
    erased = np.zeros((1024, 1024), bool)
    x0, y0, x1, y1 = SUBTITLE_BOX
    erased[y0:y1, x0:x1] = True
    for box in NAMES:
        erased[box[1]:box[3], box[0]:box[2]] = True
    erase_png(erased & (title[..., 3] > 0)).save(out / f"{TITLE}.erase.png", optimize=True)
    over = np.zeros((1024, 1024, 4), np.uint8)
    img = render(SUBTITLE, 25, (255, 255, 255), (8, 8, 8), 2, CREDIT, tracking=2.2)
    h, w = img.shape[:2]
    blit(over, img, (x0 + x1 - w) // 2, (y0 + y1 - h) // 2)
    Image.fromarray(over, "RGBA").save(out / f"{TITLE}.png", optimize=True)
    for box, name in NAMES.items():
        img = render(name, 21, (255, 255, 255), (8, 8, 8), 1, CREDIT)
        h, w = img.shape[:2]
        x, y, _, _ = packer.place(w, h)
        blit(sheet, img, x, y)
        # The Japanese names start where the English should.
        layout.append(f"{TITLE} {' '.join(map(str, box))} {WORDS} {x} {y} {x + w} {y + h} 0 {(box[3] - box[1] - h) // 2}")

    # ---- frame 13: only the icons stay on the plates.
    the_map = decode(jp[MAP])
    plate_erase = np.zeros((1024, 1024), bool)
    for box, (name, sx, sy, icon_end) in PLACES.items():
        x0, y0, x1, y1 = box
        w, h = x1 - x0, y1 - y0
        plate = plates[y0:y1, x0:x1]
        under = the_map[sy - MAP_Y:sy - MAP_Y + h, sx:sx + w]
        # The plate is a crop of a slightly different copy of the map: only what's clearly
        # drawn on it counts, with a pixel round that for the icon's shadow.
        drawn = np.abs(plate[..., :3].astype(int) - under[..., :3]).sum(axis=2) > 36
        keep_icon = ndimage.binary_dilation(drawn, iterations=1)
        keep_icon[:, icon_end:] = False
        plate_erase[y0:y1, x0:x1] = ~keep_icon
        shift = PLACE_SHIFT.get(name, 0)
        layout.append(f"{PLATES} {' '.join(map(str, box))} {PLATES} {x0} {y0} {x0 + icon_end} {y1} {shift} 0")
        big = name in ("Hall of Contemplation", "Unmoving Bridge", "The Watchtower", "Central Schoolhouse", "The Pale Fortress")
        size = 18 if big else 16
        rows = np.flatnonzero(drawn[:, icon_end + 1:].sum(axis=1) > 2)
        middle = (rows[0] + rows[-1] + 1) // 2 if len(rows) else h // 2
        bright = plate[:, icon_end:][drawn[:, icon_end:] & (plate[:, icon_end:, :3].astype(int).sum(axis=2) > 540)]
        fill = tuple(int(v) for v in np.median(bright[:, :3], axis=0)) if len(bright) else (255, 247, 222)
        img = render(name, size, fill, (24, 16, 8), 1)
        th, tw_ = img.shape[:2]
        x, y, _, _ = packer.place(tw_, th)
        blit(sheet, img, x, y)
        layout.append(f"{PLATES} {' '.join(map(str, box))} {WORDS} {x} {y} {x + tw_} {y + th} {icon_end + 3 + shift} {middle - th // 2}")
    erase_png(plate_erase & (plates[..., 3] > 0)).save(out / f"{PLATES}.erase.png", optimize=True)

    # ---- frame 22: the text comes off the art.
    if tw_path.exists():
        card_jp, card_tw = decode(jp[CARD]), decode(frames_of(tw_path)[CARD])
        clean, in_text = clean_card(card_jp, card_tw)
        changed = (clean != card_jp).any(axis=2)
        gone = changed & (clean[..., 3] == 0)
        erase_png(gone).save(out / f"{CARD}.erase.png", optimize=True)
        # The rebuilt art goes in as a diff, so the pack holds none of the game's texels.
        fix = changed & (clean[..., 3] > 0)
        diff_png(card_jp, clean, fix).save(out / f"{CARD}.diff.png", optimize=True)
        canvas = Image.new("RGBA", (1024, 1024), (0, 0, 0, 0))
        for box, text in CARD_ROWS:
            img = render(text, 26, (206, 214, 222), (8, 8, 8), 1)
            canvas.alpha_composite(Image.fromarray(img, "RGBA"), (box[0] + 4, (box[1] + box[3] - img.shape[0]) // 2))
        y = CARD_WORDS_BOX[1] + 6
        for line in CARD_WORDS:
            img = render(line, 27, (255, 247, 222), (66, 66, 57), 1)
            canvas.alpha_composite(Image.fromarray(img, "RGBA"), ((CARD_WORDS_BOX[0] + CARD_WORDS_BOX[2] - img.shape[1]) // 2, y))
            y += 46
        canvas.save(out / f"{CARD}.png", optimize=True)

    Image.fromarray(sheet, "RGBA").save(out / f"{WORDS}.png", optimize=True)
    (out / "layout.txt").write_text("\n".join(layout) + "\n", encoding="utf8")
    if preview:
        Image.fromarray(sheet, "RGBA").save(Path(preview) / "op_0.png")
