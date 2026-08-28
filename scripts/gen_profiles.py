"""Build Polaris device profiles from this firmware's own personality tables.

Polaris can plan a rig around hardware that is not on the bench, but only if it
knows the footprints, and those come from here. Reading them off a running
device is the most trustworthy source and needs the device; reading them off the
simulator needs only a PC and is worth exactly what the simulator is worth. This
reads the firmware, which is what both of those are copies of.

It is a parser rather than a table, so that regenerating after a personality
change is a command instead of an editing job. Veyron's table has changed five
times.

    python scripts/gen_profiles.py --out ../ravlight-polaris/internal/catalog/profiles

The check that makes the output trustworthy: per-block channel counts are summed
from the channel tables and compared against the footprint each personality
declares beside them. Those are two independent statements in the firmware and
they have disagreed before, when P2, P4 and P6 each declared 13 channels where
the table held 14.
"""

import argparse
import datetime
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
PROBLEMS = []


def note(name, msg):
    PROBLEMS.append("%s: %s" % (name, msg))


def firmware_version():
    text = (ROOT / "include" / "version.h").read_text(encoding="utf-8")
    m = re.search(r'#define\s+FW_VERSION\s+"([^"]+)"', text)
    return m.group(1) if m else "unknown"


def defines(text):
    """Integer #defines, so widths written as expressions can be resolved."""
    return {name: int(value)
            for name, value in re.findall(r"#define\s+(\w+)\s+(\d+)\b", text)}


def resolve(expr, consts):
    """A channel width, as the firmware writes it.

    They are small integer expressions over the pixel-count defines, and some
    have parentheses and division: "(VEYRON_NUM_PIXELS_1 / 2) * 3" is the mirror
    layout. Names are substituted, then what is left has to be arithmetic and
    nothing else before it is evaluated — an unknown identifier is an error
    rather than a zero, because a width that silently became zero would produce
    a footprint that looks plausible and is wrong.

    Division is made integer, since that is what it means in C.
    """
    substituted = re.sub(
        r"[A-Za-z_]\w*",
        lambda m: str(consts[m.group(0)]) if m.group(0) in consts else m.group(0),
        expr)
    if not re.fullmatch(r"[\d\s()+\-*/]+", substituted):
        raise ValueError("cannot resolve channel width %r" % expr)
    return int(eval(substituted.replace("/", "//"), {"__builtins__": {}}, {}))


def channel_tables(text, consts):
    """Every `static const dmx_channel_t NAME[]` as a list of (section, width)."""
    tables = {}
    pattern = r"static\s+const\s+dmx_channel_t\s+(\w+)\[\]\s*=\s*\{(.*?)\n\};"
    for name, body in re.findall(pattern, text, re.S):
        rows = []
        for row in re.findall(r"\{([^{}]*)\}", body):
            fields = [f.strip() for f in row.split(",")]
            if len(fields) < 7:
                continue
            rows.append((fields[2], resolve(fields[-1], consts)))
        tables[name] = rows
    return tables


def sections_of(table, order):
    """Channels per section, in the order the sections are numbered."""
    totals = {}
    for section, width in table:
        totals[section] = totals.get(section, 0) + width
    return [totals[s] for s in order if s in totals]


def personality_rows(text, array_name):
    """The rows of a `static const personality_t NAME[]` array, as field lists."""
    pattern = (r"static\s+const\s+personality_t\s+" + array_name +
               r"\[\]\s*=\s*\{(.*?)\n\};")
    block = re.search(pattern, text, re.S)
    if not block:
        raise ValueError("no %s array found" % array_name)
    return [[f.strip() for f in row.split(",")]
            for row in re.findall(r"\{([^{}]*)\}", block.group(1))]


def veyron():
    base = ROOT / "include" / "fixtures" / "veyron"
    text = (base / "personalities.h").read_text(encoding="utf-8")
    # Widths are written as expressions over the pixel counts, and those live in
    # fixture.h next door rather than in the table itself.
    consts = defines(text)
    consts.update(defines((base / "fixture.h").read_text(encoding="utf-8")))
    tables = channel_tables(text, consts)
    order = ["VEYRON_SEC_STRIP", "VEYRON_SEC_ACCENT", "VEYRON_SEC_STROBE"]

    out = []
    rows = re.findall(
        r'VEYRON_PERSONALITY\(\s*"([^"]+)"\s*,\s*(\d+)\s*,\s*(\w+)\s*\)', text)
    for idx, (name, footprint, table) in enumerate(rows, 1):
        secs = sections_of(tables[table], order)
        if sum(secs) != int(footprint):
            note(name, "channel table sums to %d, footprint says %s"
                 % (sum(secs), footprint))
        out.append({"idx": idx, "name": name, "footprint": int(footprint),
                    "sections": secs})
    return out


