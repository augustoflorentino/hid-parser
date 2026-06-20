import collections
import subprocess
import sys

from hidtools.hid import ReportDescriptor

TYPES = {"INPUT": "I", "OUTPUT": "O", "FEATURE": "F"}


def field_slots(field, kind, rid):
    head = (kind, rid, field.start)
    if not field.type & 0x3:
        if not field.usages:
            return [head + (field.size * field.count, "pad")]
        usages = ",".join("0x%08x" % u for u in field.usages)
        return [head + (field.size, field.count, field.logical_min, usages)]
    if not field.usage:
        return [head + (field.size * field.count, "pad")]
    return [head + (field.size, 1, field.logical_min, "0x%08x" % field.usage)]


def theirs(path):
    rdesc = ReportDescriptor.from_bytes(open(path, "rb").read())
    out = []
    for reports in (rdesc.input_reports, rdesc.output_reports, rdesc.feature_reports):
        for rid, report in reports.items():
            for field in report.fields:
                out.extend(field_slots(field, TYPES[report.type.name], max(rid, 0)))
    return out


def ours(dump, path):
    run = subprocess.run([dump, "--slots", path], capture_output=True, text=True)
    if run.returncode != 0:
        raise ValueError(run.stderr.strip())
    out = []
    for line in run.stdout.splitlines():
        p = line.split()
        if p[-1] == "pad":
            out.append((p[0], int(p[1]), int(p[2]), int(p[3]), "pad"))
        else:
            out.append((p[0], int(p[1]), int(p[2]), int(p[3]), int(p[4]), int(p[5]), p[6]))
    return out


def usage_max_only(slot):
    return len(slot) == 7 and slot[4] == 1 and slot[6].endswith("0000")


def quirk_key(slot):
    if usage_max_only(slot):
        return (slot[0], slot[1], slot[2], slot[3], "pad")
    return slot


def compare(dump, path):
    try:
        a = theirs(path)
    except Exception as e:
        a = "rejected"
    try:
        b = ours(dump, path)
    except ValueError:
        b = "rejected"
    if a == "rejected" or b == "rejected":
        return a == b, "both reject" if a == b else "one rejects"
    if collections.Counter(a) == collections.Counter(b):
        return True, "exact"
    if collections.Counter(map(quirk_key, a)) == collections.Counter(map(quirk_key, b)):
        return True, "hidtools ignores Usage Maximum without Usage Minimum"
    return False, "differs"


def main():
    dump = sys.argv[1]
    tally = collections.Counter()
    for path in sys.argv[2:]:
        ok, why = compare(dump, path)
        tally[why] += 1
        if not ok:
            print("%s: %s" % (why, path))
    for why, n in sorted(tally.items()):
        print("%5d %s" % (n, why))
    return 0 if tally["differs"] == 0 and tally["one rejects"] == 0 else 1


sys.exit(main())
