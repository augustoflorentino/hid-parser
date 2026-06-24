#!/usr/bin/env python3
import argparse
import hashlib
import re
import subprocess
import sys
import urllib.request
from pathlib import Path

HID_DEBUG_TAG = "v7.1"
HID_DEBUG_URL = f"https://api.github.com/repos/torvalds/linux/contents/drivers/hid/hid-debug.c?ref={HID_DEBUG_TAG}"
HID_DEBUG_SHA256 = "c08501f41cab1544e21ec4a502fccd1a4db1f55755dfb5bcf56b87e0335fd569"

TYPES = {"INPUT": "I", "OUTPUT": "O", "FEATURE": "F"}
FLAG_NAMES = [
    (0x001, "Constant ", ""), (0x002, "Variable ", "Array "), (0x004, "Relative ", "Absolute "),
    (0x008, "Wrap ", ""), (0x010, "NonLinear ", ""), (0x020, "NoPreferredState ", ""),
    (0x040, "NullState ", ""), (0x080, "Volatile ", ""), (0x100, "BufferedByte ", ""),
]
UNIT_SYSTEMS = ["None", "SI Linear", "SI Rotation", "English Linear", "English Rotation"]
UNIT_NAMES = [
    ["None"] * 8,
    ["None", "Centimeter", "Gram", "Seconds", "Kelvin", "Ampere", "Candela", "None"],
    ["None", "Radians", "Gram", "Seconds", "Kelvin", "Ampere", "Candela", "None"],
    ["None", "Inch", "Slug", "Seconds", "Fahrenheit", "Ampere", "Candela", "None"],
    ["None", "Degrees", "Slug", "Seconds", "Fahrenheit", "Ampere", "Candela", "None"],
]
ENTRY = re.compile(r'\{\s*(0x[0-9a-fA-F]+|\d+)\s*,\s*(0x[0-9a-fA-F]+|\d+)\s*,\s*"([^"]*)"\s*\}')


def load_usage_table(path):
    if path:
        source = Path(path).read_bytes()
    else:
        request = urllib.request.Request(HID_DEBUG_URL, headers={"Accept": "application/vnd.github.raw"})
        with urllib.request.urlopen(request) as response:
            source = response.read()
        if hashlib.sha256(source).hexdigest() != HID_DEBUG_SHA256:
            sys.exit(f"hid-debug.c checksum mismatch for {HID_DEBUG_TAG}")
    text = source.decode("utf-8", "replace")
    start = text.index("hid_usage_table[]")
    block = text[start:text.index("\n};", start)]
    return [(int(p, 0), int(u, 0), name) for p, u, name in ENTRY.findall(block)]


class KernelNames:
    def __init__(self, table):
        self.table = table
        self.first = {}
        for index, (page, _, _) in enumerate(table):
            self.first.setdefault(page, index)

    def page(self, page):
        index = self.first.get(page)
        return self.table[index][2] if index is not None else f"{page:04x}"

    def modifier(self, index, page, wanted):
        for entry_page, usage, name in self.table[index:]:
            if entry_page == page and usage == wanted:
                return name
        return None

    def usage(self, usage):
        page = usage >> 16
        actual = usage & 0xFFFF
        index = self.first.get(page)
        if index is None:
            return f"{self.page(page)}.{actual:04x}"
        modifier = self.modifier(index, page, usage & 0xF000) if page == 0x20 and usage & 0xF000 else None
        if modifier:
            actual &= 0x0FFF
        for _, entry_usage, name in self.table[index + 1:]:
            if entry_usage == 0:
                break
            if entry_usage == actual:
                return f"{self.page(page)}.{name}{modifier or ''}"
        return f"{self.page(page)}.{usage & 0xFFFF:04x}"


def flags_text(flags):
    return "Flags( " + "".join(on if flags & bit else off for bit, on, off in FLAG_NAMES) + ")"


def unit_nibble(nibble):
    return -((0x7 & ~(nibble & 0x7)) + 1) if nibble & 0x8 else nibble & 0x7


def unit_text(unit):
    system = unit & 0xF
    if system > 4:
        return "Unit(Invalid)"
    parts = []
    data = unit >> 4
    for i in range(1, 8):
        nibble = data & 0xF
        data >>= 4
        if nibble:
            parts.append(UNIT_NAMES[system][i] + ("" if nibble == 1 else f"^{unit_nibble(nibble)}"))
    return f"Unit({UNIT_SYSTEMS[system]} : " + "*".join(parts) + ")"


def parse_dump(text):
    lines = text.split("\n")
    descriptor = bytes(int(x, 16) for x in lines[0].split())
    fields = []
    report = None
    field = None
    pending_usages = 0
    for line in lines[1:]:
        stripped = line.strip()
        if not stripped:
            if fields or report:
                break
            continue
        if pending_usages:
            field["usages"].append(stripped)
            pending_usages -= 1
            continue
        match = re.fullmatch(r"(INPUT|OUTPUT|FEATURE)(?:\((\d+)\))?\[\w+\]", stripped)
        if match:
            report = (TYPES[match.group(1)], int(match.group(2) or 0))
            continue
        if re.fullmatch(r"Field\(\d+\)", stripped):
            field = {"type": report[0], "id": report[1], "usages": [], "lines": {}}
            fields.append(field)
            continue
        match = re.fullmatch(r"Usage\((\d+)\)", stripped)
        if match:
            pending_usages = int(match.group(1))
            continue
        key = stripped.split("(", 1)[0]
        field["lines"][key] = stripped
    return descriptor, fields


