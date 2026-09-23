with open(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\interface\situation_log.gui", "r", encoding="utf-8", errors="ignore") as f:
    for num, line in enumerate(f, 1):
        if "log_window" in line or "entries" in line:
            print(f"{num}: {line.strip()}")
