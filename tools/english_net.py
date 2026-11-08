"""The network screens' pictures (data/tactics/NetData.bin and MesBox.bin), and the SAVE/LOAD screens'
(data/SLProg/SLProg.bin), in English.

Every label sits on a button face or a header plate. Each is given by a region inside its face: the
Japanese found there is covered row by row from the face either side of it, and the English drawn in
the Japanese's colour, centred on the face (its edges found by scanning out to the dark border) and
sized to fit it. Written as diffs (make_english_pack.Sheet): the pack holds only the English."""
import numpy as np
from PIL import Image
from scipy import ndimage

import english_opening
import make_english_pack as pack

QUIT = "Back to TITLE (disconnect)"
# frame: [(the region the label is in, the English)]
NETDATA = {
    1: [((16, 11, 258, 30), "Please enter your player name."),
        ((48, 73, 108, 92), "OK"), ((156, 73, 228, 92), "Cancel"),
        ((48, 113, 108, 130), "OK"), ((156, 113, 228, 130), "Cancel")],
    2: [((8, 9, 230, 28), "Choose how to connect."),
        ((30, 38, 262, 58), "LAN (Local Area Network)"), ((30, 72, 262, 92), "Internet (TCP/IP)"),
        ((30, 105, 262, 125), "Back to TITLE"),
        ((30, 149, 262, 169), "LAN (Local Area Network)"), ((30, 182, 262, 202), "Internet (TCP/IP)"),
        ((30, 216, 262, 236), "Back to TITLE")],
    3: [((40, 11, 220, 30), "Multiplayer Game"), ((20, 50, 225, 70), "Games You Can Join"),
        ((40, 219, 100, 240), "Search"), ((150, 219, 215, 240), "Join"),
        ((30, 262, 230, 283), "Create"), ((30, 302, 230, 322), QUIT),
        ((40, 341, 100, 361), "Search"), ((150, 341, 215, 361), "Join"),
        ((30, 376, 230, 396), "Create"), ((30, 415, 230, 435), QUIT)],
    4: [((12, 11, 252, 31), "Multiplayer Game (Internet)"), ((12, 50, 248, 70), "Host's TCP/IP Address"),
        ((40, 116, 220, 137), "Join"), ((30, 160, 230, 180), "Create"), ((30, 199, 230, 219), QUIT),
        ((40, 238, 220, 258), "Join"), ((30, 273, 230, 293), "Create"), ((30, 312, 230, 332), QUIT)],
    5: [((140, 46, 390, 66), "Choose Characters"),
        ((25, 80, 205, 100), "Use DEFAULT Data"), ((25, 115, 205, 135), "Use SAVE Data"),
        ((30, 150, 195, 167), "Choose Members"), ((12, 174, 78, 190), "Limit"), ((12, 197, 78, 213), "Chosen"),
        ((60, 222, 170, 240), "Notes"),
        ((565, 45, 785, 66), "Selected Quest"),
        ((572, 78, 665, 96), "Overview"), ((688, 78, 778, 96), "Objective"),
        ((800, 78, 892, 96), "Terms"), ((913, 78, 1005, 96), "Forces"),
        ((660, 340, 920, 366), "~ Host Settings ~"), ((700, 372, 880, 392), "Choose a Quest"),
        ((600, 398, 750, 418), "Versus"), ((830, 398, 980, 418), "Co-op"),
        ((20, 450, 290, 470), "Chat"), ((314, 451, 410, 469), "All"), ((426, 451, 526, 469), "Team"),
        ((570, 562, 700, 582), "Player"), ((725, 562, 838, 582), "Team"),
        ((842, 562, 937, 582), "Units"), ((940, 562, 1000, 582), "Total Lv"),
        ((585, 692, 790, 712), "Show TCP/IP Address"), ((585, 732, 995, 752), "Start Game"),
        ((315, 736, 520, 756), QUIT), ((20, 737, 245, 757), "Ready"),
        # the other looks: lit, pressed, waiting
        ((15, 772, 235, 794), "Waiting for the Host"),
        ((262, 775, 355, 792), "Overview"), ((378, 775, 470, 792), "Objective"),
        ((490, 775, 585, 792), "Terms"), ((605, 775, 700, 792), "Forces"),
        ((262, 804, 355, 821), "Overview"), ((378, 804, 470, 821), "Objective"),
        ((490, 804, 585, 821), "Terms"), ((605, 804, 700, 821), "Forces"),
        ((20, 806, 205, 826), "Use DEFAULT Data"), ((20, 841, 205, 861), "Use SAVE Data"),
        ((262, 832, 358, 850), "All"), ((375, 832, 470, 850), "Team"),
        ((262, 862, 358, 880), "All"), ((375, 862, 470, 880), "Team"),
        ((20, 879, 225, 899), QUIT), ((20, 916, 225, 936), "Show TCP/IP Address"),
        ((20, 953, 440, 973), "Start Game")],
}
MESBOX = {1: [((110, 136, 170, 156), "Yes"), ((222, 136, 292, 156), "No"),
              ((110, 176, 170, 195), "Yes"), ((222, 176, 292, 195), "No")]}
