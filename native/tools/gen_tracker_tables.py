#!/usr/bin/env python3
"""Write the tracker's location and chart tables (docs/TRACKER.md) from Archipelago's Wind Waker client.

  native/tools/gen_tracker_tables.py --ap <Archipelago>/worlds/tww [--wwrando <wwrando>] [--check]

Archipelago's Wind Waker world (worlds/tww, MIT) knows the save flag behind each of wwrando's item
locations, as GameCube addresses read with Dolphin Memory Engine (Locations.py, TWWClient.py). This turns
each one into the save field the decompiled game names (a stage's chest, switch or item bit, an event bit,
an ocean bit, a chart's "complete" bit), checks the result, and writes
native/src/pc/features/pc_tracker_tables.inc. The game's item functions (game/src/d/d_item.cpp) give each
chart's collect-map number.

--wwrando: also checks the names against wwrando's logic/item_locations.txt (the tracker's names come from
that file): a location the table lacks is listed as untracked. --check: compares with the committed file
instead of writing it (exit 1 if it differs).
"""

import argparse
import ast
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "native/src/pc/features/pc_tracker_tables.inc")
D_ITEM = os.path.join(ROOT, "game/src/d/d_item.cpp")

# GameCube addresses (GZLE01) of the save fields Archipelago reads: g_dComIfG_gameInfo.save (dSv_info_c)
SAVE = 0x803C4C08
EVENT = SAVE + 0x624  # dSv_save_c::mEvent (u8 mFlags[0x100])
OCEAN = SAVE + 0x5C0  # dSv_save_c::mOcean (u16[50])
LETTER_OWND = SAVE + 0x90  # dSv_player_get_bag_item_c::mReserveFlags

# Locations whose flag Archipelago leaves commented out but gives (unverified there)
UNVERIFIED = {
    "Windfall Island - Kane - Place Shop Guru Statue on Gate",
    "Windfall Island - Kane - Place Postman Statue on Gate",
    "Windfall Island - Kane - Place Six Flags on Gate",
    "Windfall Island - Kane - Place Six Idols on Gate",
}


def parse_locations(path):
    """name -> (type, stage_id, bit, address or None), from Locations.py's LOCATION_TABLE (the commented
    entries in UNVERIFIED too)."""
    text = open(path, encoding="utf-8").read()
    table = text[text.index("LOCATION_TABLE"):]
    table = table[:table.index("\n}\n")]
    out = {}
    entry = re.compile(
        r'^\s*(#\s*)?"(?P<name>[^"]+)": TWWLocationData\(\s*(?:#\s*)?(?P<args>.*?)\s*(?:#\s*)?\),?\s*$',
        re.M | re.S)
    for block in re.split(r'\n(?=\s*#?\s*"[^"]+": TWWLocationData\()', table):
        m = entry.match(block.strip("\n"))
        if not m:
            continue
        name = m.group("name")
        commented = m.group(1) is not None
        if commented and name not in UNVERIFIED:
            continue
        args = re.sub(r"\n\s*#\s*", " ", m.group("args"))
        args = [a.strip() for a in re.sub(r"\s+", " ", args).split(",") if a.strip()]
        if len(args) < 5:
            continue  # no flag known (Orca's 500 hits, Beedle's memberships)
        kind = args[4].split(".")[-1]
        stage = int(args[3], 0)
        bit = int(args[5], 0) if len(args) > 5 else 0
        addr = int(args[6], 0) if len(args) > 6 else None
        out[name] = (kind, stage, bit, addr)
    return out


def parse_dict(path, var):
    text = open(path, encoding="utf-8").read()
    start = text.index(var)
    body = text[text.index("{", start):]
    depth = 0
    for i, ch in enumerate(body):
        depth += ch == "{"
        depth -= ch == "}"
        if depth == 0:
            return ast.literal_eval(body[:i + 1])
    raise ValueError(var)


def parse_chart_items(path):
    """chart name -> item number, from Items.py's ITEM_TABLE."""
    out = {}
    for m in re.finditer(r'^\s*"([^"]*Chart[^"]*)":\s*TWWItemData\(.*,\s*(0x[0-9A-Fa-f]+)\),?\s*$',
                         open(path, encoding="utf-8").read(), re.M):
        out[m.group(1)] = int(m.group(2), 16)
    return out


