import re

data = open(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", "rb").read()
matches = set(re.findall(rb"\.\?AV[A-Za-z0-9_]*Event[A-Za-z0-9_]*@@", data))
for m in sorted(matches):
    print(m.decode("latin-1"))
