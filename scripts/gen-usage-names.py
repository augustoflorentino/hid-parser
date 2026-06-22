#!/usr/bin/env python3
import hashlib
import json
import subprocess
import sys
import tempfile
import urllib.request
from pathlib import Path

HUT_URL = "https://usb.org/sites/default/files/hut1_7.pdf"
HUT_VERSION = "1.7"
HUT_PDF_SHA256 = "65e85bd8cb146a7ee5c905bf5ab0a12a6d6925d1c866bad609a772fbdfeb8df5"
HUT_JSON_SHA256 = "07e8ca728d78271cb0186482e51479deee32f1ca34016473623173638a2144d8"
HUT_JSON_NAME = "HidUsageTables.json"

PAGES_ONLY_IN_PDF_TABLE = {0x10: "Unicode", 0x92: "Gaming Device"}
PAGE_RANGES = [(0xFF00, 0xFFFF, "Vendor-defined")]

ROOT = Path(__file__).resolve().parent.parent
OUTPUT = ROOT / "src" / "usage_names.c"

def sha256(data):
    return hashlib.sha256(data).hexdigest()


def fetch_pdf(path):
    if path:
        return Path(path).read_bytes()
    request = urllib.request.Request(HUT_URL, headers={"User-Agent": "Mozilla/5.0"})
    with urllib.request.urlopen(request) as response:
        return response.read()


def extract_json(pdf):
    with tempfile.TemporaryDirectory() as tmp:
        pdf_path = Path(tmp) / "hut.pdf"
        json_path = Path(tmp) / HUT_JSON_NAME
        pdf_path.write_bytes(pdf)
        subprocess.run(["pdfdetach", "-savefile", HUT_JSON_NAME, "-o", str(json_path), str(pdf_path)], check=True)
        return json_path.read_bytes()


def load_tables(source):
    data = Path(source).read_bytes() if source and source.endswith(".json") else None
    if data is None:
        pdf = fetch_pdf(source)
        if sha256(pdf) != HUT_PDF_SHA256:
            sys.exit("HUT PDF checksum mismatch: update HUT_URL, HUT_VERSION and the checksums together")
        data = extract_json(pdf)
    if sha256(data) != HUT_JSON_SHA256:
        sys.exit("HUT JSON checksum mismatch")
    return json.loads(data)


def clean(name):
    return name.replace("\\_", "_")


class Pool:
    def __init__(self):
        self.offsets = {}
        self.strings = []
        self.size = 0

    def add(self, text):
        if text not in self.offsets:
            self.offsets[text] = self.size
            self.strings.append(text)
            self.size += len(text.encode("utf-8")) + 1
        return self.offsets[text]


def build(tables):
    pool = Pool()
    pages = []
    usages = []
    known = {page["Id"]: page for page in tables["UsagePages"]}
    for page_id in sorted(set(known) | set(PAGES_ONLY_IN_PDF_TABLE)):
        page = known.get(page_id)
        if page is None:
            pages.append((page_id, pool.add(PAGES_ONLY_IN_PDF_TABLE[page_id]), 0, len(usages), 0, 0, 0))
            continue
        gen = page.get("UsageIdGenerator")
        name = pool.add(clean(page["Name"]))
        first = len(usages)
        for usage in sorted(page["UsageIds"], key=lambda u: u["Id"]):
            usages.append((usage["Id"], pool.add(clean(usage["Name"]))))
        if gen:
            pages.append((page_id, name, pool.add(gen["NamePrefix"]), first, len(usages) - first, gen["StartUsageId"], gen["EndUsageId"]))
        else:
            pages.append((page_id, name, 0, first, len(usages) - first, 0, 0))
    ranges = [(lo, hi, pool.add(name)) for lo, hi, name in PAGE_RANGES]
    return pool, pages, usages, ranges


def char_literals(text):
    out = []
    for byte in text.encode("utf-8"):
        ch = chr(byte)
        if byte >= 0x80:
            out.append(f"'\\x{byte:02x}'")
        elif ch in "'\\":
            out.append("'\\" + ch + "'")
        else:
            out.append("'" + ch + "'")
    return out


def emit_pool(out, pool):
    out.append(f"static const char hidp_usage_pool[{pool.size}] = {{")
    for text in pool.strings:
        out.append("    " + "".join(lit + "," for lit in char_literals(text)) + "0,")
    out.append("};")


def emit_array(out, ctype, name, values, per_line=12):
    out.append(f"static const {ctype} {name}[{max(len(values), 1)}] = {{")
    values = values or [0]
    for i in range(0, len(values), per_line):
        out.append("    " + " ".join(f"{v}," for v in values[i:i + per_line]))
    out.append("};")