def parse_collect_maps():
    """item number -> collect-map number (dComIfGs_onGetCollectMap), from d_item.cpp."""
    text = open(D_ITEM, encoding="utf-8").read()
    table = text[text.index("ItemGetFunc* item_func_ptr"):]
    table = table[:table.index("};")]
    funcs = {int(n, 16): f for n, f in re.findall(r"/\* 0x([0-9A-F]{2}) \*/ (\w+),", table)}
    bodies = {}
    for m in re.finditer(r"^void (item_func_collectmap\d+)\(\) \{\n\s*dComIfGs_onGetCollectMap\((\d+)\);", text, re.M):
        bodies.setdefault(m.group(1), int(m.group(2)))
    return {item: bodies[f] for item, f in funcs.items() if f in bodies}


def switch_no(ap_bit):
    """Archipelago reads a stage's switches as 10 big-endian bytes from mSwitch[0]; its bit k is byte
    9 - k/8, bit k%8. The game's switch number of that bit (dSv_memBit_c::isSwitch)."""
    byte = 9 - ap_bit // 8
    word, in_word = divmod(byte, 4)
    return word * 32 + (3 - in_word) * 8 + ap_bit % 8


def chart_complete_index(salvage_bit):
    """Archipelago reads the salvage bits as a big-endian u64 at dSv_player_map_c::field_0x0[3][0]
    (isCompleteMap's words); its bit k is that word pair's bit. The isCompleteMap index of that bit."""
    return 32 + salvage_bit if salvage_bit < 32 else salvage_bit - 32


def c_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def build(ap_dir, wwrando_dir):
    locs = parse_locations(os.path.join(ap_dir, "Locations.py"))
    salvage = parse_dict(os.path.join(ap_dir, "Locations.py"), "ISLAND_NAME_TO_SALVAGE_BIT")
    island_names = parse_dict(os.path.join(ap_dir, "randomizers/Charts.py"), "ISLAND_NUMBER_TO_NAME")
    island_charts = parse_dict(os.path.join(ap_dir, "Items.py"), "ISLAND_NUMBER_TO_CHART_NAME")
    chart_items = parse_chart_items(os.path.join(ap_dir, "Items.py"))
    collect = parse_collect_maps()
    errors = []

    rows = []
    for name, (kind, stage, bit, addr) in locs.items():
        if " - " not in name:
            continue  # "Defeat Ganondorf": not a tracker location
        general, detailed = name.split(" - ", 1)
        note = " /* unverified */" if name in UNVERIFIED else ""
        if kind == "CHEST":
            row = ("CHEST", stage, bit)
        elif kind == "SWTCH":
            row = ("SWITCH", stage, switch_no(bit))
        elif kind == "PCKUP":
            row = ("PICKUP", stage, bit)
        elif kind == "EVENT":
            idx = addr - EVENT
            if not 0 <= idx < 0x100:
                errors.append(f"{name}: event address {addr:#x} outside the event bits")
                continue
            row = ("EVENT", 0, (idx << 8) | (1 << bit))
        elif kind == "BOCTO":
            idx, odd = divmod(addr - OCEAN, 2)
            if odd or not 0 <= idx < 50:
                errors.append(f"{name}: ocean address {addr:#x} not a u16 of dSv_ocean_c")
                continue
            row = ("OCEAN", idx, bit)
        elif kind == "SPECL":
            if addr is not None:
                # Lenzo's assistant (& 6, or & 7 which includes it) and three letters (& 3) need every bit
                # of a mask set in one event byte
                mask = 0x6 if "Lenzo" in name else 0x3
                row = ("EVENT_MASK", addr - EVENT, mask)
            elif "Ankle" in name:
                row = ("SPECIAL", 0, 0)  # PC_TRACKER_SPECIAL_ANKLE
            elif "Maggie" in name:
                row = ("SPECIAL", 1, 0)  # PC_TRACKER_SPECIAL_MAGGIE
            else:
                errors.append(f"{name}: unknown special location")
                continue
        elif kind == "CHART":
            continue  # sunken treasure: the chart table below, resolved by the tracker's chart mapping
        else:
            errors.append(f"{name}: unknown type {kind}")
            continue
        rows.append((general, detailed, row, note))

    charts = []
    for number in sorted(island_charts):
        chart = island_charts[number]
        island = island_names[number]
        item = chart_items.get(chart)
        if item is None or item not in collect:
            errors.append(f"{chart}: no item number or collect map")
            continue
        index = collect[item] - 1
        if chart_complete_index(salvage[island]) != index:
            errors.append(f"{chart} (island {number} {island}): salvage bit {salvage[island]} is complete-map "
                          f"index {chart_complete_index(salvage[island])}, d_item.cpp says {index}")
        charts.append((chart, index, number, island))
    # the special charts and the triforce charts not tied to an island are items only
    specials = []
    for chart, item in sorted(chart_items.items(), key=lambda kv: kv[0]):
        if chart in island_charts.values():
            continue
        if item not in collect:
            errors.append(f"{chart}: item {item:#x} has no collect map")
            continue
        specials.append((chart, collect[item] - 1))

    untracked = []
    if wwrando_dir:
        text = open(os.path.join(wwrando_dir, "logic/item_locations.txt"), encoding="utf-8").read()
        names = re.findall(r"^([^\s#][^\n]*):\s*$", text, re.M)
        have = {f"{g} - {d}" for g, d, _, _ in rows} | {f"{i} - Sunken Treasure" for _, _, _, i in charts}
        untracked = [n for n in names if n not in have]
        extra = sorted(have - set(names))
        if extra:
            errors.append("not wwrando locations: " + ", ".join(extra))

    return rows, charts, specials, untracked, errors


