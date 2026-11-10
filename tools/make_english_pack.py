#!/usr/bin/env python3
"""Builds languages/english.zip from languages/en/, drawing its images.

Reads your imported files (gamedata/) to match each label's surroundings, but the
images hold no game art, so the zip can be shared. For each label in LABELS:

- On a plate, a patch blended from the colours just outside the label covers the
  Japanese. It follows the frame's alpha, so transparent texels stay transparent.
- On a transparent sheet, <frame>.erase.png clears the Japanese instead of covering it.

The English takes the colour of the text it replaces, so dim button states stay dim.

Needs Pillow, NumPy and SciPy. Pass --preview DIR to write the painted frames there.
"""
import argparse
import random
import re
import struct
import zipfile
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont
from scipy import ndimage

import english_opening

REPO = Path(__file__).resolve().parent.parent
ROOT = REPO / "languages"
PACK = ROOT / "en"
GAME = REPO / "gamedata/jp"
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSerifCondensed-Bold.ttf"
TITLE_FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf"

PANEL = (0x3a, 0x42, 0x31)     # the option panel's average colour
TEXT = (0xf0, 0xf0, 0xe4)
SHADOW = (0x10, 0x12, 0x0e)


def rows(x0, x1, y0, step, h, texts):
    """Labels in a column: one row per step, None where there's nothing to change."""
    return [((x0, y0 + i * step, x1, y0 + i * step + h), t) for i, t in enumerate(texts) if t]


JOBS = ["Warrior", "Valkyrie", "Swordsman", "Fencer", "Hunter", "Scout", "Priest", "Sister", "Mage", "Witch"]
WHITE = ["White " + j for j in ("Warrior", "Warrior", "Swordsman", "Swordsman", "Hunter", "Hunter", "Priest", "Priest", "Mage", "Mage")]
BLACK = ["Black " + j for j in ("Warrior", "Warrior", "Swordsman", "Swordsman", "Hunter", "Hunter", "Priest", "Priest", "Mage", "Mage")]
CONDITIONS = ["Poison", "Restraint", "Mask", "Frostbite", None, "Toxic", "Curse",
              "Atk Down", "M.Atk Down", "Slow", "Def Down", "M.Def Down"]
TEAM = [f"Team {i}" for i in range(1, 6)]
# The credits' spellings where they give one, the story's Japanese read plainly for the rest.
STUDENTS = ["Callot", "Syalon", "Palmra", "Sesiris", "Fanna", "Alsea", "Rossaria", "Quest", "Illion", "Oskir", "Sirius",
            "Ney", "Yuri", "Axel", "Chris", "Grave", "Bardi", "Flicka", "Cadmus", "Pyros", "Hudy", "Ele", "Sheila",
            "Ferdona", "Iona", "Kiefer", "Nafotka", "Hazban", "Marvela", "Muller", "Lilith", "Yelfan", "Shelfa", "Jacobin"]
OTHERS = ["Denehere", "Barbara", "Zilmea", "Rubigore", "Braki", "Neph", "Baubes", "Barbo", "Reyvan", "Papesh", "Fiorio",
          "Schneidra", "Navilos", "Nazrek", "Cheryl", "Robery", "Fald", "Batra"]
FULL_NAMES = ["Callot Winfy", "Syalon Flayspid", "Palmra Ruverdracloa", "Sesiris Finott", "Fanna Hyverios", "Alsea Bellthird",
              "Rossaria Ordinar", "Quest Shnonsaw", "Illion Vinnat", "Oskir Vainheart", "Sirius Musie", "Ney Prawdis",
              "Yuri Winkvalley", "Axel Enforge", "Chris Parazin", "Grave Temperancehill", "Bardi Temperancehill",
              "Flicka Laspalmeisen", "Cadmus Praishua", "Pyros Fictburn", "Hudy Sutherland", "Ele Moonselis", "Sheila Efin",
              "Ferdona Pylon", "Iona Harel", "Kiefer Starbreeze", "Nafotka Corvette", "Hazban Schooning", "Marvela Tetra",
              "Muller Aspenbirge", "Lilith Seirenengram", "Yelfan Shuvalus", "Shelfa Lucifuge", "Jacobin Abas",
              "Denehere Ruverdracloa"]
LECTURES = [rows(9, 107, 4, 22, 14, t) for t in (
    ["Warrior Basics", "Sword Basics", "Hunter Basics", "Priest Basics", "Mage Basics", "Service", "Magic Basics",
     "Call of the Wind", "Mage's Soul", "Head of Wings", "What's a Lecture?", "What's a Quest?", "Brush in Hand", "Fallen Monk",
     "Melody of Light", "Hidden Strains", "Head of Keys", "Marquetry", "The Public Face", "The Hidden Face", "Sword Dance",
     "Hunter's Soul", "Chapel Aide"],)]
LECTURES += [rows(125, 223, 4, 22, 14, t) for t in (
    ["Words of Healing", "Dark Verse", "For Everyone", "Rite of Silence", "Head of Scales", "Wisdom's Font", "Waterside Oath",
     "Earth's Embrace", None, "Wanderer", "Morals", "Holy Chant", "Life on a Blade", "Head of Flame", "Flame Guardian",
     "Supreme Flame", None, "Chapel Head", "Contemplation", "Priest's Soul", None, "Spire Chief", "Spirit of Words"],)]
LECTURES += [rows(241, 339, 4, 22, 14, [None, "Ridge Training", "In the Dark", "Camp Training", "In the Forest", "Out to Sea"])]
LECTURES = sum(LECTURES, [])

LABELS = {
    "data/menu/handata.bin": {
        0: [# To Title, Office, Roster and Wait: han_bar()
            ((8, 48, 92, 68), "This Week"), ((240, 46, 460, 68), "Advance Schedule"),
            ((580, 46, 645, 65), "Quest"), ((932, 73, 1016, 94), "Quest Week"), ((938, 107, 974, 127), "Limit"),
            ((938, 136, 974, 157), "Free"), ((580, 441, 640, 466), "Lecture"),
            ((596, 173, 652, 191), "Summary"), ((712, 173, 764, 191), "Goal"), ((826, 173, 878, 191), "Terms"), ((941, 173, 993, 191), "Force"),
            ((940, 473, 976, 491), "Basic"), ((940, 505, 976, 523), "Mid"), ((940, 536, 976, 554), "Adv."),
            ((16, 600, 96, 617), "Level"), ((16, 623, 96, 640), "Class"), ((12, 646, 100, 663), "Total Power")]
           + [((150, 84 + 97 * i, 200, 107 + 97 * i), t) for i, t in enumerate(TEAM)]
           + [((x, 48, x + 36, 65), t) for x, t in zip((689, 738, 787, 836, 885), TEAM)]
           # the buttons' other states, below the screen's layout
           + rows(26, 90, 773, 29.7, 19, ["Summary", "Goal", "Terms", "Force", "Summary", "Goal", "Terms", "Force"])
           + rows(170, 224, 776, 31, 19, ["Basic", "Mid", "Adv."]) + rows(170, 224, 871, 31.5, 19, ["Basic", "Mid", "Adv."])
           + rows(234, 326, 772, 24, 18, ["Level", "Class", "Total Power"]) + rows(234, 326, 848, 24, 18, ["Level", "Class", "Total Power"])
           + rows(448, 500, 775, 33, 19, ["Limit", "Free", "Limit", "Free"])
           + rows(506, 549, 771, 26.5, 18, TEAM) + rows(556, 599, 771, 26.5, 18, TEAM) + rows(606, 649, 771, 26.5, 18, TEAM)
           + [((592, 902, 682, 922), "To Title"), ((144, 963, 230, 982), "Quest Week"), ((150, 993, 410, 1013), "Advance Schedule")],
        1: [((218, 8, 240, 26), "No."), ((258, 8, 332, 26), "Effect"), ((370, 8, 426, 25), "History"),
            ((356, 148, 402, 164), "Quest"), ((356, 166, 402, 182), "Lecture")]
           + rows(3, 80, 190, 24, 21, JOBS) + rows(85, 162, 190, 24, 21, WHITE) + rows(167, 244, 190, 24, 21, BLACK),
        2: rows(304, 372, 0, 16, 16, STUDENTS) + [((153, 387, 226, 403), "Jacobin")],
        3: rows(228, 292, 0, 16, 16, OTHERS) + [((6, 100, 70, 113), "Barbara"), ((6, 244, 70, 258), "Rubigore"), ((82, 460, 146, 474), "Nazrek")],
        # 4: the quest plates, quest_plates()
        5: LECTURES, 6: LECTURES, 7: LECTURES,
        11: [((60, 56, 102, 76), "Yes"), ((160, 56, 222, 76), "No"), ((60, 95, 102, 114), "Yes"), ((160, 95, 222, 114), "No")],
    },
    "data/menu/kojindata.bin": {
        0: [((41, 94, 92, 118), "Engage\nTime"), ((50, 180, 90, 198), "Hit"), ((50, 203, 90, 221), "Power"), ((42, 237, 100, 252), "Extra"),
            ((50, 296, 90, 314), "Evade"), ((50, 319, 90, 337), "Armor"), ((50, 352, 90, 370), "Barrier")],
        1: [((36, 21, 106, 39), "Capacity"), ((510, 31, 586, 50), "Owned"), ((680, 31, 752, 50), "Learn"),
            ((638, 67, 672, 84), "Class"), ((638, 86, 672, 103), "Cost"), ((638, 105, 672, 122), "Left"),
            ((470, 132, 502, 149), "Prep"), ((470, 151, 502, 168), "Recov"), ((638, 132, 672, 149), "Aim"), ((638, 151, 672, 168), "Target")]
           + [((x - 17, 320, x + 17, 337), t) for x, t in zip((44, 80, 116, 152, 188, 224, 260, 295, 332, 368, 404, 440),
              ("Heavy", "Swift", "Front", "Back", "Holy", "Dark", "Void", "Earth", "Wind", "Water", "Fire", "Deep"))],
        2: [((36, 21, 106, 39), "Capacity"), ((510, 31, 586, 50), "Owned"),
            ((642, 67, 674, 84), "Type"), ((642, 86, 674, 103), "Stack"), ((642, 105, 674, 122), "Value")]
           + rows(640, 698, 133, 18.7, 17, ["Warrior", "Swordsman", "Hunter", "Priest", "Mage"])
           + [((x - w, 320, x + w, 337), t) for x, w, t in ((42, 17, "2-Hand"), (78, 17, "1-Hand"), (114, 17, "Shield"), (150, 17, "Bow"),
              (186, 17, "X-Bow"), (222, 17, "Staff"), (258, 17, "Armor"), (294, 17, "Leather"), (349, 30, "Accessory"), (421, 30, "Accessory"))],
        3: [((680, 31, 754, 49), "OK"), ((506, 141, 560, 158), "Hit"), ((506, 161, 560, 178), "Power"), ((506, 202, 560, 219), "Evade"),
            ((506, 220, 560, 237), "Armor"), ((506, 240, 560, 257), "Barrier"), ((664, 63, 748, 80), "HP Regen"), ((664, 81, 748, 98), "MP Regen"),
            ((664, 101, 748, 118), "Sight"), ((664, 119, 748, 136), "Move"), ((664, 141, 748, 158), "Range"), ((664, 160, 748, 177), "Area"),
            ((664, 179, 748, 196), "Recovery"), ((664, 220, 748, 237), "Body"), ((664, 240, 748, 257), "Will")],
        4: [((20, 32, 78, 52), "Stat"), ((102, 32, 162, 52), "Value"), ((172, 32, 206, 52), "Rank"), ((292, 32, 344, 52), "Gain"),
            ((360, 32, 434, 52), "Need EXP"), ((452, 32, 492, 52), "Boost"), ((608, 66, 694, 84), "Suggested"),
            ((608, 202, 672, 220), "Class EXP"), ((608, 236, 672, 254), "Cost EXP"), ((608, 270, 672, 288), "Left EXP"),
            ((612, 306, 668, 328), "OK"), ((702, 306, 776, 328), "Cancel")],
        7: rows(14, 150, 3, 28, 24, JOBS + WHITE + BLACK)    # the buttons at x 205..320: koji_buttons()
           + rows(360, 447, 5, 24, 16, JOBS + WHITE + BLACK)
           + rows(448, 524, 101, 24, 16, CONDITIONS)
           + rows(724, 790, 48, 16, 16, CONDITIONS)
           + rows(362, 444, 720, 14, 14, ["Two-Hand Weapon", "One-Hand Weapon", "Shield", "Bow", "Crossbow", "Staff", "Metal Armor",
                                          "Leather Armor", "Accessory", "Consumable"])
           + rows(254, 293, 810, 16.2, 16, ["Power", "Speed", "Skill", "Virtue", "Mind", "Endure", "Spirit", "Total"])
           + rows(295, 357, 810, 16.2, 16, ["Warrior", "Swordsman", "Archer", "Priest", "Mage", "Melee", "Skill User"])
           + [((552, 562, 608, 582), "OK"), ((530, 600, 626, 620), "Cancel"), ((820, 455, 906, 475), "OK"),
              ((42, 848, 114, 866), "Owned"), ((42, 887, 114, 905), "Learn"), ((42, 924, 114, 942), "Learned"), ((96, 955, 134, 971), "Sure")]
           + rows(722, 800, 411, 14, 14, ["Absolute", "Adjust", "Hit", "Power", "Effect", "Evade", "Armor", "Barrier", "HP Regen", "MP Regen",
                                          "Sight", "Move", "Range", "Area", "Recovery", "Body", "Will"]),
        # The longest Japanese names start left of the plate's black.
        **{12 + i: ([((18, 363, 34, 378), "", (88, 82, 58))] if i in (2, 34) else []) + [((28, 362, 259, 379), name, (6, 6, 8))]
           for i, name in enumerate(FULL_NAMES)},
    },
}


