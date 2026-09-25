import sys

SOURCE_PATH = r"d:\stellarismcp\source\stellaris_4.5_source.cpp"

targets = [
    "CCountryCreateSpeciesModTemplate",
    "CCountryDeleteSpeciesModTemplate",
    "CCountryUpdateSpeciesModTemplate",
    "CCreateSpeciesModSpecialProjectCommand"
]

print("Searching for targets in source...")
with open(SOURCE_PATH, "r", encoding="utf-8", errors="ignore") as f:
    for line_idx, line in enumerate(f, 1):
        for t in targets:
            if t in line:
                print(f"Line {line_idx} [{t}]: {line.strip()[:140]}")
