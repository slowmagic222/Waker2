#!/usr/bin/env python3
"""The in-game seed generator's side of the Wind Waker Randomizer (docs/SEEDGEN.md).

The game (native/src/pc/features/pc_seedgen.cpp) runs this script with the bundled Python and
reads its standard output: one record per line, fields separated by tabs, so the C++ side needs no
JSON or YAML parser. wwrando (LagoLunatic/wwrando) itself is unchanged; this script only drives it
the way its own GUI does, headless.

  waker2_seedgen.py describe --wwrando DIR [--models DIR]
      Every option the menu shows, from wwrando's Options class and its GUI's labels, plus the
      starting gear, the player models in --models and their colors and color presets.
  waker2_seedgen.py generate --wwrando DIR --settings FILE --clean-iso ISO --output DIR [--models DIR] [--dry]
      Randomizes with the settings file the menu wrote and prints the progress, then the output ISO
      (--dry: the logs only, without reading or writing a disc).
  waker2_seedgen.py permalink --wwrando DIR --settings FILE --permalink TEXT
      Prints the settings a permalink holds (to import a race's permalink into the menu).

Records (describe):
  version  <wwrando version>
  section  <name>
  option   <name> <kind> <default> <min> <max> <label> <description>
           kind: bool, int, enum, model, preset
  choice   <option> <value> <description>
  gear     <item> <max count> <default count>
  model    <name> <casual clothes allowed 0/1>
  color    <model> <hero|casual> <color name> <RRGGBB>
  preset   <model> <hero|casual> <preset name> <color>=<RRGGBB>,...
Records (generate):
  progress <percent 0-100> <text>
  done     <output ISO> <permalink> <seed> <seed hash>
  error    <message>
Records (permalink):
  set      <key> <value>          (the settings file's keys, below)
  error    <message>

The settings file (written by the game, <user dir>/rando/seedgen.ini): NAME=value lines, '#'
comments. NAME is a wwrando option (bool 0/1, int, enum value, model or preset name), `seed`,
`gear.<item>` (how many of that item to start with) or `color.<color name>` (RRGGBB). Anything
missing keeps wwrando's default.
"""

import argparse
import os
import re
import sys
import traceback
import xml.etree.ElementTree as ET


def out(*fields):
    print("\t".join(clean(f) for f in fields), flush=True)


def clean(value) -> str:
    """One field: no tabs or newlines, wwrando's HTML descriptions as plain text."""
    text = str(value)
    text = re.sub(r"<br\s*/?>", " ", text)
    text = re.sub(r"<[^>]+>", "", text)
    text = text.replace("&amp;", "&").replace("&lt;", "<").replace("&gt;", ">")
    return re.sub(r"\s+", " ", text).strip()


def setup_wwrando(wwrando_dir: str, models_dir: str | None):
    """Imports wwrando from its source folder, with the custom models taken from models_dir."""
    wwrando_dir = os.path.abspath(wwrando_dir)
    sys.path.insert(0, os.path.join(wwrando_dir, "gclib"))
    sys.path.insert(0, wwrando_dir)
    import wwrando_paths
    if models_dir:
        # customizer.py reads CUSTOM_MODELS_PATH when it is imported: set it first.
        wwrando_paths.CUSTOM_MODELS_PATH = os.path.abspath(models_dir)
    return wwrando_dir


# ---- describe ---------------------------------------------------------------------------------------

def gui_labels(wwrando_dir: str) -> dict[str, str]:
    """The text wwrando's own window shows for each option (its Qt Designer files)."""
    labels: dict[str, str] = {}
    for ui in ("randomizer_window.ui", "cosmetic_tab.ui"):
        path = os.path.join(wwrando_dir, "wwr_ui", ui)
        if not os.path.isfile(path):
            continue
        for widget in ET.parse(path).iter("widget"):
            name = widget.get("name", "")
            text = None
            for prop in widget.findall("property"):
                if prop.get("name") == "text":
                    s = prop.find("string")
                    text = s.text if s is not None else None
            if not text:
                continue
            text = text.strip().rstrip(":")
            if name.startswith("label_for_"):
                labels.setdefault(name[len("label_for_"):], text)
            elif widget.get("class") == "QCheckBox":
                labels[name] = text
    return labels


def option_sections(wwrando_dir: str) -> dict[str, str]:
    """option name -> the `#region` of options/wwrando_options.py it is in."""
    sections: dict[str, str] = {}
    region = ""
    with open(os.path.join(wwrando_dir, "options", "wwrando_options.py"), encoding="utf-8") as f:
        for line in f:
            m = re.match(r"\s*#region\s+(.*)", line)
            if m:
                region = m.group(1).strip()
                continue
            m = re.match(r"\s*([a-z_0-9]+)\s*:\s*[^=]+=\s*option\(", line)
            if m:
                sections[m.group(1)] = region
    return sections


