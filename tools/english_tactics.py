"""The battles' opening banners (data/tactics/tactstart/*.bin: the victory conditions, and the
tutorial's objectives) and the start panel (data/tactics/common.bin frames 9 and 10), in English.

A banner is an 8-bit BMP whose black is see-through, shown over the dimmed map before a battle.
Each line of Japanese is given by its rectangle and replaced by its English:
- on black, the rectangle is erased (<frame>.erase.png);
- on a picture (the dungeon floors, the gate), the Japanese's strokes are filled in from the art
  around them (<frame>.diff.png, a difference from the game's texels), and what comes out black
  is erased instead.
The English (<frame>.png) is drawn in the results screen's serif, in the Japanese's white, blue,
red or green, over a soft dark edge. Markup: {b:...} blue, {r:...} red, {g:...} green."""
import re

import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy import ndimage

import english_net
import make_english_pack as pack

FONT = pack.RESULTS_FONT
COLOURS = {"w": (255, 255, 255), "b": (82, 165, 255), "r": (247, 74, 74), "g": (81, 254, 131)}
VICTORY, OBJECTIVE = "Victory Conditions", "Objective"


def T(y0, y1, text=VICTORY):
    """The heading."""
    return ("c", y0, y1, 128, 388, [text], 40)


def C(y0, y1, x0, x1, lines, size=21):
    """Lines centred on the banner."""
    return ("c", y0, y1, x0, x1, lines if isinstance(lines, list) else [lines], size)


def L(y0, y1, x0, x1, lines, size=15, at=None):
    """Lines from the left edge `at` (the Japanese's own by default)."""
    return ("l", y0, y1, x0, x1, lines if isinstance(lines, list) else [lines], size, at)


def E(y0, y1, x0, x1):
    """Only erased (or filled in): text with no English of its own."""
    return ("e", y0, y1, x0, x1, [], 0)


# Shared by more than one banner.
RETAKE_ALL = [L(133, 160, 66, 397, "Defeat {r:every Crawler}.", 20),
              L(103, 124, 35, 178, "« Objective A »", 16), L(178, 199, 35, 178, "« Objective B »", 16),
              L(210, 237, 66, 446, "{b:Retake every} Guard Tower", 20), L(242, 268, 66, 205, "{b:and Holy Pond}.", 20)]
RED_TOWERS = [T(5, 54), L(67, 92, 1, 177, "« Objective 1 »", 16, at=8), L(296, 321, 6, 182, "« Objective 2 »", 16, at=8),
              L(101, 166, 186, 494, ["Find the red Guard Towers", "and retake both of them."], 20),
              L(167, 271, 186, 494, ["• Come near with more allies than", "  enemies and, in time, a tower", "  changes sides.",
                                     "• If one is destroyed before you", "  retake it, the adventure ends."], 14),
              L(356, 445, 186, 442, ["Defeat Urgo the Red Wall.", "", "  He appears once Objective 1", "  is complete."], 19)]
HOLD_ONE = lambda limit: [T(93, 142), C(207, 235, 19, 496, "Keep {b:holding} at least one Guard Tower."),
                          L(268, 308, 59, 462, ["• If every Guard Tower is {r:taken} within the time",
                                                f"  limit ({limit}), the adventure ends."], 15)]
FLOOR = lambda n, y0, y1: C(y0, y1, 140, 370, f"Current floor: Basement {n}", 16)
STATUES = lambda n: [T(58, 105), C(119, 144, 58, 435, "{r:Destroy} the stone statues, then head", 19),
                     C(151, 176, 58, 435, "for the door to the next floor.", 19), FLOOR(n, 371, 388)]
GATE = [T(48, 95), C(169, 194, 50, 454, "Make your way to the gate guarded", 19),
        C(201, 226, 48, 200, "by two Holy Ponds: {b:reach it}.", 19)]
NEXT_DOOR = [T(70, 117), C(148, 173, 59, 447, "Head for the door to the next floor.", 19)]
TOWERS_PLACE = lambda n, title, lines, floor: [T(*title), C(*lines[0], 101, 421, "Head for the place with", 19),
                                               C(*lines[1], 101, 421, "the Guard Towers and Ponds.", 19), FLOOR(n, *floor)]