def frames_of(path):
    b = path.read_bytes()
    n = struct.unpack_from("<I", b, 4)[0]
    off = list(struct.unpack_from(f"<{n}I", b, 8)) + [len(b)]
    return [b[off[i]:off[i + 1]] for i in range(n)]


def decode(e):
    """A DX mode 2 entry as an RGBA array (A1R5G5B5 texels)."""
    w, h, tw, th = struct.unpack_from("<4H", e, 4)
    p = np.frombuffer(e, "<u2", tw * th, 12).reshape(th, tw)[:h, :w].astype(np.uint32)
    five = lambda v: (v << 3 | v >> 2).astype(np.uint8)
    return np.dstack((five(p >> 10 & 31), five(p >> 5 & 31), five(p & 31), ((p >> 15) * 255).astype(np.uint8)))


def blend_background(frame, box):
    """A smooth fill for box from the texels just outside it: no detail of them survives."""
    x0, y0, x1, y1 = box
    H, W = frame.shape[:2]
    ring = lambda ys, xs: ndimage.gaussian_filter1d(frame[ys, xs, :3].astype(float), 4, axis=0, mode="nearest")
    top = ring(np.full(x1 - x0, max(y0 - 1, 0)), np.arange(x0, x1))
    bottom = ring(np.full(x1 - x0, min(y1, H - 1)), np.arange(x0, x1))
    left = ring(np.arange(y0, y1), np.full(y1 - y0, max(x0 - 1, 0)))
    right = ring(np.arange(y0, y1), np.full(y1 - y0, min(x1, W - 1)))
    v = np.linspace(0, 1, y1 - y0)[:, None, None]
    u = np.linspace(0, 1, x1 - x0)[None, :, None]
    fill = (top[None] * (1 - v) + bottom[None] * v + left[:, None] * (1 - u) + right[:, None] * u) / 2
    return ndimage.gaussian_filter(fill, (2, 2, 0))


def text_colour(frame, box, background):
    """The Japanese's own colour: the texels of the box that differ most from the fill."""
    x0, y0, x1, y1 = box
    area = frame[y0:y1, x0:x1]
    opaque = area[..., 3] > 0
    if not opaque.any():
        return (255, 255, 255)
    diff = np.abs(area[..., :3].astype(float) - background).sum(axis=2) * opaque
    limit = np.percentile(diff[opaque], 97)
    picks = area[(diff >= limit) & opaque, :3]
    return tuple(int(v) for v in np.median(picks, axis=0))


SS = 4         # text is drawn this much larger, then scaled down


def fitted_size(text, width, height, font_path=FONT):
    """The largest size that fits, squeezed up to a quarter."""
    for size in range(min(height, 18), 6, -1):
        l, t, r, b = ImageFont.truetype(font_path, size * SS).getbbox(text)
        if (b - t) / SS <= height and (r - l) / SS * 0.75 <= width - 2:
            return size
    return 7