def describe(args):
    wwrando_dir = setup_wwrando(args.wwrando, args.models)
    from enum import StrEnum
    from version import VERSION
    from options.wwrando_options import Options
    from wwr_ui.inventory import INVENTORY_ITEMS, DEFAULT_STARTING_ITEMS
    import customizer

    out("version", VERSION)
    labels = gui_labels(wwrando_dir)
    sections = option_sections(wwrando_dir)
    section = None
    for opt in Options.all():
        if opt.hidden or opt.name in ("dry_run", "custom_colors", "randomized_gear", "starting_gear"):
            continue
        if sections.get(opt.name, "") != section:
            section = sections.get(opt.name, "")
            out("section", section)
        label = labels.get(opt.name) or opt.name.replace("progression_", "").replace("_", " ").capitalize()
        default = opt.default
        lo = "" if opt.minimum is None else opt.minimum
        hi = "" if opt.maximum is None else opt.maximum
        if opt.name == "custom_player_model":
            kind = "model"
        elif opt.name == "custom_color_preset":
            kind = "preset"
        elif issubclass(opt.type, bool):
            kind, default = "bool", int(default)
        elif issubclass(opt.type, StrEnum):
            kind = "enum"
        elif issubclass(opt.type, int):
            kind = "int"
        else:
            continue  # a type the menu has no row for
        out("option", opt.name, kind, default, lo, hi, label, opt.description)
        if kind == "enum":
            for value in opt.type:
                out("choice", opt.name, value.value, opt.choice_descriptions.get(value, ""))

    for item in sorted(set(INVENTORY_ITEMS)):
        out("gear", item, INVENTORY_ITEMS.count(item), DEFAULT_STARTING_ITEMS.count(item))

    models = ["Link"] + customizer.get_all_custom_model_names()
    for name in models:
        meta = customizer.get_model_metadata(name)
        if "error_message" in meta:
            print(f"[seedgen] model {name}: {meta['error_message']}", file=sys.stderr)
            continue
        out("model", name, 0 if meta.get("disable_casual_clothes", False) else 1)
        for prefix in ("hero", "casual"):
            for color_name, rgb in meta.get(prefix + "_custom_colors", {}).items():
                out("color", name, prefix, color_name, "%02X%02X%02X" % tuple(rgb))
            for preset_name, preset in meta.get(prefix + "_color_presets", {}).items():
                colors = ",".join("%s=%02X%02X%02X" % (c, *rgb) for c, rgb in preset.items())
                out("preset", name, prefix, preset_name, colors)
    for name in ("Random", "Random (exclude Link)"):
        out("model", name, 1)


# ---- generate ---------------------------------------------------------------------------------------