POEM = [
    ["I stand among many {b:friends},",
     "   yet however loudly I may sing of it, you may",
     "   pity me as a {r:liar}.",
     "      I pray: offer your prayers {g:with your friends}.",
     "         I seek the prayers of {b:those who bear swords}."],
    ["I stand among many {b:friends},",
     "   yet do you know",
     "      {r:that without the one friend no other can replace,}",
     "      {r:I am nothing at all}?",
     "      I pray: offer your prayers {g:with your friends}.",
     "         I seek the prayers of {b:the embodiment of curses}",
     "         and {b:those who hold the holy light}."],
    ["I stand among many {b:friends},",
     "   yet do you know",
     "      {r:that we watch over you, always and at every moment}?",
     "      I pray: offer your prayers {g:with your friends}.",
     "         It is {b:your} prayers I seek."],
]

# tactstartNN.bin: {frame: [items]}; PICTURE: the banners whose lines are on art.
BANNERS = {
    1: {0: [T(105, 154), C(222, 249, 13, 491, "{b:Defend} until the Crawlers withdraw."),
            L(262, 300, 20, 488, ["• Enemies that reach the fort's north end {r:break through}.",
                                  "  Stop as many of them as you can."])]},
    2: {0: [T(105, 154), C(222, 249, 66, 427, "{b:Find} the one waiting to be rescued."),
            L(262, 300, 73, 462, ["• If you can't find them within the time limit,", "  it's {r:the end of the adventure}."])]},
    3: {0: [T(20, 69)] + RETAKE_ALL},
    4: {0: [T(105, 154), C(215, 242, 76, 437, "Defeat every Crawler."),
            L(261, 299, 73, 436, ["• If you can't defeat them all within the time", "  limit, the adventure {r:ends}."])]},
    5: {0: [T(84, 133), C(216, 243, 111, 413, "{b:Retake} the Holy Pond.")],
        1: [T(72, 121), C(178, 206, 112, 413, "Defeat Mother Elza.")]},
    6: {0: [T(72, 121), C(179, 206, 118, 388, "Defeat Vanchner.")]},
    7: {0: [T(105, 154), C(213, 240, 59, 446, "{b:Join up} with Cheryl and {b:go down} the pass."),
            L(253, 291, 63, 463, ["• Getting back to where the adventure began", "  counts as going down the pass."])]},
    8: {0: [T(20, 69), C(136, 163, 66, 427, "{b:Protect} the Guard Towers and Holy Ponds."),
            L(177, 215, 77, 447, ["• If all are taken within the time limit,", "  the adventure {r:ends}."])]},
    9: {0: RED_TOWERS},
    10: {0: [T(105, 154), C(213, 240, 58, 328, "{b:Escort} Fald."),
             L(253, 291, 63, 464, ["• Escort Fald until he is safely", "  through the Ashtros Pass."])]},
    11: {0: [T(105, 154), C(196, 223, 121, 393, "Hold back the Crawlers' {r:breakthrough}"),
             C(228, 255, 99, 401, "and {b:defend} until time runs out."),
             L(268, 306, 33, 481, ["• Enemies that reach the cave's south end break through.",
                                   "  Stop as many of them as you can."], 14)]},
    13: {0: [T(84, 133), C(216, 243, 102, 433, "{b:Solve} the mystery of the ruins.")],
         1: [T(4, 53), C(67, 94, 91, 422, "{b:Solve} the mystery of the ruins."),
             L(115, 203, 20, 493, POEM[0], 13), L(223, 347, 20, 493, POEM[1], 13), L(367, 473, 20, 493, POEM[2], 13)]},
    14: {0: GATE},
    15: {0: GATE},
    151: {0: HOLD_ONE("30 minutes")},
    16: {0: [T(48, 97), C(159, 186, 66, 427, "{b:Retake} every Guard Tower.")]},
    17: {0: [T(40, 89), C(134, 162, 55, 417, "Keep the adventure going for 15 minutes.", 19),
             L(174, 232, 60, 469, ["• Head for the exit sooner and the training fails.",
                                   "  (The exit: the gate with two Holy Ponds)"], 14),
             C(266, 288, 33, 407, "« Goals {g:hoped for} in Underground Training »", 17),
             L(295, 313, 59, 460, "• {b:Take and hold} every Guard Tower and Holy Pond", 15),
             L(317, 335, 59, 343, "• {b:Take and hold} every treasure chest", 15),
             L(339, 357, 59, 323, "• {r:Destroy} every Warding Wall", 15),
             L(364, 379, 76, 340, "* The more goals you meet, the better the result.", 12)]},
    18: {0: [T(20, 69), L(153, 180, 66, 446, "Retake {b:every} Guard Tower", 20), L(185, 212, 66, 191, "{b:and Holy Pond}.", 20)]},
    20: {0: [T(72, 121), C(179, 206, 111, 383, "Defeat the Sealing Catalyst.")]},
    21: {0: [T(12, 61), L(81, 102, 18, 195, "« Objective 1 »", 16),
             L(113, 140, 66, 338, "Two or more Guard Towers", 20), L(145, 172, 66, 338, "and four or more Holy Ponds:", 20),
             L(177, 204, 60, 210, "{b:retake them}.", 20),
             L(386, 407, 18, 195, "« Objective 2 »", 16), C(418, 445, 55, 474, "{b:Hold} Objective 1 for fifteen minutes.", 20)]},
    22: {0: [T(105, 154), C(203, 230, 30, 479, "Defeat all five from the Unmoving Bridge.", 20),
             C(263, 280, 224, 286, "or", 15),
             C(309, 336, 30, 468, "{b:Hold} the Guard Tower in the middle of the bridge", 19),
             C(341, 368, 30, 479, "until the time limit (ten minutes) runs out.", 19)]},
    24: {0: [T(105, 154), C(213, 240, 71, 458, "{b:Find} Chris and Axel"),
             C(245, 272, 69, 430, "and {b:help} them escape from underground.")],
         1: [T(20, 69)] + RETAKE_ALL},
    29: {0: [T(92, 141), C(204, 231, 66, 456, "{b:Take} every Guard Tower."),
             C(249, 267, 44, 482, "• Try {b:not to defeat} the Paladine students.", 15)]},
    31: {0: [T(73, 122), C(213, 240, 1, 509, "Get the three Unmoving Bridge students {b:out}.", 20),
             C(255, 273, 109, 394, "{b:Lead} them back to the starting point.", 15)]},
    32: {0: HOLD_ONE("one hour")},
    33: {0: [T(110, 159), C(246, 273, 100, 431, "{r:Wipe out} the Crawlers.")]},
    35: {0: [T(84, 133), C(216, 243, 96, 427, "{r:Destroy} every stone statue.")]},
    37: {0: NEXT_DOOR + [FLOOR(1, 368, 384)]},
    38: {0: NEXT_DOOR + [FLOOR(2, 368, 384)]},
    39: {0: NEXT_DOOR + [FLOOR(3, 368, 384)]},
    40: {0: TOWERS_PLACE(1, (40, 87), ((126, 151), (158, 183)), (378, 394))},
    41: {0: [T(105, 154), C(218, 246, 18, 493, "{b:Drive out} the {r:Chaos} nesting in Syalon's heart.", 20)]},
    44: {0: [T(72, 121), C(179, 207, 66, 456, "{b:Drive out} the {r:Chaos} lurking in the Land of Wind.", 19)]},
    45: {0: [T(72, 121), C(179, 207, 157, 341, "{r:Drive out} the Chaos.")],
         1: [C(268, 293, 189, 326, "Defeat in the Final Battle", 19)]},
    **{n: {0: STATUES(n - 47)} for n in range(48, 68)},
    68: {0: TOWERS_PLACE(21, (44, 91), ((122, 149), (154, 180)), (395, 411))},
    80: {0: [T(104, 154, OBJECTIVE), C(199, 226, 129, 397, "{b:Move} to the destination."),
             C(259, 277, 123, 407, "With a unit selected,", 16), C(286, 304, 140, 383, "right-click the destination.", 16)],
         1: [T(104, 154, OBJECTIVE), C(199, 226, 125, 397, "Defeat the {r:enemy} with a normal attack."),
             C(258, 276, 117, 401, "With a unit selected,", 16), C(285, 303, 122, 387, "right-click the target.", 16)],
         2: [T(49, 99, OBJECTIVE), C(130, 157, 91, 392, "Defeat the enemy with a skill attack."),
             C(191, 213, 65, 398, "« Switching to skill mode »", 18),
             L(224, 257, 103, 477, "A) Left-click the attack mode icon", 16),
             L(261, 294, 103, 457, "B) Double-left-click a skill icon", 16),
             L(302, 320, 107, 369, "C) Hold down the SHIFT key", 16),
             L(350, 383, 98, 452, ["* Any of A, B or C switches the mode,", "* but only with a single unit selected."], 13),
             L(403, 436, 192, 336, "← Skill mode", 15)],
         3: [T(58, 108, OBJECTIVE), C(149, 176, 86, 387, "Bring a unit onto the field."),
             L(211, 230, 86, 364, "1. Select the unit to bring in", 16),
             L(247, 266, 86, 443, "2. Right-click on the unit calling it", 16),
             L(277, 310, 96, 429, ["* The more units being called, the longer", "   it takes."], 13)],
         4: [T(58, 108, OBJECTIVE), C(149, 176, 86, 387, "Take a unit off the field."),
             L(211, 230, 124, 402, "1. Select the unit to take off", 16),
             L(237, 270, 126, 381, "2. Left-click the OUT icon", 16)]},
    82: {0: RED_TOWERS},
}
PICTURE = {14, 15, 37, 38, 39, 40, *range(48, 69)}