def our_fields(hiddump, descriptor):
    hex_text = " ".join(f"{b:02x}" for b in descriptor)
    run = subprocess.run([hiddump, "--fields", "--hex"], input=hex_text.encode(), capture_output=True)
    if run.returncode != 0:
        sys.exit(f"hiddump failed: {run.stderr.decode().strip()}")
    fields = []
    for line in run.stdout.decode().splitlines():
        parts = line.split()
        if len(parts) != 14:
            continue
        kind, rid, bit, size, count, flags, lmin, lmax, pmin, pmax, exp, unit, n, usages = parts
        fields.append({
            "type": kind, "id": int(rid), "bit": int(bit), "size": int(size), "count": int(count),
            "flags": int(flags), "lmin": int(lmin), "lmax": int(lmax), "pmin": int(pmin),
            "pmax": int(pmax), "exp": int(exp), "unit": int(unit, 16), "n": int(n),
            "usages": [] if usages == "-" else [int(u, 16) for u in usages.split(",")],
        })
    return fields


def expected_lines(field, names):
    offset = field["bit"] - (8 if field["id"] else 0)
    usages = list(field["usages"])
    while len(usages) < field["count"]:
        usages.append(usages[-1])
    lines = {
        "Report Size": f"Report Size({field['size']})",
        "Report Count": f"Report Count({field['count']})",
        "Report Offset": f"Report Offset({offset})",
        "Flags": flags_text(field["flags"]),
    }
    if field["lmin"] != field["lmax"]:
        lines["Logical Minimum"] = f"Logical Minimum({field['lmin']})"
        lines["Logical Maximum"] = f"Logical Maximum({field['lmax']})"
    if field["pmin"] != field["pmax"]:
        lines["Physical Minimum"] = f"Physical Minimum({field['pmin']})"
        lines["Physical Maximum"] = f"Physical Maximum({field['pmax']})"
    if field["exp"]:
        lines["Unit Exponent"] = f"Unit Exponent({field['exp']})"
    if field["unit"]:
        lines["Unit"] = unit_text(field["unit"])
    return offset, [names.usage(u) for u in usages], lines


COMPARED = ["Report Size", "Report Count", "Logical Minimum", "Logical Maximum", "Physical Minimum",
            "Physical Maximum", "Unit Exponent", "Unit", "Flags"]


def kernel_offset(field):
    match = re.fullmatch(r"Report Offset\((\d+)\)", field["lines"].get("Report Offset", ""))
    return int(match.group(1)) if match else -1


def compare_usages(where, kernel, ours):
    if len(kernel) != len(ours):
        print(f"{where}: kernel has {len(kernel)} usages, hid-parser {len(ours)}")
    for i, (k, o) in enumerate(zip(kernel, ours)):
        if k != o:
            print(f"{where}: usage[{i}] kernel={k} hid-parser={o}")
            return 1
    return int(len(kernel) != len(ours))


def compare_field(where, kernel, ours_offset_usages_lines):
    _, usages, lines = ours_offset_usages_lines
    differences = compare_usages(where, kernel["usages"], usages)
    for key in COMPARED:
        k = kernel["lines"].get(key)
        o = lines.get(key)
        if k != o:
            print(f"{where}: kernel={k or '-'} hid-parser={o or '-'}")
            differences += 1
    return differences


def compare(path, hiddump, names):
    descriptor, kernel = parse_dump(Path(path).read_text(errors="replace"))
    ours = {}
    for field in our_fields(hiddump, descriptor):
        if field["n"] == 0:
            continue
        expected = expected_lines(field, names)
        ours[(field["type"], field["id"], expected[0])] = expected
    differences = 0
    for field in kernel:
        key = (field["type"], field["id"], kernel_offset(field))
        where = f"{path}: {key[0]} id={key[1]} offset={key[2]}"
        if key not in ours:
            print(f"{where}: only in kernel")
            differences += 1
            continue
        differences += compare_field(where, field, ours.pop(key))
    for key in ours:
        print(f"{path}: {key[0]} id={key[1]} offset={key[2]}: only in hid-parser")
        differences += 1
    print(f"{path}: {len(kernel)} kernel fields, {differences} differences")
    return differences


def main():
    parser = argparse.ArgumentParser(description="Compare hid-parser with the kernel HID parser dump in debugfs rdesc files")
    parser.add_argument("--hiddump", default=str(Path(__file__).resolve().parent.parent / "build" / "hiddump"))
    parser.add_argument("--hid-debug", help=f"hid-debug.c to take usage names from (default: download {HID_DEBUG_TAG})")
    parser.add_argument("rdesc", nargs="+")
    args = parser.parse_args()
    names = KernelNames(load_usage_table(args.hid_debug))
    total = sum(compare(path, args.hiddump, names) for path in args.rdesc)
    sys.exit(1 if total else 0)


if __name__ == "__main__":
    main()