YES_NO = [((40, 57, 122, 74), "Yes"), ((150, 57, 232, 74), "No"), ((44, 99, 114, 112), "Yes"), ((150, 99, 232, 112), "No")]
# The SAVE and LOAD screens' buttons: drawn in capitals like their LOAD and SAVE, inside the face
# (inset from the button's ornaments and lit rim).
SL_FONT = "/usr/share/fonts/truetype/liberation/LiberationSerif-Regular.ttf"
SLPROG = {
    3: YES_NO,
    4: [((456, 248, 569, 268), "DELETE", SL_FONT), ((673, 248, 786, 268), "CANCEL", SL_FONT),
        ((287, 776, 391, 796), "DELETE", SL_FONT), ((451, 776, 555, 796), "CANCEL", SL_FONT)],
    5: [((580, 631, 692, 651), "CANCEL", SL_FONT), ((287, 776, 391, 796), "CANCEL", SL_FONT)],
    6: [((14, 8, 256, 28), "These characters can't be used:"), ((14, 29, 256, 50), "\\  /  :  *  ?  \"  <  >  |")],
    7: [((14, 19, 256, 38), "Overwrite this SAVE?")] + YES_NO,
    8: [((14, 19, 256, 38), "Delete this SAVE?")] + YES_NO,
}
TITLE_BOX = (470, 4, 820, 38)     # frame 5's "マルチプレイゲーム諸設定", on nothing


def face_span(frame, y, x0, x1):
    """The face's inside along row y, out from [x0, x1) to its dark border."""
    lum = frame[y, :, :3].mean(axis=1)
    dark = (lum < 35) | (frame[y, :, 3] == 0)
    left, right = x0, x1
    while left > 0 and not dark[left - 1]:
        left -= 1
    while right < frame.shape[1] and not dark[right]:
        right += 1
    return left + 6, right - 6


def label(sheet, region, text, font=None, shadow=False):
    frame = sheet.frame
    x0, y0, x1, y1 = region
    face = frame[y0:y1, x0:x1, :3]
    lum = face.mean(axis=2)
    jp = (np.abs(lum - np.median(lum)) > 50) & (frame[y0:y1, x0:x1, 3] > 0)    # light text, or dark on a light face
    ys, xs = np.where(jp)
    if not len(xs):
        raise ValueError(f"no label in {region}")
    tx0, tx1, ty0, ty1 = x0 + xs.min() - 2, x0 + xs.max() + 3, y0 + ys.min() - 1, y0 + ys.max() + 2
    if shadow:                  # SLProg's labels have one two rows below them
        ty1 = min(ty1 + 2, y1)
    colour = np.percentile(face[jp], 90, axis=0)
    t = np.linspace(0, 1, tx1 - tx0)[:, None]
    for y in range(ty0, ty1):
        left, right = frame[y, tx0 - 3:tx0, :3].mean(axis=0), frame[y, tx1:tx1 + 3, :3].mean(axis=0)
        sheet.rebuild((y, slice(tx0, tx1)), left * (1 - t) + right * t)
    cy = (ty0 + ty1) / 2
    fx0, fx1 = face_span(frame, round(cy), tx0, tx1)
    fx0, fx1 = max(fx0, x0), min(fx1, x1)     # on a long plate, the label's own region
    size = min(16, ty1 - ty0)
    while True:
        a = pack.serif_ink(text, size + 2, font) if font else pack.ink(text, size, tracking=0.4)[0]
        if a.shape[1] <= fx1 - fx0 or size <= 10:
            break
        size -= 1
    pack.put(sheet.over, a, round((fx0 + fx1 - a.shape[1]) / 2), round(cy - a.shape[0] / 2), colour)


def build(preview=None):
    for rel, labels in (("data/tactics/netdata.bin", NETDATA), ("data/tactics/mesbox.bin", MESBOX),
                        ("data/slprog/slprog.bin", SLPROG)):
        frames = pack.frames_of(pack.GAME / rel)
        for index, items in labels.items():
            sheet = pack.Sheet(english_opening.decode(frames[index]).astype(float))
            for region, *text in items:
                label(sheet, region, *text, shadow=rel.endswith("slprog.bin"))
            if rel.endswith("netdata.bin") and index == 5:
                title(sheet)
            sheet.save(rel, index, preview, f"{rel.split('/')[-1][:-4]}_{index}")
            if getattr(sheet, "erase", None) is not None:
                mask = np.zeros((*sheet.erase.shape, 4), np.uint8)
                mask[sheet.erase] = 255
                Image.fromarray(mask, "RGBA").save(pack.PACK / "images" / rel / f"{index}.erase.png", optimize=True)


def title(sheet):
    """The setup screen's heading: the Japanese half erased (it is on nothing), the English drawn as it."""
    frame = sheet.frame
    x0, y0, x1, y1 = TITLE_BOX
    jp = np.zeros(frame.shape[:2], bool)
    jp[y0:y1, x0:x1] = frame[y0:y1, x0:x1, 3] > 0
    sheet.erase = jp
    colour = np.percentile(frame[y0:y1, x0:x1][jp[y0:y1, x0:x1], :3], 90, axis=0)
    a = pack.serif_ink("Multiplayer Setup", 26)
    pack.put(sheet.over, ndimage.maximum_filter(a, 3) * 0.6, x0 + 9, y0 + 5, (8, 8, 8))
    pack.put(sheet.over, a, x0 + 10, y0 + 6, colour)
