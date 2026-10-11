#!/usr/bin/env python3
"""Apps > AI's pinned roots (src/network/AiRoots.h): every PEM in GOOGLE_ROOTS and WIKIPEDIA_ROOTS
decodes, and its SHA-256 is the one the header writes for it ("<Name>, SHA-256 ...:" with the
fingerprint on the next line), so a root edited by hand - or a fingerprint comment that no longer describes its PEM - fails the host
suite. Run with the suite (test/CMakeLists.txt); exits 0 when all agree.
"""
import base64
import hashlib
import re
import sys
from pathlib import Path

HEADER = Path(__file__).resolve().parent.parent / "src" / "network" / "AiRoots.h"
EXPECTED = {"GOOGLE_ROOTS": ["GTS Root R1", "GTS Root R4"],
            "WIKIPEDIA_ROOTS": ["ISRG Root X1", "ISRG Root X2", "ISRG Root YE"]}


def main():
    text = HEADER.read_text()
    fingerprints = {}
    for name, fp in re.findall(r"//\s+(GTS Root R\d|ISRG Root \w\w), SHA-256[^\n]*:\n//\s+([0-9A-F:]{95})\n", text):
        fingerprints[name] = fp.replace(":", "").lower()
    failures = []
    for array, names in EXPECTED.items():
        m = re.search(r"inline constexpr char %s\[\] =\n(.*?);\n" % array, text, re.S)
        if not m:
            failures.append("%s: not found" % array)
            continue
        pems = []
        current = None
        for line in m.group(1).splitlines():
            label = re.match(r"\s*//\s*(GTS Root R\d|ISRG Root \w\w)", line)
            if label:
                current = [label.group(1), []]
                pems.append(current)
                continue
            lit = re.match(r'\s*"([^"]*)\\n"\s*$', line)
            if lit and current is not None:
                current[1].append(lit.group(1))
        got = [p[0] for p in pems]
        if got != names:
            failures.append("%s holds %s, expected %s" % (array, got, names))
        for name, lines in pems:
            if not lines or lines[0] != "-----BEGIN CERTIFICATE-----" or lines[-1] != "-----END CERTIFICATE-----":
                failures.append("%s: not one PEM" % name)
                continue
            der = base64.b64decode("".join(lines[1:-1]), validate=True)
            digest = hashlib.sha256(der).hexdigest()
            if fingerprints.get(name) != digest:
                failures.append("%s: SHA-256 %s, the header says %s" % (name, digest, fingerprints.get(name)))
            else:
                print("ok  %-12s %s... (%d bytes)" % (name, digest[:16], len(der)))
    for f in failures:
        print("FAIL " + f)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