def emit_tables(out, pool, pages, usages, ranges):
    offset_type = "uint16_t" if pool.size <= 0xFFFF else "uint32_t"
    emit_pool(out, pool)
    out.append("")
    out.append("typedef struct {")
    out.append("    uint16_t page;")
    out.append(f"    {offset_type} name;")
    out.append(f"    {offset_type} prefix;")
    out.append("    uint16_t first;")
    out.append("    uint16_t count;")
    out.append("    uint16_t gen_first;")
    out.append("    uint16_t gen_last;")
    out.append("} hidp_un_page_t;")
    out.append("")
    out.append("typedef struct {")
    out.append("    uint16_t first;")
    out.append("    uint16_t last;")
    out.append(f"    {offset_type} name;")
    out.append("} hidp_un_range_t;")
    out.append("")
    out.append(f"static const hidp_un_page_t hidp_usage_pages[{len(pages)}] = {{")
    for page_id, name, prefix, first, count, gen_first, gen_last in pages:
        out.append(f"    {{0x{page_id:04X}, {name}, {prefix}, {first}, {count}, {gen_first}, {gen_last}}},")
    out.append("};")
    out.append("")
    out.append(f"static const hidp_un_range_t hidp_usage_page_ranges[{len(ranges)}] = {{")
    for lo, hi, name in ranges:
        out.append(f"    {{0x{lo:04X}, 0x{hi:04X}, {name}}},")
    out.append("};")
    out.append("")
    emit_array(out, "uint16_t", "hidp_usage_ids", [f"0x{u[0]:04X}" for u in usages], 10)
    out.append("")
    emit_array(out, offset_type, "hidp_usage_names", [str(u[1]) for u in usages])


LOOKUP = """
static const hidp_un_page_t *hidp_find_page(uint32_t page)
{
    size_t lo = 0;
    size_t hi = sizeof(hidp_usage_pages) / sizeof(hidp_usage_pages[0]);

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;

        if (hidp_usage_pages[mid].page == page)
            return &hidp_usage_pages[mid];
        if (hidp_usage_pages[mid].page < page)
            lo = mid + 1;
        else
            hi = mid;
    }
    return NULL;
}

static const char *hidp_find_usage(const hidp_un_page_t *entry, uint32_t id)
{
    size_t lo = entry->first;
    size_t hi = (size_t)entry->first + entry->count;

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;

        if (hidp_usage_ids[mid] == id)
            return hidp_usage_pool + hidp_usage_names[mid];
        if (hidp_usage_ids[mid] < id)
            lo = mid + 1;
        else
            hi = mid;
    }
    return NULL;
}

const char *hidp_usage_page_name(uint32_t page)
{
    const hidp_un_page_t *entry = hidp_find_page(page);
    size_t i;

    if (entry != NULL)
        return hidp_usage_pool + entry->name;
    for (i = 0; i < sizeof(hidp_usage_page_ranges) / sizeof(hidp_usage_page_ranges[0]); i++) {
        if (page >= hidp_usage_page_ranges[i].first && page <= hidp_usage_page_ranges[i].last)
            return hidp_usage_pool + hidp_usage_page_ranges[i].name;
    }
    return NULL;
}

const char *hidp_usage_name(uint32_t usage)
{
    const hidp_un_page_t *entry = hidp_find_page(usage >> 16);
    uint32_t id = usage & 0xFFFFu;

    if (entry == NULL)
        return NULL;
    if (entry->gen_last != 0 && id >= entry->gen_first && id <= entry->gen_last)
        return hidp_usage_pool + entry->prefix;
    return hidp_find_usage(entry, id);
}
"""


def main():
    source = sys.argv[1] if len(sys.argv) > 1 else None
    tables = load_tables(source)
    version = f'{tables["UsageTableVersion"]}.{tables["UsageTableRevision"]}'
    if version != HUT_VERSION:
        sys.exit(f"HUT JSON says version {version}, expected {HUT_VERSION}")
    pool, pages, usages, ranges = build(tables)
    out = ['#include "hidp_internal.h"', ""]
    emit_tables(out, pool, pages, usages, ranges)
    out.append(LOOKUP.rstrip())
    OUTPUT.write_text("\n".join(out) + "\n")
    print(f"{OUTPUT}: HUT {version}, {len(pages)} pages, {len(usages)} usages, pool {pool.size} bytes")


if __name__ == "__main__":
    main()
