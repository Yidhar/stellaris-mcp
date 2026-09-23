import re

with open(r'E:\Program Files (x86)\Steam\steamapps\common\Stellaris\interface\topbar_species_view.gui', 'r') as f:
    text = f.read()

names = set(re.findall(r'name\s*=\s*"([^"]+)"', text))
for n in sorted(names):
    if any(k in n for k in ['pop', 'species', 'count', 'right', 'button', 'entry']):
        print(n)