def axon():
    text = (ROOT / "include" / "fixtures" / "axon" /
            "personalities.h").read_text(encoding="utf-8")
    tables = channel_tables(text, defines(text))
    out = []
    for idx, fields in enumerate(personality_rows(text, "AXON_PERSONALITIES"), 1):
        name, footprint, table = fields[0].strip('"'), int(fields[1]), fields[2]
        # Every Axon row sits in the one section, so this sums to the footprint
        # of a single block rather than splitting into several.
        if table in tables:
            total = sum(w for _, w in tables[table])
            if total != footprint:
                note(name, "channel table holds %d channels, footprint says %d"
                     % (total, footprint))
        out.append({"idx": idx, "name": name, "footprint": footprint})
    return out


def orion():
    text = (ROOT / "src" / "fixtures" / "orion" /
            "fixture_config.cpp").read_text(encoding="utf-8")
    # No channel table here on purpose: the renderer reads positionStart and
    # controlStart directly, so there is nothing to cross-check against.
    return [{"idx": idx, "name": fields[0].strip('"'), "footprint": int(fields[1])}
            for idx, fields
            in enumerate(personality_rows(text, "ORION_PERSONALITIES"), 1)]


def build():
    v_pers, o_pers, a_pers = veyron(), orion(), axon()

    # The default personality decides the block start addresses, computed from
    # its own sections rather than written out here. The allocator moves them
    # anyway; a profile that starts self-consistent is one you can read and
    # check.
    v_default = 2
    v_secs = next(p["sections"] for p in v_pers if p["idx"] == v_default)

    return [
        {
            "id": "veyron-xdmx2",
            "name": "Veyron — pixel bar, XDMX rev2.2",
            "fixture": "Veyron", "board": "XDMX v2.2",
            "fw_base": "veyron_xdmx2",
            "config": {
                "fixture": {
                    "personality": v_default,
                    "rgbw": 1,
                    "white": 1 + v_secs[0],
                    "function": 1 + v_secs[0] + v_secs[1],
                    "dimCurve": 1,
                    "statusLed": True,
                },
                "dmx": {"universe": 0, "input": 2},
            },
            "personalities": v_pers,
        },
        {
            "id": "orion-lifter-v5",
            "name": "Orion — winch, LED Lifter v5",
            "fixture": "Orion", "board": "LED Lifter v5",
            "fw_base": "orion_led_lifter_v5",
            "config": {
                "fixture": {"personality": 3, "positionStart": 1,
                            "controlStart": 3},
                "dmx": {"universe": 0, "input": 2},
            },
            "personalities": o_pers,
        },
        {
            "id": "axon-node",
            "name": "Axon — Art-Net and sACN to DMX node",
            "fixture": "Axon", "board": "", "fw_base": "axon",
            # No fixture section, because the Axon has none: it has no bridge
            # configuration of its own. The universe it puts on the wire is
            # dmxConfig.startUniverse, and that whole universe is what it takes.
            "config": {
                "fixture": {},
                "dmx": {"universe": 0, "input": 2, "output": True},
            },
            "personalities": a_pers,
        },
    ]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    out_dir = pathlib.Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    fw = firmware_version()
    today = datetime.date.today().isoformat()

    for profile in build():
        profile["captured"] = {
            "from": "firmware source",
            "fw": fw,
            "at": today,
            "note": "generated by scripts/gen_profiles.py from this firmware's "
                    "own personality tables",
        }
        path = out_dir / (profile["id"] + ".json")
        path.write_text(json.dumps(profile, indent=2, ensure_ascii=False) + "\n",
                        encoding="utf-8")
        print("  %-18s %d personalities" % (profile["id"],
                                            len(profile["personalities"])))

    if PROBLEMS:
        print("\nthe firmware disagrees with itself:", file=sys.stderr)
        for p in PROBLEMS:
            print("  " + p, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