# --------------------------------------------------------------------------------- drawing ---
def runs(markup):
    """[(text, colour)] from "plain {b:blue} plain"."""
    out = []
    for m in re.finditer(r"\{(\w):([^}]*)\}|([^{]+)", markup):
        out.append((m.group(2), COLOURS[m.group(1)]) if m.group(1) else (m.group(3), COLOURS["w"]))
    return out


def line_art(markup, size):
    """The line's coverage and colour per texel, between the font's ascent and descent (so lines
    of one size share a baseline), cropped left and right to the ink."""
    font = ImageFont.truetype(FONT, size * pack.SS)
    ascent, descent = font.getmetrics()
    parts = runs(markup)
    width = int(sum(font.getlength(t) for t, _ in parts)) + 8 * pack.SS
    width += -width % pack.SS
    height = ascent + descent + -(ascent + descent) % pack.SS
    cover = np.zeros((height // pack.SS, width // pack.SS))
    rgb = np.zeros(cover.shape + (3,))
    x = 4 * pack.SS
    for text, colour in parts:
        canvas = Image.new("L", (width, height), 0)
        ImageDraw.Draw(canvas).text((x, 0), text, font=font, fill=255)
        a = np.array(canvas.resize((width // pack.SS, height // pack.SS), Image.LANCZOS)).astype(float) / 255
        rgb += a[..., None] * colour
        cover += a
        x += font.getlength(text)
    cover = np.clip(cover, 0, 1)
    rgb /= np.maximum(cover, 1e-6)[..., None]
    xs = np.where(cover.max(axis=0) > 0.02)[0]
    return cover[:, xs.min():xs.max() + 1], rgb[:, xs.min():xs.max() + 1]


def width_of(markup, size):
    """The line's width, its indent included."""
    if not markup.strip():
        return 0
    indent = markup[:len(markup) - len(markup.lstrip(" "))]
    return round(ImageFont.truetype(FONT, size * pack.SS).getlength(indent) / pack.SS) + line_art(markup.lstrip(" "), size)[0].shape[1]


def draw_line(over, markup, size, x, cy):
    """The line at x (its left), its middle on cy, over a soft dark edge."""
    if not markup.strip():
        return
    indent = markup[:len(markup) - len(markup.lstrip(" "))]
    x += round(ImageFont.truetype(FONT, size * pack.SS).getlength(indent) / pack.SS)
    a, rgb = line_art(markup.lstrip(" "), size)
    y = round(cy - a.shape[0] / 2)
    pad = 3
    halo = ndimage.gaussian_filter(ndimage.maximum_filter(np.pad(a, pad), 3), 1.0)
    pack.put(over, np.clip(halo * 1.2, 0, 0.75), x - pad, y - pad, (0, 0, 0))
    pack.put(over, a, x, y, rgb.reshape(-1, 3).reshape(a.shape + (3,)))


def text_strokes(frame, box):
    """The Japanese's strokes in box, on a picture: bright white, or the highlight colours, and the
    soft edge round them."""
    x0, y0, x1, y1 = box
    rgb = frame[y0:y1, x0:x1, :3]
    white = (rgb.min(axis=2) > 110) & (rgb.max(axis=2) - rgb.min(axis=2) < 40)
    red = (rgb[..., 0] > 150) & (rgb[..., 0] - rgb[..., 1] > 60)
    blue = (rgb[..., 2] > 130) & (rgb[..., 2] - rgb[..., 0] > 50)
    mask = np.zeros(frame.shape[:2], bool)
    mask[y0:y1, x0:x1] = ndimage.binary_dilation(white | red | blue, iterations=5)
    return mask & (frame[..., 3] > 0)


def picture_of(frame):
    """Where the banner's picture is: its opaque texels but the text's, closed and filled, so the
    strokes inside it count as on it and the black round it does not."""
    rgb = frame[..., :3].astype(int)
    texty = (rgb.min(axis=2) > 150) | ((rgb[..., 0] > 190) & (rgb[..., 1] < 130)) | ((rgb[..., 2] > 190) & (rgb[..., 0] < 150))
    solid = (frame[..., 3] > 0) & ~ndimage.binary_dilation(texty, iterations=5)
    solid = ndimage.binary_opening(solid, iterations=2)      # not the strokes' soft edges
    labels, n = ndimage.label(solid)
    if n:
        sizes = ndimage.sum(solid, labels, range(1, n + 1))
        solid = np.isin(labels, 1 + np.flatnonzero(sizes > 2000))
    return ndimage.binary_fill_holes(ndimage.binary_closing(solid, iterations=12))


def fill_in(sheet, mask, known):
    """english_tutorial.inpaint, drawing only on the `known` texels (the art, not the black)."""
    rgb = sheet.frame[..., :3]
    k = known.astype(float)
    out, todo = rgb.copy(), mask.copy()
    for sigma in (2, 4, 8, 16, 32):
        num = np.stack([ndimage.gaussian_filter(rgb[..., c] * k, sigma) for c in range(3)], axis=-1)
        den = ndimage.gaussian_filter(k, sigma)
        fill = todo & (den > 0.05)
        out[fill] = num[fill] / den[fill][:, None]
        todo &= ~fill
    sheet.rebuild((mask,), out[mask])


def banner(rel, index, frame, items, picture, preview=None):
    sheet = pack.Sheet(frame.astype(float))
    H, W = frame.shape[:2]
    erase = np.zeros((H, W), bool)
    art = picture_of(frame) if picture else None
    for kind, y0, y1, x0, x1, lines, size, *at in items:
        box = (max(x0 - 4, 0), max(y0 - 4, 0), min(x1 + 5, W), min(y1 + 5, H))
        if picture:
            # Strokes on the art are filled in from the art round them; those on black are erased.
            mask = text_strokes(frame, box)
            on_art = mask & art
            fill_in(sheet, on_art, art & ~mask)
            erase |= mask & ~on_art
        else:
            erase[box[1]:box[3], box[0]:box[2]] = True
        if not lines:
            continue
        left = at[0] if at and at[0] is not None else x0
        room = W - 12 - left if kind == "l" else W - 24
        while size > 9 and max(width_of(line, size) for line in lines) > room:
            size -= 1
        slot = (y1 - y0 + 1) / len(lines)
        for i, line in enumerate(lines):
            cy = y0 + slot * (i + 0.5)
            if kind == "l":
                draw_line(sheet.over, line, size, left, cy)
            else:
                centre = 256 if kind == "c" else (x0 + x1) / 2
                draw_line(sheet.over, line, size, round(centre - width_of(line, size) / 2), cy)
    save(sheet, rel, index, erase, preview, f"{rel.split('/')[-1][:-4]}_{index}")


def save(sheet, rel, index, erase, preview, name):
    sheet.save(rel, index, None)
    out = pack.PACK / "images" / rel
    if erase.any():
        mask = np.zeros(erase.shape + (4,), np.uint8)
        mask[erase] = 255
        Image.fromarray(mask, "RGBA").save(out / f"{index}.erase.png", optimize=True)
    else:
        (out / f"{index}.erase.png").unlink(missing_ok=True)
    if not sheet.fixed.any():
        (out / f"{index}.diff.png").unlink(missing_ok=True)
    if preview:
        shown = Image.new("RGBA", sheet.frame.shape[1::-1], (40, 46, 40, 255))
        base = sheet.base.copy()
        base[erase] = 0
        shown.alpha_composite(Image.fromarray(base.round().astype(np.uint8), "RGBA"))
        shown.alpha_composite(Image.fromarray(sheet.over.round().astype(np.uint8), "RGBA"))
        shown.convert("RGB").save(f"{preview}/{name}.png")


# ----------------------------------------------------------------------------- start panel ---
START_TEXT = [(8, 26, "Choose your starting members.", 15), (31, 47, "Left-click a character to select it,", 12),
              (49, 65, "then press Start to begin the adventure.", 12)]
START_BUTTONS = [((8, 19, 110, 38), "First deployment"),
                 ((112, 46, 220, 72), "Start"), ((110, 86, 220, 111), "Start"), ((110, 123, 220, 149), "Start")]


def start_panel(preview=None):
    """common.bin frame 9: "choose the starting members" on a flat plate; frame 10: the start
    button in its three looks and the "initial deployment" under "Starting Member"."""
    rel = "data/tactics/common.bin"
    frames = pack.frames_of(pack.GAME / rel)
    frame = decode(frames[9]).astype(float)
    sheet = pack.Sheet(frame)
    inner = frame[6:-6, 6:-6, :3]
    panel = np.median(inner.reshape(-1, 3), axis=0)
    jp = np.zeros(frame.shape[:2], bool)
    jp[6:-6, 6:-6] = np.abs(inner - panel).max(axis=2) > 10
    jp = ndimage.binary_dilation(jp, iterations=1) & (frame[..., 3] > 0)
    jp[:5], jp[-5:], jp[:, :5], jp[:, -5:] = False, False, False, False
    sheet.rebuild((jp,), np.broadcast_to(panel, (int(jp.sum()), 3)))
    for y0, y1, text, size in START_TEXT:
        while size > 9 and width_of(text, size) > frame.shape[1] - 16:
            size -= 1
        draw_line(sheet.over, text, size, round((frame.shape[1] - width_of(text, size)) / 2), (y0 + y1) / 2)
    sheet.save(rel, 9, preview, "common_9")

    frame = decode(frames[10]).astype(float)
    sheet = pack.Sheet(frame)
    for region, text in START_BUTTONS:
        english_net.label(sheet, region, text)
    sheet.save(rel, 10, preview, "common_10")


def decode(e):
    return pack.decode_bmp(e) if e[:2] == b"BM" else pack.decode(e)


def build(preview=None):
    start_panel(preview)
    for n, frames in BANNERS.items():
        rel = f"data/tactics/tactstart/tactstart{n:02d}.bin"
        entries = pack.frames_of(pack.GAME / rel)
        for index, items in frames.items():
            frame = decode(entries[index])
            banner(rel, index, frame, items, n in PICTURE, preview)


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--preview", help="also write the painted frames to this folder")
    build(parser.parse_args().preview)
