#!/usr/bin/env python3
"""List the places of the developer map select, from the disc's res/Menu/Menu1.dat (its code is gone from TP
HD, the data isn't). Each line is a WWHD_WARP target: stage,point,room,layer.

usage: warplist.py [game dir] [filter]   (filter: a substring of the stage name or the description)
"""
import os
import struct
import sys

game = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "game")
filt = sys.argv[2].lower() if len(sys.argv) > 2 else ""
d = open(os.path.join(game, "content", "res", "Menu", "Menu1.dat"), "rb").read()


def text(b):
    return b.split(b"\0")[0].decode("shift_jis", "replace")


groups, first = d[0], struct.unpack_from(">I", d, 4)[0]
for g in range(groups):
    o = first + g * 0x48  # menu_stage_class: label[65], count at +0x41, entries at +0x44
    label, count, entries = text(d[o:o + 0x41]), d[o + 0x41], struct.unpack_from(">I", d, o + 0x44)[0]
    for i in range(count):
        e = entries + i * 0x4C  # menu_data_class: description[65], stage[8], room s8, point u8, layer s8
        desc, stage = text(d[e:e + 0x41]), text(d[e + 0x41:e + 0x49])
        room, point, layer = struct.unpack_from(">bBb", d, e + 0x49)
        if filt and filt not in stage.lower() and filt not in desc.lower() and filt not in label.lower():
            continue
        print(f"{stage},{point},{room},{layer}\t{label} / {desc}")