def read_settings(path: str) -> dict[str, str]:
    values: dict[str, str] = {}
    if path and os.path.isfile(path):
        with open(path, encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#") or "=" not in line:
                    continue
                key, value = line.split("=", 1)
                values[key.strip()] = value.strip()
    return values


def make_options(values: dict[str, str]):
    from enum import StrEnum
    from options.wwrando_options import Options
    from wwr_ui.inventory import INVENTORY_ITEMS, DEFAULT_STARTING_ITEMS

    options = Options()
    by_name = Options.by_name()
    for key, value in values.items():
        opt = by_name.get(key)
        if opt is None or opt.name in ("custom_colors", "randomized_gear", "starting_gear", "dry_run"):
            continue
        if issubclass(opt.type, bool):
            options[key] = value in ("1", "true", "True")
        elif issubclass(opt.type, StrEnum):
            options[key] = opt.type(value)
        elif issubclass(opt.type, int):
            options[key] = int(value)
        else:
            options[key] = value

    # Starting gear: gear.<item>=<count>; everything else of the inventory is randomized.
    if any(k.startswith("gear.") for k in values):
        starting = []
        for item in sorted(set(INVENTORY_ITEMS)):
            count = values.get("gear." + item)
            count = DEFAULT_STARTING_ITEMS.count(item) if count is None else int(count)
            starting += [item] * max(0, min(count, INVENTORY_ITEMS.count(item)))
        randomized = INVENTORY_ITEMS.copy()
        for item in starting:
            randomized.remove(item)
        options.starting_gear = sorted(starting)
        options.randomized_gear = sorted(randomized)

    colors = {}
    for key, value in values.items():
        if key.startswith("color.") and re.fullmatch(r"[0-9A-Fa-f]{6}", value):
            n = int(value, 16)
            colors[key[len("color."):]] = [(n >> 16) & 0xFF, (n >> 8) & 0xFF, n & 0xFF]
    options.custom_colors = colors
    return options


class CmdLineArgs:
    """What WWRandomizer reads from wwrando.py's command line, all off."""
    dry = disassemble = exportfolder = nologs = bulk = printflags = noitemrando = False
    mapselect = heap = False
    test = None


# Randomizer+ (docs/RANDOMIZER_PLUS.md): progression only where the generated world has the place. Its land has the
# houses, caves, fountains, dungeon doors and NPCs, but not yet the islands' puzzle chests and landmarks (the Wind
# Shrine, Tingle's tower, the lighthouse...), the open sea's platforms, submarines and ships, or a mailbox; their
# locations get no progress items, so a seed stays beatable.
RPLUS_OFF = (
    "progression_island_puzzles", "progression_misc", "progression_free_gifts", "progression_expensive_purchases",
    "progression_minigames", "progression_short_sidequests", "progression_long_sidequests", "progression_mail",
    "progression_platforms_rafts", "progression_submarines", "progression_big_octos_gunboats",
    "progression_eye_reef_chests", "progression_triforce_charts", "progression_treasure_charts",
)


def rplus_options(options):
    for name in RPLUS_OFF:
        options[name] = False
    return options


def generate(args):
    setup_wwrando(args.wwrando, args.models)
    from randomizer import WWRandomizer
    from seedgen import seedgen

    values = read_settings(args.settings)
    options = make_options(values)
    seed = values.get("seed", "").strip()
    if args.permalink:
        seed, options = WWRandomizer.decode_permalink(args.permalink, options)
    if args.rplus:
        options = rplus_options(options)
    if not seed:
        seed = seedgen.make_random_seed_name()
    os.makedirs(args.output, exist_ok=True)

    out("progress", 0, "Preparing...")
    cmd = CmdLineArgs()
    cmd.dry = args.dry
    rando = WWRandomizer(seed=seed, clean_iso_path=args.clean_iso, randomized_output_folder=args.output,
                         options=options, cmd_line_args=cmd)
    total = max(1, rando.get_max_progress_length())
    try:
        last = -1
        for text, done in rando.randomize():
            percent = min(99, int(done * 100 / total))
            if percent != last or text:
                out("progress", percent, text)
                last = percent
    except Exception as e:
        rando.write_error_log(f"Error on seed {rando.seed}:\n{e}\n\n{traceback.format_exc()}")
        raise
    iso = os.path.join(args.output, "WW Random %s.iso" % rando.seed)
    out("progress", 100, "Done")
    out("done", os.path.abspath(iso), rando.permalink, rando.seed, rando.seed_hash)


def permalink(args):
    setup_wwrando(args.wwrando, None)
    from enum import StrEnum
    from randomizer import WWRandomizer
    from options.wwrando_options import Options
    from wwr_ui.inventory import INVENTORY_ITEMS

    seed, options = WWRandomizer.decode_permalink(args.permalink, make_options(read_settings(args.settings)))
    out("set", "seed", seed)
    for opt in Options.all():
        if not opt.permalink or opt.hidden:
            continue
        value = options[opt.name]
        if opt.name == "starting_gear":
            for item in sorted(set(INVENTORY_ITEMS)):
                out("set", "gear." + item, value.count(item))
        elif isinstance(value, bool):
            out("set", opt.name, int(value))
        elif isinstance(value, (int, StrEnum)):
            out("set", opt.name, value.value if isinstance(value, StrEnum) else value)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = parser.add_subparsers(dest="command", required=True)
    for name in ("describe", "generate", "permalink"):
        p = sub.add_parser(name)
        p.add_argument("--wwrando", required=True, help="wwrando's source folder")
        p.add_argument("--models", help="custom player models folder (default: wwrando's models/)")
        if name in ("generate", "permalink"):
            p.add_argument("--settings", default="", help="the menu's settings file")
            p.add_argument("--permalink", default="", required=name == "permalink")
        if name == "generate":
            p.add_argument("--clean-iso", required=True)
            p.add_argument("--output", required=True)
            p.add_argument("--dry", action="store_true", help="logs only, no ISO (a test without a disc)")
            p.add_argument("--rplus", action="store_true",
                           help="for a Randomizer+ world: no progress items where the world has no place yet")
    args = parser.parse_args()
    try:
        {"describe": describe, "generate": generate, "permalink": permalink}[args.command](args)
    except Exception as e:
        traceback.print_exc()
        out("error", str(e) or type(e).__name__)
        sys.exit(1)


if __name__ == "__main__":
    main()
