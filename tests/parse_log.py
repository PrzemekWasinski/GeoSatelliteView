import re
import os

LOG_FILE = os.path.join(os.path.dirname(__file__), "api_test.log")
OUT_FILE = os.path.join(os.path.dirname(__file__), "satellites.cpp")

# matches: 2026-05-20 11:48:39  SUCCESS  GOES18 PNW AirMass
LINE_RE = re.compile(r"SUCCESS\s+GOES(\d+)\s+(\S+)\s+(\S+)")

entries = []
with open(LOG_FILE) as f:
    for line in f:
        m = LINE_RE.search(line)
        if m:
            satellite, sector, product = m.group(1), m.group(2), m.group(3)
            entries.append((satellite, sector, product))

lines = []
lines.append('#include <map>')
lines.append('#include <string>')
lines.append('#include <vector>')
lines.append('')
lines.append('std::vector<std::map<std::string, std::string>> satellites = {')

for satellite, sector, product in entries:
    lines.append(
        f'    {{"Satellite", "GOES{satellite}"}}, {{"Sector", "{sector}"}}, {{"Product", "{product}"}},'
    )

lines.append('};')
lines.append('')

with open(OUT_FILE, "w") as f:
    f.write("\n".join(lines))

print(f"Written {len(entries)} entries to {OUT_FILE}")