def render(rows, charts, specials, untracked, ap_rev):
    out = [
        "// Generated by native/tools/gen_tracker_tables.py from Archipelago's worlds/tww"
        + (f" ({ap_rev})" if ap_rev else "") + "; do not edit.",
        "// docs/TRACKER.md. PC_TRACKER_LOCATION(general, detailed, kind, a, b):",
        "//   CHEST a=stage b=bit (isTbox), SWITCH a=stage b=switch, PICKUP a=stage b=item bit,",
        "//   EVENT b=event bit (isEventBit), EVENT_MASK a=event byte b=mask (all set), OCEAN a=grid b=bit,",
        "//   SPECIAL a=PcTrackerSpecial.",
        "#ifdef PC_TRACKER_LOCATION",
    ]
    for general, detailed, (kind, a, b), note in rows:
        out.append(f"PC_TRACKER_LOCATION({c_str(general)}, {c_str(detailed)}, {kind}, {a:#x}, {b:#x}){note}")
    out += ["#endif", "", "// PC_TRACKER_CHART(name, complete-map index, vanilla island number, island name)",
            "#ifdef PC_TRACKER_CHART"]
    for chart, index, number, island in charts:
        out.append(f"PC_TRACKER_CHART({c_str(chart)}, {index}, {number}, {c_str(island)})")
    out += ["#endif", "", "// PC_TRACKER_SPECIAL_CHART(name, collect-map index): charts not tied to an island",
            "#ifdef PC_TRACKER_SPECIAL_CHART"]
    for chart, index in specials:
        out.append(f"PC_TRACKER_SPECIAL_CHART({c_str(chart)}, {index})")
    out.append("#endif")
    if untracked:
        out += ["", "// wwrando locations without a known flag (left to the tracker's manual marks):"]
        out += [f"//   {n}" for n in untracked]
    return "\n".join(out) + "\n"


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--ap", required=True, help="Archipelago's worlds/tww directory")
    p.add_argument("--ap-rev", default="", help="the Archipelago commit, for the header")
    p.add_argument("--wwrando", help="a wwrando checkout, to check the names")
    p.add_argument("--check", action="store_true")
    args = p.parse_args()
    rows, charts, specials, untracked, errors = build(args.ap, args.wwrando)
    for e in errors:
        print("error:", e, file=sys.stderr)
    if errors:
        return 1
    text = render(rows, charts, specials, untracked, args.ap_rev)
    if args.check:
        same = os.path.exists(OUT) and open(OUT, encoding="utf-8").read() == text
        print("up to date" if same else f"{OUT} differs")
        return 0 if same else 1
    with open(OUT, "w", encoding="utf-8") as f:
        f.write(text)
    print(f"{len(rows)} locations, {len(charts)} island charts, {len(specials)} other charts, "
          f"{len(untracked)} untracked -> {os.path.relpath(OUT, ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