def render_line(text, width, height, colour, size, font_path=FONT, steady=False):
    """The text centred in width x height, with a dark halo. steady: centred on the font's ascent and
    descent rather than this text's ink, so a list of names shares one baseline."""
    font = ImageFont.truetype(font_path, size * SS)
    l, t, r, b = font.getbbox(text)
    if steady:
        probe = text + ("Agjpqy" if text.isascii() else "")
        t, b = min(t, font.getbbox(probe)[1]), max(b, font.getbbox(probe)[3])
    tw, th = r - l, b - t
    canvas = Image.new("L", (tw + 8, th + 8), 0)
    ImageDraw.Draw(canvas).text((4 - l, 4 - t), text, font=font, fill=255)
    w = min(canvas.width // SS, width - 2)
    h = max(canvas.height // SS, 1)
    mask = canvas.resize((w, h), Image.LANCZOS)
    glyph = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    edge = Image.new("L", (width, height), 0)
    at = ((width - w) // 2, (height - h) // 2)
    edge.paste(mask, at)
    halo = edge.filter(ImageFilter.MaxFilter(3)).point(lambda v: v * 3 // 4)
    glyph.paste((8, 8, 10, 255), (0, 0), halo)
    glyph.paste(colour + (255,), at, mask)
    return glyph


def clamp(frame, box):
    H, W = frame.shape[:2]
    x0, y0, x1, y1 = (int(round(v)) for v in box)
    return max(x0, 0), max(y0, 0), min(x1, W), min(y1, H)


def even_sizes(frame, labels):
    """One size for all the frame's labels in boxes of the same size, so a column reads evenly."""
    sizes = {}
    for box, text, *_ in labels:
        x0, y0, x1, y1 = clamp(frame, box)
        lines = text.split("\n")
        key = x1 - x0, y1 - y0, len(lines)
        h = (y1 - y0) // len(lines)
        sizes[key] = min([sizes.get(key, 99)] + [fitted_size(line, x1 - x0, h) for line in lines])
    return sizes


# The banks whose dark-on-light labels are drawn light (draw_label).
LIGHT_LETTERS = {"data/menu/handata.bin"}


def draw_label(over, erase, frame, box, text, sizes, fixed=None, light=False):
    """fixed: the plate's colour, where what's outside the label doesn't match what's behind it.
    light: dark letters on a light plate become light ones, as on the character screen (dimmer
    looks stay as much dimmer)."""
    x0, y0, x1, y1 = box = clamp(frame, box)
    H, W = frame.shape[:2]
    # Opaque all round means a plate; otherwise it's text on a transparent sheet.
    ring = np.concatenate((frame[max(y0 - 1, 0), x0:x1, 3], frame[min(y1, H - 1), x0:x1, 3],
                           frame[y0:y1, max(x0 - 1, 0), 3], frame[y0:y1, min(x1, W - 1), 3]))
    plate = (ring > 0).mean() > 0.5
    fill = np.full((y1 - y0, x1 - x0, 3), fixed, float) if fixed else blend_background(frame, box)
    colour = text_colour(frame, box, fill)
    if light and sum(colour) < fill.mean() * 3:
        colour = tuple(int(np.clip(v, 0, 255)) for v in np.array((244, 240, 228)) - sum(colour) / 3)
    if plate:
        rng = np.random.default_rng(x0 * 4099 + y0)
        rgb = np.clip(fill + rng.normal(0, 1.5, fill.shape[:2])[..., None], 0, 255).astype(np.uint8)
        # Soft at the edges, and only where the frame has texels.
        d = np.minimum.outer(np.minimum(np.arange(y1 - y0), np.arange(y1 - y0)[::-1]), np.minimum(np.arange(x1 - x0), np.arange(x1 - x0)[::-1]))
        # Closed first: the holes keyed out along the Japanese's strokes go with it.
        solid = ndimage.binary_closing(np.pad(frame[y0:y1, x0:x1, 3] > 0, 3, constant_values=True), iterations=3)[3:-3, 3:-3]
        alpha = np.clip(110 + d * 70, 0, 255) * solid
        patch = Image.fromarray(np.dstack((rgb, alpha.astype(np.uint8))), "RGBA")
        over.alpha_composite(patch, (x0, y0))
    else:
        ImageDraw.Draw(erase).rectangle((x0, y0, x1 - 1, y1 - 1), fill=(0, 0, 0, 255))
    lines = text.split("\n")
    h = (y1 - y0) // len(lines)
    for i, line in enumerate(lines):
        if line:
            over.alpha_composite(render_line(line, x1 - x0, h, colour, sizes[x1 - x0, y1 - y0, len(lines)]), (x0, y0 + i * h))


def game_images(preview):
    for rel, frames in LABELS.items():
        entries = frames_of(GAME / rel)
        out_dir = PACK / "images" / rel
        out_dir.mkdir(parents=True, exist_ok=True)
        for f, labels in frames.items():
            frame = decode(entries[f])
            size = frame.shape[1], frame.shape[0]
            over = Image.new("RGBA", size, (0, 0, 0, 0))
            erase = Image.new("RGBA", size, (0, 0, 0, 0))
            sizes = even_sizes(frame, labels)
            for box, text, *fixed in labels:
                if (rel, f) == HAN_BUTTONS_AT and text in HAN_BUTTONS:
                    continue                        # han_buttons()
                draw_label(over, erase, frame, box, text, sizes, *fixed, light=rel in LIGHT_LETTERS)
            over.save(out_dir / f"{f}.png", optimize=True)
            erased = np.array(erase)[..., 3] > 0
            if erased.any():
                erase.save(out_dir / f"{f}.erase.png", optimize=True)
            else:
                (out_dir / f"{f}.erase.png").unlink(missing_ok=True)
            if preview:
                shown = frame.copy()
                shown[erased] = 0
                painted = Image.new("RGBA", size, (40, 40, 60, 255))
                painted.alpha_composite(Image.fromarray(shown, "RGBA"))
                painted.alpha_composite(over)
                painted.convert("RGB").save(Path(preview) / f"{Path(rel).stem}_{f}.png")


def patch(img, box, rng):
    """Covers box with the panel colour, mottled like it, soft at the edges."""
    x0, y0, x1, y1 = box
    edge = 2
    for y in range(y0, y1):
        for x in range(x0, x1):
            d = min(x - x0, x1 - 1 - x, y - y0, y1 - 1 - y)
            alpha = 255 if d >= edge else 110 + 70 * d
            n = rng.randint(-5, 5)
            img.putpixel((x, y), (PANEL[0] + n, PANEL[1] + n, PANEL[2] + n, alpha))


def label(img, right, centre_y, text, size):
    """Text ending at x = right, with the original's dark edge."""
    font = ImageFont.truetype(TITLE_FONT, size)
    draw = ImageDraw.Draw(img)
    left, top, r, bottom = draw.textbbox((0, 0), text, font=font)
    x = right - (r - left) - left
    y = centre_y - (bottom - top) // 2 - top
    draw.text((x + 1, y + 1), text, font=font, fill=SHADOW + (255,))
    draw.text((x, y), text, font=font, fill=TEXT + (255,))


def option_panel():
    """data/tactics/option.bin frame 1: the volume panel and its parts."""
    img = Image.new("RGBA", (400, 222), (0, 0, 0, 0))
    rng = random.Random(1)
    patch(img, (8, 8, 112, 36), rng)
    label(img, 104, 22, "Volume", 18)
    for y0, y1, text in ((40, 62, "Music"), (70, 92, "Effects"), (100, 122, "Voice")):
        patch(img, (40, y0, 111, y1), rng)
        label(img, 107, (y0 + y1) // 2, text, 14)
    out = PACK / "images/data/tactics/option.bin/1.png"
    out.parent.mkdir(parents=True, exist_ok=True)
    img.save(out)


MENU_FONT = "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf"
QUIT_PANEL = (58, 64, 52)    # the flat colour behind the question
QUIT_TEXT = [(21, 13, "Continue your adventure?"), (21, 31, "Or return to the Title screen?")]    # left, cap top
QUIT_BUTTONS = [(46, 111, "Continue"), (156, 221, "Title")]    # what each face's label may span
TITLE_QUIT_TEXT = [(21, 13, "This will quit CROSS HERMIT."), (21, 31, "Are you sure?")]
TITLE_QUIT_BUTTONS = [(46, 111, "Yes"), (156, 221, "No")]
QUIT_FACES = [(57, 74), (97, 113)]    # the faces' rows: the menu, then its pressed look


def decode_bmp(e):
    """An 8-bit BMP entry as RGBA, keyed as the game keys it (ch::decodeBmp8)."""
    off, = struct.unpack_from("<I", e, 10)
    w, h = struct.unpack_from("<ii", e, 18)
    n = min(struct.unpack_from("<I", e, 46)[0] or (off - 0x36) >> 2, 256)
    pal = [tuple(e[0x36 + i * 4:0x39 + i * 4]) for i in range(n)] + [(0, 0, 0)] * (256 - n)
    pal = [pal[0] if i and p == (0, 0, 0) else p for i, p in enumerate(pal)]
    lut = np.zeros((256, 4), np.uint8)
    for i in range(1, 256):
        if pal[i] != pal[0]:
            lut[i] = (pal[i][2], pal[i][1], pal[i][0], 255)
    stride = (w + 3) & ~3
    return lut[np.frombuffer(e, np.uint8, stride * h, off).reshape(h, stride)[::-1, :w]]


def ink(text, size, tracking=0.0):
    """The text's coverage, cropped to its ink, and the cap's top within it."""
    font = ImageFont.truetype(MENU_FONT, size * SS)
    width = int(font.getlength(text) + tracking * SS * len(text)) + 8 * SS
    canvas = Image.new("L", (width + -width % SS, 2 * size * SS), 0)
    d = ImageDraw.Draw(canvas)
    x = 4 * SS
    for c in text:
        d.text((x, size * SS // 2), c, font=font, fill=255)
        x += font.getlength(c) + tracking * SS
    a = np.array(canvas.resize((canvas.width // SS, canvas.height // SS), Image.LANCZOS)).astype(float) / 255
    ys, xs = np.where(a > 0.02)
    cap_top = (size * SS // 2 + font.getbbox("T")[1]) / SS
    return a[ys.min():ys.max() + 1, xs.min():xs.max() + 1], cap_top - ys.min()


def put(over, a, x, y, colour):
    """Lays coverage a on over at (x, y): colour is one RGB, one per row or one per texel."""
    h, w = a.shape
    colour = np.asarray(colour, float)
    rgb = np.broadcast_to(colour.reshape(-1, 1, 3) if colour.ndim == 2 else colour, (h, w, 3))
    h, w = min(h, over.shape[0] - y), min(w, over.shape[1] - x)     # clipped at over's right and bottom
    a, rgb = a[:h, :w], rgb[:h, :w]
    region = over[y:y + h, x:x + w]
    under = region[..., 3] / 255 * (1 - a)
    alpha = a + under
    region[..., :3] = (rgb * a[..., None] + region[..., :3] * under[..., None]) / np.maximum(alpha, 1e-6)[..., None]
    region[..., 3] = alpha * 255


class Sheet:
    """One frame's changes: the art rebuilt under the Japanese (base, where fixed) and the English (over).

    The rebuilt art is saved as <frame>.diff.png, its difference from the game's texels, so the pack
    holds none of them; <frame>.png holds only the English."""

    def __init__(self, frame):
        self.frame = frame
        self.base = frame.copy()
        self.fixed = np.zeros(frame.shape[:2], bool)
        self.over = np.zeros(frame.shape, float)

    def rebuild(self, where, rgb):
        self.base[where + (slice(0, 3),)] = rgb
        self.fixed[where] = True

    def save(self, rel, index, preview=None, name=None):
        out = PACK / "images" / rel
        out.mkdir(parents=True, exist_ok=True)
        english_opening.diff_png(self.frame, self.base, self.fixed).save(out / f"{index}.diff.png", optimize=True)
        Image.fromarray(self.over.round().astype(np.uint8), "RGBA").save(out / f"{index}.png", optimize=True)
        if preview:
            shown = Image.new("RGBA", self.frame.shape[1::-1], (40, 40, 60, 255))
            base = self.base.copy()
            base[self.fixed, 3] = 255
            shown.alpha_composite(Image.fromarray(base.round().astype(np.uint8), "RGBA"))
            shown.alpha_composite(Image.fromarray(self.over.round().astype(np.uint8), "RGBA"))
            shown.convert("RGB").save(Path(preview) / f"{name}.png")


def quit_menu(preview):
    """data/tactics/common.bin frame 7: the battle menu's "continue or go to the title?" and its buttons."""
    quit_panel("data/tactics/common.bin", 7, QUIT_TEXT, QUIT_BUTTONS, preview, "common_7")


def title_quit(preview):
    """data/tactics/title.bin frame 6: the title's Exit, "quit CROSS HERMIT?", the same panel as the battle's."""
    quit_panel("data/tactics/title.bin", 6, TITLE_QUIT_TEXT, TITLE_QUIT_BUTTONS, preview, "title_6")


def quit_panel(rel, index, lines, buttons, preview, name):
    """A question on a flat colour over two buttons, in the menu's look and its pressed look.

    The question is on a flat colour, so only the Japanese and its shadow are covered. The button
    faces are smooth, so each label is covered row by row from the face either side of it."""
    frame = decode_bmp(frames_of(GAME / rel)[index]).astype(float)
    sheet = Sheet(frame)
    over = sheet.over
    # The question: the Japanese is anything off the panel colour; its shadow and halo are inside a dilation.
    jp = np.abs(frame[..., :3] - QUIT_PANEL).max(axis=2) > 8
    jp[:9], jp[48:], jp[:, :14], jp[:, 252:] = False, False, False, False
    cover = ndimage.binary_dilation(jp, iterations=1)
    sheet.rebuild((cover,), QUIT_PANEL)
    for x, cap, text in lines:
        a, top = ink(text, 16)
        y = round(cap - top)
        halo = ndimage.maximum_filter(np.pad(a, 1), 3) * 0.45
        put(over, halo, x - 1, y - 1, (3, 3, 3))
        put(over, a, x + 1, y + 1, (3, 3, 3))
        put(over, a, x, y, np.linspace((255, 255, 255), (196, 198, 194), a.shape[0]))
    # The buttons, in both looks.
    for y0, y1 in QUIT_FACES:
        for x0, x1, text in buttons:
            face_label(sheet, (x0, y0, x1, y1), text, 16)
    sheet.save(rel, index, preview, name)


BAR_LABELS = [((266, 4, 321, 26), "Time Left"), ((477, 4, 547, 26), "Elapsed")]    # common.bin frame 0's labels


def cover_along(sheet, box, avoid, span, f=4):
    """Covers box with the stretch of the same rows, within span and clear of the boxes in avoid,
    whose edges match it best, blended in over f texels at the sides."""
    frame = sheet.frame
    x0, y0, x1, y1 = box
    w = x1 - x0
    target = frame[y0:y1, x0 - f:x1 + f, :3]
    edges = np.zeros(target.shape[:2], bool)
    edges[:, :f], edges[:, -f:] = True, True
    best = None
    for sx in range(span[0] + f, span[1] - w - f):
        if any(sx - f < bx1 + 8 and sx + w + f > bx0 - 8 for bx0, _, bx1, _ in avoid):
            continue
        cost = ((frame[y0:y1, sx - f:sx + w + f, :3] - target)[edges] ** 2).mean()
        if best is None or cost < best[0]:
            best = (cost, sx)
    source = frame[y0:y1, best[1] - f:best[1] + w + f, :3]
    blend = np.clip(np.minimum(np.arange(w + 2 * f), np.arange(w + 2 * f)[::-1]) / f, 0, 1)[None, :, None]
    sheet.rebuild((slice(y0, y1), slice(x0 - f, x1 + f)), source * blend + target * (1 - blend))


def top_bar(preview):
    """data/tactics/common.bin frame 0: the battle's top bar, "time left" and "time elapsed".

    The labels are on stone that is alike all along the bar, so each is covered with the stretch
    of the same rows whose edges match it best, blended in at the sides."""
    rel = "data/tactics/common.bin"
    frame = decode_bmp(frames_of(GAME / rel)[0]).astype(float)
    sheet = Sheet(frame)
    over = sheet.over
    labels = [box for box, _ in BAR_LABELS]
    for (x0, y0, x1, y1), text in BAR_LABELS:
        cover_along(sheet, (x0, y0, x1, y1), labels, (100, frame.shape[1] - 100))
        # The English: the Japanese's colour, its dark edge, right-aligned where it ended.
        jp = frame[y0:y1, x0:x1, :3]
        lit = (jp.mean(axis=2) > 200) | ((np.ptp(jp, axis=2) > 60) & (jp[..., 2] > 150))
        colour = np.percentile(jp[lit], 90, axis=0)
        a, top = ink(text, 14)
        x, y = x1 - 4 - a.shape[1], round(y0 + 5 - top)
        put(over, ndimage.maximum_filter(np.pad(a, 1), 3), x - 1, y - 1, (12, 10, 8))
        put(over, a, x, y, colour)
    sheet.save(rel, 0, preview, "common_0")


DATE_FONT = "/usr/share/fonts/truetype/liberation/LiberationSerif-BoldItalic.ttf"
MONTHS = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"]
DATE_SHEET = 3              # workroom.bin's new frame, for the date's English
DATE_MONTH_END = 150        # screen x where every month name ends, so "Week" keeps one gap
DATE_BASELINE = (93, 66)    # screen baselines of the month and of the week's digit and words
DATE_WEEK_SHIFT = 46        # the week's digit moves right, to make room for "Week"


def outlined(text, size, stroke, font=DATE_FONT):
    """White text with a black edge, as the date's digits: the image and its baseline row."""
    f = ImageFont.truetype(font, size * SS)
    pad = (stroke + 4) * SS
    l, t, r, b = f.getbbox(text, anchor="ls", stroke_width=stroke * SS)
    w, h = r - l + 2 * pad, b - t + 2 * pad
    canvas = Image.new("RGBA", (w + -w % SS, h + -h % SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(canvas)
    d.text((pad - l, pad - t), text, font=f, anchor="ls", fill=(255, 255, 255, 255), stroke_width=stroke * SS, stroke_fill=(0, 0, 0, 255))
    small = np.array(canvas.convert("RGBa").resize((canvas.width // SS, canvas.height // SS), Image.LANCZOS).convert("RGBA"))
    ys, xs = np.where(small[..., 3] > 0)
    return small[ys.min():ys.max() + 1, xs.min():xs.max() + 1], round((pad - t) / SS) - ys.min()


def week_screen():
    """data/workroom/workroom.bin: the week screen's date ("4月 第1週" becomes "Apr Week 1"),
    through layout.txt, and the buttons of the week and map screens (frames 0 and 1)."""
    rel = "data/workroom/workroom.bin"
    out = PACK / "images" / rel
    out.mkdir(parents=True, exist_ok=True)
    sheet = np.zeros((256, 1024, 4), np.uint8)
    layout = ["# The date: month names and \"Week\" on frame 3; the week's digits moved right; 月 and 週 draw nothing."]
    x = y = 0
    month_y, week_y = DATE_BASELINE

    def place(img):
        nonlocal x, y
        if x + img.shape[1] > 1020:
            x, y = 0, y + 80
        box = (x, y, x + img.shape[1], y + img.shape[0])
        sheet[y:y + img.shape[0], x:x + img.shape[1]] = img
        x += img.shape[1] + 2
        return box

    for k, name in enumerate(MONTHS):
        img, base = outlined(name, 62, 3)
        x0, y0, x1, y1 = place(img)
        cx, cy = 294 + k // 6 * 108, k % 6 * 84          # the digit's cell; drawn at (21, 28)
        layout.append(f"0 {cx} {cy} {cx + 108} {cy + 84} {DATE_SHEET} {x0} {y0} {x1} {y1} "
                      f"{DATE_MONTH_END - (x1 - x0) - 21} {month_y - base - 28}")
    img, base = outlined("Week", 27, 2)
    x0, y0, x1, y1 = place(img)
    layout.append(f"0 164 26 190 52 {DATE_SHEET} {x0} {y0} {x1} {y1} {DATE_MONTH_END + 10 - 147} {week_y - base - 44}")
    blank = (1023, 255, 1024, 256)                       # one clear texel
    for y in (0, 52):
        layout.append(f"0 164 {y} 190 {y + 26} {DATE_SHEET} {' '.join(map(str, blank))} 0 0")
    for k in range(9):
        cx, cy = 190 + k // 5 * 52, k % 5 * 62
        layout.append(f"0 {cx} {cy} {cx + 52} {cy + 62} 0 {cx} {cy} {cx + 52} {cy + 62} {DATE_WEEK_SHIFT} 0")
    Image.fromarray(sheet, "RGBA").save(out / f"{DATE_SHEET}.png", optimize=True)
    (out / "layout.txt").write_text("\n".join(layout) + "\n", encoding="utf8")


WEEK_BUTTONS = ["Settings", "Roster", "View Map", "To Class"]    # frame 0's buttons, rows of 38, then their lit looks
BACK_BUTTONS = [(80, 669, 250, 688), (30, 777, 200, 794)]        # frame 1's 戻る: on the map screen, then its flash
# The map's places in WorkRoom's kSpots order (frame 2's plates: 20 to a column of 200 x 50 cells).
MAP_PLACES = ["Golden Bough Hunt", "Campnero", "Stargazing Canopy", "The Watchtower", "Central Schoolhouse",
              "Forest of Wandering", "Amanjio Battlefield", "Inner Sea", "Payan", "Razenis Strait", "Unmoving Bridge",
              "Hall of Contemplation", "Baleos Wasteland", "Storm Hill", "Sea God's Shrine", "The Pale Fortress",
              "Scales Cave", "Old Imperial Library", "Saints' Tomb", "Daleos Undercity", "Keneone Forest", "Elberois",
              "Hermes Plain", "Sera's Water-Weave", "Darowana Marsh", "Starfall Hill"]
MAP_OFFSET = (112, 53)      # the opening's clean map (opdata.bin frame 43) sits here on frame 1
MAP_RIGHT = 980             # the map's right edge on frame 1: a longer name takes two lines
PLATE_FRAME, PLATE_STRIP = 4, 340   # the lit plates' English: a new frame of 340 x 50 strips, 10 to a column,
PLATE_MARGIN = 12                   # each starting this far left of the text's place (Hermes Plain sits over its icon)


def map_spots():
    """Each place's label position, from game/workroom/workroom_tables.hpp's kSpots."""
    src = (REPO / "game/workroom/workroom_tables.hpp").read_text()
    rows = re.findall(r"\{(-?\d+), (-?\d+), (-?\d+), (-?\d+), (-?\d+), (-?\d+), (-?\d+), (-?\d+)\}", src)
    return [(int(r[4]), int(r[5])) for r in rows[:len(MAP_PLACES)]]


def week_buttons():
    """Frame 0's buttons and frame 1's 戻る, each label covered from its face and redrawn."""
    rel = "data/workroom/workroom.bin"
    sheet = Sheet(english_opening.decode(frames_of(GAME / rel)[0]).astype(float))
    for i in range(8):
        face_label(sheet, (20, i * 38 + 6, 144, i * 38 + 32), WEEK_BUTTONS[i % 4], 18)
    sheet.save(rel, 0)


def map_screen(preview):
    """The map screen (workroom.bin frames 1 and 2): the place names baked into the map, and the lit plates.

    The opening has the same map without names, so each name comes off the map with that copy,
    colour-matched to what is round it, as a diff. The plates line up with the map, so erasing
    all but a plate's icon shows the cleaned map. The lit names are wider than the plates' cells,
    so layout.txt draws each plate as its icon plus an English strip from a new frame."""
    rel = "data/workroom/workroom.bin"
    frames = frames_of(GAME / rel)
    the_map, plates = (english_opening.decode(frames[i]).astype(float) for i in (1, 2))
    opening = english_opening.decode(english_opening.frames_of(GAME / "data/op/opdata.bin")[43]).astype(float)
    ox, oy = MAP_OFFSET
    clean = np.zeros_like(the_map)
    clean[oy:oy + opening.shape[0], ox:] = opening[:, :the_map.shape[1] - ox]
    sheet = Sheet(the_map)
    strips = np.zeros((512, 1024, 4), float)
    layout = ["# The map's lit plates: each cell's icon, then its English strip on frame 4."]
    plate_erase = np.zeros(plates.shape[:2], bool)
    spots = map_spots()
    found = []
    for i, (lx, ly) in enumerate(spots):
        cx, cy = i // 20 * 200, i % 20 * 50
        plate = plates[cy:cy + 50, cx:cx + 200]
        drawn = (np.abs(plate[..., :3] - the_map[ly:ly + 50, lx:lx + 200, :3]).max(axis=2) > 36) & (plate[..., 3] > 0)
        cols = set(np.flatnonzero(drawn.any(axis=0)))
        icon_end = min(cols)
        while cols & {icon_end + 1, icon_end + 2, icon_end + 3}:    # the icon: the first run of columns
            icon_end += 1
        rows = np.flatnonzero(drawn[:, icon_end + 1:].any(axis=1))
        found.append((icon_end, max(cols), rows[0], rows[-1], drawn))
    # The names off the map: the boxes right of the icons, filled from the clean copy.
    boxes = [(lx + ie + 2, ly + y0 - 3, lx + te + 4, ly + y1 + 4) for (lx, ly), (ie, te, y0, y1, _) in zip(spots, found)]
    named = np.zeros(the_map.shape[:2], bool)
    for x0, y0, x1, y1 in boxes:
        named[y0:y1, x0:x1] = True
    pad = 8
    for x0, y0, x1, y1 in boxes:
        around = (slice(y0 - pad, y1 + pad), slice(x0 - pad, x1 + pad))
        here, copy, ring = the_map[around][..., :3], clean[around][..., :3], ~named[around]
        fit = np.empty_like(copy)
        for c in range(3):
            a = np.stack([copy[..., c][ring], np.ones(ring.sum())], axis=1)
            k = np.linalg.lstsq(a, here[..., c][ring], rcond=None)[0]
            miss = np.abs(a @ k - here[..., c][ring])
            good = miss < np.percentile(miss, 70)      # not other labels' glow, nor details only one copy has
            k = np.linalg.lstsq(a[good], here[..., c][ring][good], rcond=None)[0]
            fit[..., c] = copy[..., c] * k[0] + k[1]
        inside = np.zeros(ring.shape, bool)
        inside[pad:-pad, pad:-pad] = True
        weight = np.clip(ndimage.distance_transform_edt(inside) / 2.5, 0, 1)[..., None]
        sheet.rebuild(around, np.clip(fit * weight + here * (1 - weight), 0, 255))
    # The English, on the map and on the plates, in each one's own colour.
    for i, ((lx, ly), (icon_end, _, y0, y1, drawn), name) in enumerate(zip(spots, found, MAP_PLACES)):
        cx, cy = i // 20 * 200, i % 20 * 50
        plate = plates[cy:cy + 50, cx:cx + 200]
        keep = ndimage.binary_dilation(drawn, iterations=1)
        keep[:, icon_end + 1:] = False
        plate_erase[cy:cy + 50, cx:cx + 200] = ~keep & (plate[..., 3] > 0)
        big = name in ("Hall of Contemplation", "Unmoving Bridge", "The Watchtower", "Central Schoolhouse", "The Pale Fortress")
        size = 18 if big else 16
        shift = english_opening.PLACE_SHIFT.get(name, 0)
        text = drawn[:, icon_end + 1:]
        lit = plate[:, icon_end + 1:][text & (plate[:, icon_end + 1:, :3].sum(axis=2) > 540)]
        plain_box = the_map[ly:ly + 50, lx + icon_end + 1:lx + 200]
        plain = plain_box[text & (plain_box[..., :3].sum(axis=2) > 480)]
        left = icon_end + 3 + shift
        lines = [name]
        if lx + left + english_opening.render(name, size, (0, 0, 0), (0, 0, 0), 1).shape[1] > MAP_RIGHT:
            lines = name.rsplit(" ", 1)     # past the map's edge: two lines
        strip = np.zeros((50, PLATE_STRIP, 4), float)
        for over, rgb, x0, y in ((strip, lit, PLATE_MARGIN - icon_end - 1, 0), (sheet.over, plain, lx, ly)):
            fill = tuple(int(v) for v in np.median(rgb[:, :3], axis=0)) if len(rgb) else (255, 247, 222)
            imgs = [english_opening.render(line, size, fill, (24, 16, 8), 1).astype(float) for line in lines]
            step = size + 1
            top = y + (y0 + y1 + 1) // 2 - (imgs[0].shape[0] + step * (len(imgs) - 1)) // 2
            for k, img in enumerate(imgs):
                put(over, img[..., 3] / 255, x0 + left, top + k * step, img[..., :3])
        # The lit plate: the game's icon from its cell, then the English from frame 4, which is wider.
        sx, sy = i // 10 * PLATE_STRIP, i % 10 * 50
        strips[sy:sy + 50, sx:sx + PLATE_STRIP] = strip
        key = f"2 {cx} {cy} {cx + 200} {cy + 50}"
        layout += [f"{key} 2 {cx} {cy} {cx + icon_end + 1} {cy + 50} 0 0",
                   f"{key} {PLATE_FRAME} {sx} {sy} {sx + PLATE_STRIP} {sy + 50} {icon_end + 1 - PLATE_MARGIN} 0"]
    for box in BACK_BUTTONS:
        face_label(sheet, box, "Back", 15, RESULTS_FONT)
    sheet.save(rel, 1, preview, "workroom_1")
    out = PACK / "images" / rel
    english_opening.erase_png(plate_erase).save(out / "2.erase.png", optimize=True)
    Image.fromarray(strips.round().astype(np.uint8), "RGBA").save(out / f"{PLATE_FRAME}.png", optimize=True)
    with open(out / "layout.txt", "a", encoding="utf8") as f:
        f.write("\n".join(layout) + "\n")


RESULTS_FONT ="/usr/share/fonts/truetype/liberation/LiberationSerif-Bold.ttf"
# The results table's label cells (rows y0..y1 of the left column), their English and size.
RESULTS_CELLS = [((82, 115), "Battle Rating", 20), ((117, 143), "Time Bonus", 17), ((148, 171), "Annihilation Bonus", 17),
                 ((190, 223), "Total EXP", 20), ((225, 251), "To Class EXP", 17), ((253, 279), "Class EXP Held", 17),
                 ((299, 333), "Units Deployed", 20), ((335, 361), "Defeated / Score", 17), ((363, 389), "Units Lost", 17)]
RESULTS_STRIP = (66, 77, 255)    # the cells' light left edge, then the cell, to x 255
RESULTS_BUTTONS = [(382, 712, 666, 739), (22, 774, 300, 798)]    # the 進行 button's faces: as shown, then lit
RESULTS_NEXT = "Next"


def fill_from(img, known, sigmas=(1.5, 3, 6, 12, 24, 48)):
    """img's colours where known is False, spread from the known texels nearest them."""
    out = np.zeros(img.shape[:2] + (3,))
    done = np.zeros(img.shape[:2], bool)
    for i, sigma in enumerate(sigmas):
        w = ndimage.gaussian_filter(known.astype(float), sigma, mode="nearest")
        f = np.stack([ndimage.gaussian_filter(img[..., c] * known, sigma, mode="nearest") for c in range(3)], -1)
        ok = (w > (0.2 if i < len(sigmas) - 1 else 0)) & ~done
        out[ok] = (f / np.maximum(w, 1e-9)[..., None])[ok]
        done |= ok
    return out


def clean_cell(jp, tw, threshold=16):
    """A table cell with the Japanese gone: the TW disc's copy where only the Japanese covers it,
    and a fill from the cell where both discs have text."""
    differ = np.abs(jp - tw).max(axis=2) > 0
    ys, xs = np.where(differ)
    text = np.zeros_like(differ)
    text[max(ys.min() - 3, 0):ys.max() + 4, max(xs.min() - 3, 0):xs.max() + 4] = True
    # The cell without any text: the texels between the glyphs that both discs agree on.
    plain = ~ndimage.binary_dilation(differ, iterations=2) & (jp[..., :3].mean(axis=2) < 90)
    background = fill_from(jp, plain | ~text, (3, 6, 12, 24, 48))
    off = lambda a: ndimage.binary_dilation(np.abs(a[..., :3] - background).max(axis=2) > threshold, iterations=1) & text
    on_jp, on_tw = off(jp), off(tw)
    out = jp.copy()
    out[on_jp & ~on_tw] = tw[on_jp & ~on_tw]
    hole = on_jp & on_tw
    out[hole, :3] = fill_from(out, ~hole)[hole]
    return out


def serif_ink(text, size, font=RESULTS_FONT):
    """Like ink(), in the results screen's serif."""
    font = ImageFont.truetype(font, size * SS)
    l, t, r, b = font.getbbox(text)
    canvas = Image.new("L", (r - l + 8 * SS, b - t + 8 * SS), 0)
    ImageDraw.Draw(canvas).text((4 * SS - l, 4 * SS - t), text, font=font, fill=255)
    canvas = canvas.resize((canvas.width // SS, canvas.height // SS), Image.LANCZOS)
    a = np.array(canvas).astype(float) / 255
    ys, xs = np.where(a > 0.02)
    return a[ys.min():ys.max() + 1, xs.min():xs.max() + 1]


def shadowed(over, a, x, y, colour):
    """Text with the results screen's dark glow round it."""
    pad = 3
    halo = ndimage.gaussian_filter(ndimage.maximum_filter(np.pad(a, pad), 3), 0.8)
    put(over, np.clip(halo * 1.4, 0, 0.85), x - pad, y - pad, (0, 0, 0))
    put(over, a, x, y, colour)


def face_label(sheet, box, text, font_px, font=MENU_FONT):
    """A button: its label covered row by row from the face either side of it, then the English."""
    frame = sheet.frame
    x0, y0, x1, y1 = box
    lum = frame[y0:y1, x0:x1, :3].mean(axis=2)
    label = lum > 150
    ys, xs = np.where(label)
    tx0, tx1, ty0, ty1 = x0 + xs.min() - 2, x0 + xs.max() + 3, y0 + ys.min() - 1, y0 + ys.max() + 2
    colour = np.percentile(frame[y0:y1, x0:x1][label, :3], 90, axis=0)
    t = np.linspace(0, 1, tx1 - tx0)[:, None]
    for y in range(ty0, ty1):
        left, right = frame[y, tx0 - 3:tx0, :3].mean(axis=0), frame[y, tx1:tx1 + 3, :3].mean(axis=0)
        sheet.rebuild((y, slice(tx0, tx1)), left * (1 - t) + right * t)
    a = serif_ink(text, font_px) if font == RESULTS_FONT else ink(text, font_px, tracking=0.6)[0]
    put(sheet.over, a, round((x0 + x1 - a.shape[1]) / 2), round((ty0 + ty1 - a.shape[0]) / 2), colour)


# kojindata.bin frame 7's buttons, in their three looks: the rows' centres, and the Japanese's text and edge colours.
KOJI_BUTTONS = ("Skills", "Gear", "Change Class", "Stats Up", "Students", "Back")
KOJI_LOOKS = [((20, 67, 114, 161, 208, 301), (8, 8, 8), (239, 239, 231)),
              ((346, 390, 434, 478, 522, 566), (8, 8, 8), (239, 239, 231)),
              ((610, 654, 698, 742, 786), (255, 247, 247), (33, 33, 24))]


def koji_buttons():
    """The character screen's buttons (kojindata.bin frame 7): each label covered from its face, row by row,
    as a diff, then the English in its look's own colours, so the lit look still stands out."""
    rel = "data/menu/kojindata.bin"
    out = PACK / "images" / rel
    frame = decode(frames_of(GAME / rel)[7]).astype(float)
    sheet = Sheet(frame)
    sheet.over = np.array(Image.open(out / "7.png").convert("RGBA")).astype(float)    # game_images' labels
    for ys, fill, edge in KOJI_LOOKS:
        for y, text in zip(ys, KOJI_BUTTONS):
            x0, y0, x1, y1 = 205, y - 12, 320, y + 13
            face = frame[y0:y1, x0:x1, :3]
            row = np.median(face, axis=1, keepdims=True)
            label = ndimage.binary_dilation(np.abs(face - row).max(axis=2) > 40, iterations=1)
            ys_, xs = np.where(label)
            tx0, tx1, ty0, ty1 = x0 + xs.min() - 1, x0 + xs.max() + 2, y0 + ys_.min(), y0 + ys_.max() + 1
            t = np.linspace(0, 1, tx1 - tx0)[:, None]
            for yy in range(ty0, ty1):
                left, right = frame[yy, tx0 - 3:tx0, :3].mean(axis=0), frame[yy, tx1:tx1 + 3, :3].mean(axis=0)
                sheet.rebuild((yy, slice(tx0, tx1)), left * (1 - t) + right * t)
            size = 17
            while True:
                img = english_opening.render(text, size, fill, edge, 1, font_path=MENU_FONT).astype(float)
                if img.shape[1] <= x1 - x0 + 4 or size <= 12:
                    break
                size -= 1
            put(sheet.over, img[..., 3] / 255, round((x0 + x1 - img.shape[1]) / 2), round(y + 1 - img.shape[0] / 2), img[..., :3])
    sheet.save(rel, 7)


# handata.bin frame 0: the top bar's 執務室 and 名簿 (their glyphs' centre x), the lit looks' 76 x 24 cells
# (drawn at screen y 9 and the x given), and the 待機 plates' face rows.
HAN_BAR = [(215.5, (332, 816, 179), "Office"), (316, (332, 840, 277), "Roster")]
HAN_BAR_KEEP = [(15, 0, 85, 40), (100, 0, 165, 40), (452, 0, 535, 40), (925, 0, 1024, 40)]    # LOAD, SAVE, Tutorial, To Title
HAN_BAR_FONT = "/usr/share/fonts/truetype/liberation/LiberationSerif-Regular.ttf"
HAN_WAIT = [(574, 591), (671, 688)]


def bar_ink(text):
    """The bar's letters: dark serif, with the soft glow round them."""
    a = np.pad(serif_ink(text, 21, HAN_BAR_FONT), 4)
    glow = np.clip(ndimage.gaussian_filter(ndimage.maximum_filter(a, 5), 1.0) * 1.5, 0, 1)
    return a, glow


# handata.bin frame 0's selectable buttons, in all their looks: drawn like the character screen's
# (koji_buttons) by han_buttons(), not as plain labels.
HAN_BUTTONS_AT = ("data/menu/handata.bin", 0)
HAN_BUTTONS = {"Summary", "Goal", "Terms", "Force", "Basic", "Mid", "Adv.", "Level", "Class", "Total Power", "Limit", "Free"}


def label_texels(face):
    """A button face's label texels: what stands out from its row, and the soft edge round it."""
    row = np.median(face, axis=1, keepdims=True)
    core = np.abs(face - row).max(axis=2) > 36
    core[:, :2] = core[:, -2:] = False                  # the face's own rim at the sides
    faint = np.abs(face - row).max(axis=2) > 18          # the strokes' soft edges, near them
    label = ndimage.binary_dilation(core, iterations=2) | (ndimage.binary_dilation(core, iterations=4) & faint)
    label[:, :2] = label[:, -2:] = False
    return core, label, row


def blank_faces(frame, buttons):
    """Each button's face without its label. Buttons of one look are the same art, so their faces are
    lined up (the first one's size, centred on each) and each texel taken from the ones whose label
    leaves it clear (a label: what differs from their median); one that all the labels cover
    comes from the face's other side, mirrored, else the clean texels beside it in its row.
    buttons: (x0, y0, x1, y1) regions holding each face. Returns [(region, rgb)], lined up."""
    groups = []                                  # [reference region, [(region, face, label, index)]]
    order = []
    for x0, y0, x1, y1 in buttons:
        best = None
        cx, cy = (x0 + x1) // 2, (y0 + y1) // 2
        for g in groups:
            rx0, ry0, rx1, ry1 = g[0]
            w, h = rx1 - rx0, ry1 - ry0
            ref = frame[ry0:ry1, rx0:rx1, :3]
            for dy in range(-4, 5):
                for dx in range(-4, 5):
                    ax, ay = cx - w // 2 + dx, cy - h // 2 + dy
                    if ay < 0 or ax < 0 or ay + h > frame.shape[0] or ax + w > frame.shape[1]:
                        continue
                    d = np.median(np.abs(frame[ay:ay + h, ax:ax + w, :3] - ref).max(axis=2))
                    if d < 8 and (best is None or d < best[0]):
                        best = (d, g, (ax, ay, ax + w, ay + h))
        region = best[2] if best else (x0, y0, x1, y1)
        face = frame[region[1]:region[3], region[0]:region[2], :3]
        member = (region, face, label_texels(face)[1], len(order))
        order.append(None)
        if best:
            best[1][1].append(member)
        else:
            groups.append([region, [member]])
    made = []
    for _, members in groups:
        faces = np.stack([m[1] for m in members])
        if len(members) > 2:
            # The labels: what stands out in a face's own row (faint too: a dim look's letters are low),
            # but not what they all have alike (the art: the groove, the rim); then again as what
            # differs from the median without them.
            seed = np.stack([ndimage.binary_dilation(np.abs(f - np.median(f, axis=1, keepdims=True)).max(axis=2) > 16)
                             for f in faces])
            common = seed.all(axis=0)
            shared = ndimage.binary_dilation(common & ((faces.max(axis=0) - faces.min(axis=0)).max(axis=2) > 30),
                                             iterations=2)     # where a label may be in all of them
            seed &= ~common
            blank = np.nanmedian(np.where(seed[..., None], np.nan, faces), axis=0)
            blank = np.where(np.isnan(blank), np.median(faces, axis=0), blank)
            for _ in range(3):
                masks = ndimage.binary_dilation(np.abs(faces - blank).max(axis=3) > 24, iterations=2,
                                                structure=np.ones((1, 3, 3), bool))
                blank = np.nanmedian(np.where(masks[..., None], np.nan, faces), axis=0)
        else:
            masks = np.stack([m[2] for m in members])
            blank = np.nanmedian(np.where(masks[..., None], np.nan, faces), axis=0)
            shared = np.zeros(blank.shape[:2], bool)
        made.append([members, blank, shared])
    for members, blank, shared in made:
        hole = np.isnan(blank).any(axis=2)
        if hole.any() or shared.any():
            # What every label covers: the face is the same mirrored, so the other side where it's clear
            # (the axis: the flip that matches best where both sides are).
            w = blank.shape[1]
            def flip(k):
                cols = k - np.arange(w)
                f = np.full_like(blank, np.nan)
                ok = (cols >= 0) & (cols < w)
                f[:, ok] = blank[:, cols[ok]]
                return f
            def misfit(k):
                d = np.abs(flip(k) - blank).max(axis=2)
                return np.nanmedian(d) if np.isfinite(d).sum() > blank.shape[0] * w // 3 else np.inf
            k = min(range(w // 2, w + w // 2), key=misfit)
            if misfit(k) < 6:
                mirrored = flip(k)
                # A label left in all of them shows as what the other side hasn't.
                left = shared & (np.abs(blank - mirrored).max(axis=2) > 30)
                blank = np.where((hole | left)[..., None] & np.isfinite(mirrored), mirrored, blank)
        # Then the row's nearest clean texels either side, a few averaged.
        hole = np.isnan(blank).any(axis=2)
        for y in range(blank.shape[0]):
            clear = np.flatnonzero(~hole[y])
            for c in np.flatnonzero(hole[y]):
                near = np.concatenate((clear[clear < c][-2:], clear[clear > c][:2]))
                blank[y, c] = blank[y, near].mean(axis=0) if len(near) else 0
        for m in members:
            order[m[3]] = (m[0], blank)
    return order


def han_buttons(sheet):
    """On han_bar's sheet: each button's face blank (blank_faces) and the English on it in the
    character screen's sans: dark with a light edge on a lit face, the Japanese's light colour with a
    dark edge on a dim one, so the chosen one stands out."""
    rel, index = HAN_BUTTONS_AT
    frame = sheet.frame
    labels = [(clamp(frame, (box[0] - 3, box[1] - 4, box[2] + 3, box[3] + 4)), text)
              for box, text, *_ in LABELS[rel][index] if text in HAN_BUTTONS]
    for (x0, y0, x1, y1), rgb in blank_faces(frame, [r for r, _ in labels]):
        sheet.rebuild((slice(y0, y1), slice(x0, x1)), rgb)
    for (x0, y0, x1, y1), text in labels:
        face = frame[y0 + 3:y1 - 3, x0:x1, :3]
        core, _, row = label_texels(face)
        if not core.any():
            continue
        lit = (face - row)[core].mean() < 0              # dark letters: the lit look
        fill, edge = ((8, 8, 8), (239, 239, 231)) if lit else \
            (tuple(int(v) for v in np.percentile(face[core], 90, axis=0)), (24, 24, 18))
        size = min(15, y1 - y0 - 9)
        while True:
            img = english_opening.render(text, size, fill, edge, 1, font_path=MENU_FONT).astype(float)
            if img.shape[1] <= x1 - x0 + 12 or size <= 9:
                break
            size -= 1
        cy = (y0 + y1) / 2
        put(sheet.over, img[..., 3] / 255, round((x0 + x1 - img.shape[1]) / 2), round(cy + 1 - img.shape[0] / 2), img[..., :3])


def han_bar():
    """The team screen's top bar and 待機 plates (handata.bin frame 0).

    執務室 and 名簿 are covered with the bar's own stretch of the same rows, as a diff, and drawn in the
    style of LOAD and SAVE; their lit looks are erased and redrawn alike in white; the plates' 待機 is
    covered from the face either side of it."""
    rel = "data/menu/handata.bin"
    out = PACK / "images" / rel
    frame = decode(frames_of(GAME / rel)[0]).astype(float)
    sheet = Sheet(frame)
    sheet.over = np.array(Image.open(out / "0.png").convert("RGBA")).astype(float)    # game_images' labels
    erased = out / "0.erase.png"
    erase = np.array(Image.open(erased))[..., 3] > 0 if erased.exists() else np.zeros(frame.shape[:2], bool)
    boxes = [(round(cx) - 32, 6, round(cx) + 32, 33) for cx, _, _ in HAN_BAR]
    for (cx, (sx, sy, at), text), box in zip(HAN_BAR, boxes):
        cover_along(sheet, box, HAN_BAR_KEEP + boxes, (0, 900))
        a, glow = bar_ink(text)
        x, y = round(cx - a.shape[1] / 2), 12 - 4
        put(sheet.over, glow, x, y, (115, 115, 115))
        put(sheet.over, a, x, y, (8, 8, 8))
        # The lit look: a white glow, dark at its rim, in the cell drawn at (at, 9).
        erase[sy:sy + 24, sx:sx + 76] = True
        lit = np.zeros((24, 76 + 40, 4))                 # wide enough to draw in, then cut to the cell
        lx, ly = x - at + 20, y - 9
        rim = np.clip(ndimage.gaussian_filter(ndimage.maximum_filter(a, 7), 1.0) * 2, 0, 1)
        for cover, colour in ((rim, (16, 16, 16)), (glow, (255, 255, 255)), (a, (8, 8, 8))):
            put(lit, cover[max(-ly, 0):], lx, max(ly, 0), colour)
        sheet.over[sy:sy + 24, sx:sx + 76] = lit[:, 20:96]
    # To Title: the bar rebuilt under it (the watermark runs on behind it), the letters in the
    # Japanese's light grey with its soft dark glow.
    cover_along(sheet, (916, 4, 1020, 38), HAN_BAR_KEEP[:3] + boxes, (0, 900))
    a = np.pad(serif_ink("To Title", 21, RESULTS_FONT), 6)
    glow = np.clip(ndimage.gaussian_filter(ndimage.maximum_filter(a, 5), 2.0) * 1.6, 0, 1) * 0.9
    x, y = round(970 - a.shape[1] / 2), round(22 - a.shape[0] / 2)
    put(sheet.over, glow, x, y, (6, 6, 6))
    put(sheet.over, a, x, y, (206, 204, 198))
    for y0, y1 in HAN_WAIT:
        x0, x1 = 13, 49
        t = np.linspace(0, 1, x1 - x0)[:, None]
        for y in range(y0, y1):
            left, right = frame[y, x0 - 3:x0, :3].mean(axis=0), frame[y, x1:x1 + 3, :3].mean(axis=0)
            sheet.rebuild((y, slice(x0, x1)), left * (1 - t) + right * t)
        a, top = ink("Wait", 16)
        put(sheet.over, a, x0 + 1, round(y0 + 4 - top), (255, 255, 255))
    han_buttons(sheet)
    english_opening.erase_png(erase).save(erased, optimize=True)
    sheet.save(rel, 0)


# kojindata.bin frame 8: the skill name plates (rect 0x12, 120 x 12 from (0, 756)), 21 per column, as
# drawSkillName reads them. The Japanese is a 1-bit white font on opaque black: so is the English.
SKILL_COLUMNS = [
    ["Concentration", "Break Smash", "Toughness", "Burn Nap", "Life", "Aura Ray", "Angelic Arms", "Shield Smith",
     "Multi Attack", "Attack Sense", "Air Slash", "Eagle Claw", "Striker", "Hawk Eye", "Spiral Shot", "Swift",
     "Poison Arrow", "Silver Arrowhead", "Multi Shoot", "Arrow Rain", "Snipe"],
    ["Regenerate", "Crack Shot", "Dissolution", "Poison Trap", "Entangle Prison", "Mage Killer", "Critical Arrow",
     "Recover", "Bless", "Heal", "Holy Purge", "Major Heal", "Haste", "Great Heal", "Dark Chime", "Acid Chain",
     "Truth", "Dark Force", "Word of Curse", "Slow", "Blood Ritual"],
    ["Phantom Image", "Clairvoyance", "Mind Leak", "Power Blast", "Dispel Magic", "Refuse Magic", "Life Force",
     "Hermit Charm", "Stone Fang", "Gaia", "Wilderness", "Magic Growth", "Magic Resist", "Earth Bind", "Sylph Field",
     "Noise", "Thunderbolt", "Tender Wind", "Feather Walk", "Quick Gate", "Tear Storm"],
    ["Web", "Freeze Lance", "Magic Reduce", "Icicle Fall", "Magician's Charm", "Tentacles Pile", "Frost Harden",
     "Fire Arrow", "Heat Accel", "Explosion", "Master Force", "Enchant Weapon", "Strike Nova", "Shooting Star",
     "Skill Mastery", "Iconoclast", "Soul Punisher", "Holy Oath", "Power Word", "Resurrect Orb", "Sacrifice"],
]


def skill_names(columns=SKILL_COLUMNS, font=FONT):
    """Each plate redrawn whole on opaque black, as the Japanese's is, so no texel of it shows; the name
    in the pack's outlined label style, one size for all.
    columns: 4 lists of 21 names in the game's order (None keeps the Japanese plate)."""
    out = PACK / "images/data/menu/kojindata.bin"
    out.mkdir(parents=True, exist_ok=True)
    names = [n for column in columns for n in column if n]
    size = min(fitted_size(n, 120, 12, font) for n in names)
    img = Image.new("RGBA", (512, 1024), (0, 0, 0, 0))
    for c, column in enumerate(columns):
        for r, name in enumerate(column):
            if name:
                cell = Image.new("RGBA", (120, 12), (0, 0, 0, 255))
                cell.alpha_composite(render_line(name, 120, 12, (240, 240, 232), size, font, steady=True))
                img.paste(cell, (c * 120, 756 + r * 12))
    img.save(out / "8.png", optimize=True)


# handata.bin frame 4: the quest plates, 22 texels apart in columns, their names in the boxes below.
QUEST_LABELS = (rows(9, 106, 4, 22, 14, ["First Battle", "Light in the Dark", "Chaos Wells Up", "The Cursed Child", "Sunlight", "Battle of Segnor",
                                    "Encounter", None, "Taking Euria", "Parting Escort", "Defense vs. Dark", None, "Night Ruins",
                                    "Pale Fortress", None, None, None, None, None, "The Seal", "Endymion", "Provocation", "Rescue"])
           + rows(175, 271, 4, 22, 14, ["Watchtower Spire", "Unmoving Bridge", "Silent Chapel", "A Girl's Wildfire", "Baleos Wastes", "King of Ruin",
                                        None, "To the Next Cycle", "Retake Dalesion", "Yard Training"])
           + rows(336, 434, 4, 22, 14, ["Deep Drill", "Sandbar Battle", "Arts vs. Dark", None, None, None, None, None, None, None, None,
                                        "Saint's Tomb", "Great Library", "Labyrinth"])
           + rows(292, 322, 422, 22, 14, ["Now", "Now", "Now", "Now"]) + [((336, 488, 436, 502), "Self Study")])


def quest_plates(preview, labels=QUEST_LABELS, font=FONT):
    """Each name's Japanese texels rebuilt from the same colour's other plates in the column (the
    per-texel median where they have no text), then the new name in the pack's outlined label style.
    labels: (box, name) pairs as QUEST_LABELS has them; a None name keeps the Japanese."""
    rel = "data/menu/handata.bin"
    jp = decode(frames_of(GAME / rel)[4]).astype(float)
    sheet = Sheet(jp)
    lum = jp[..., :3].mean(axis=2)
    columns = {}
    for (x0, y0, x1, y1), text, *_ in labels:
        columns.setdefault((round(x0), round(x1)), []).append((round(y0), round(y1), text))
    size = min(fitted_size(t, round(x1) - round(x0), round(y1) - round(y0), font) for (x0, y0, x1, y1), t, *_ in labels if t)
    for (bx0, bx1), boxes in columns.items():
        x0, x1 = bx0 - 4, bx1 + 4      # the Japanese runs past the boxes; texels like the clean plate are kept anyway
        def text_mask(y0, y1):
            here = lum[y0:y1, x0:x1]
            return ndimage.binary_dilation(here > np.median(here, axis=1, keepdims=True) + 45, iterations=1)
        # Plates of one colour: their median colour (the text's white aside) within 30 of the group's first.
        # The column's other plates (every 22 texels; their names kept) lend their plain texels too.
        h = boxes[0][1] - boxes[0][0]
        colour = lambda y0: np.median(jp[y0:y0 + h, x0:x1, :3].reshape(-1, 3), axis=0)
        groups = []
        def group_of(y0):
            return next((g for g in groups if np.abs(colour(g[0][0]) - colour(y0)).max() < 30), None)
        for y0, y1, text in boxes:
            g = group_of(y0)
            g.append((y0, y1, text)) if g else groups.append([(y0, y1, text)])
        taken = {y0 for y0, _, _ in boxes}
        for y0 in range(boxes[0][0] % 22, jp.shape[0] - h - 2, 22):
            if y0 not in taken and (g := group_of(y0)):
                g.append((y0, y0 + h, None))
        for plates in groups:
            top, h = -1, plates[0][1] - plates[0][0] + 3          # the plate's inside: a row over the box, two under
            stack = np.stack([jp[y0 + top:y0 + top + h, x0:x1, :3] for y0, _, _ in plates])
            masks = np.stack([text_mask(y0 + top, y0 + top + h) for y0, _, _ in plates])
            # The light inner frame is bright too, but on most plates at once; text is where the names differ.
            edge = np.ones(masks.shape[1:], bool)
            edge[3:-3, 8:-8] = False
            masks &= ~(edge & (masks.mean(axis=0) >= 0.75))
            for _ in range(2):      # then again, with anything unlike the clean plate counted as text
                clean = np.nanmedian(np.where(masks[..., None], np.nan, stack), axis=0)
                bands = np.nanmedian(np.where(masks[..., None], np.nan, stack), axis=(0, 2))   # where every plate has text
                known = np.where(~np.isnan(bands).any(axis=1))[0]   # a row that is all text: the nearest that isn't
                bands = bands[known[np.abs(np.arange(len(bands))[:, None] - known[None, :]).argmin(axis=1)]]
                band = np.broadcast_to(bands[:, None, :], clean.shape)
                clean = np.where(np.isnan(clean), band, clean)
                # Inside the box, text where most plates have it: brighter than its row, unlike the plain plate.
                bright = np.zeros(clean.shape[:2], bool)
                bright[1:-2, 4:-4] = (clean - band).mean(axis=2)[1:-2, 4:-4] > 24
                # but not the plate's light inner frame: lines down or across most of it are no text
                bright &= ~(bright.mean(axis=0, keepdims=True) > 0.6) & ~(bright.mean(axis=1, keepdims=True) > 0.6)
                clean = np.where(bright[..., None], band, clean)
                masks = ndimage.binary_dilation(np.abs(stack - clean).max(axis=3) > 24, iterations=1, structure=np.ones((1, 3, 3)))
            if len({t for _, _, t in plates}) == 1:     # the same word on all of them ("Now"): no clean texel to take
                masks[:, 1:-2, 4:-4] = True
            for (y0, _, text), mask, here in zip(plates, masks, stack):
                if not text:
                    continue
                sheet.rebuild((slice(y0 + top, y0 + top + h), slice(x0, x1)), np.where(mask[..., None], clean, here))
                glyph = np.array(render_line(text, bx1 - bx0, 14, (255, 255, 255), size, font, steady=True)).astype(float)
                region = sheet.over[y0:y0 + 14, bx0:bx1]
                ga, ra = glyph[..., 3:] / 255, region[..., 3:] / 255
                alpha = ga + ra * (1 - ga)
                region[..., :3] = (glyph[..., :3] * ga + region[..., :3] * ra * (1 - ga)) / np.maximum(alpha, 1e-6)
                region[..., 3:] = alpha * 255
    sheet.save(rel, 4, preview, "handata_4")


def name_column(rel, index, x, names):
    """A frame's column of short names: one per 16-texel row from (x, 0), 64 wide, white.
    names: in unit order (the 34 students, then Denehere where the frame has him)."""
    out = PACK / "images" / rel
    out.mkdir(parents=True, exist_ok=True)
    over = np.zeros((1024, 512, 4), float)
    for i, name in enumerate(names):
        a, top = ink(name, 12)
        a = a[:, :62]
        put(over, a, x + (64 - a.shape[1]) // 2, i * 16 + 3 - round(top), (255, 255, 255))    # caps from row 3
    Image.fromarray(over.round().astype(np.uint8), "RGBA").save(out / f"{index}.png", optimize=True)
    erase = np.zeros((1024, 512), bool)
    erase[:16 * len(names), x:x + 64] = True
    english_opening.erase_png(erase).save(out / f"{index}.erase.png", optimize=True)


def results_names():
    """The results screen's unit cards (tacticsresult.bin frame 2)."""
    name_column("data/tacticsresult/tacticsresult.bin", 2, 256, STUDENTS + ["Denehere"])


def koji_names():
    """The character screen's student cells (kojindata.bin frame 10, rect 0xc)."""
    name_column("data/menu/kojindata.bin", 10, 304, STUDENTS)


def results_screen(preview):
    """data/tacticsresult/tacticsresult.bin frame 1: the results table's labels and the 進行 button.

    The labels sit on textured cells. The TW disc has other words on the same art, which gives back
    most of what is under the Japanese."""
    rel = "data/tacticsresult/tacticsresult.bin"
    jp = english_opening.decode(frames_of(GAME / rel)[1]).astype(float)
    tw = english_opening.decode(frames_of(GAME.parent / "tw" / rel)[1]).astype(float)
    sheet = Sheet(jp)
    over = sheet.over
    x_strip, x_cell, x_end = RESULTS_STRIP
    lum = jp[..., :3].mean(axis=2)
    for (y0, y1), text, size in RESULTS_CELLS:
        rows = slice(y0 + 1, y1)
        for x0, x1 in ((x_strip, x_cell), (x_cell, x_end)):
            if (jp[rows, x0:x1] != tw[rows, x0:x1]).any():
                sheet.rebuild((rows, slice(x0, x1)), clean_cell(jp[rows, x0:x1], tw[rows, x0:x1])[..., :3])
        cell = (lum[rows, x_strip:x_end] > 150) | (np.ptp(jp[rows, x_strip:x_end, :3], axis=2) > 60)
        colour = np.percentile(jp[rows, x_strip:x_end][cell, :3], 90, axis=0)
        a = serif_ink(text, size)
        shadowed(over, a, round((x_strip + x_end - a.shape[1]) / 2), round((y0 + y1 - a.shape[0]) / 2), colour)
    for box in RESULTS_BUTTONS:
        face_label(sheet, box, RESULTS_NEXT, 15, RESULTS_FONT)
    sheet.save(rel, 1, preview, "tacticsresult_1")
    results_names()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--preview", help="also write the painted frames to this folder")
    args = parser.parse_args()
    option_panel()
    quit_menu(args.preview)
    title_quit(args.preview)
    week_screen()
    week_buttons()
    map_screen(args.preview)
    results_screen(args.preview)
    top_bar(args.preview)
    game_images(args.preview)
    koji_buttons()
    koji_names()
    skill_names()
    quest_plates(args.preview)
    han_bar()
    english_opening.build(PACK / "images", GAME.parent, args.preview)
    import english_tutorial     # here: they import this module
    import english_net
    import english_tactics
    english_tutorial.build(args.preview)
    english_net.build(args.preview)
    english_tactics.build(args.preview)
    with zipfile.ZipFile(ROOT / "english.zip", "w", zipfile.ZIP_DEFLATED) as z:
        for f in sorted(PACK.rglob("*")):
            if f.is_file():
                z.write(f, f.relative_to(PACK).as_posix())
    print(ROOT / "english.zip")


if __name__ == "__main__":
    main()
