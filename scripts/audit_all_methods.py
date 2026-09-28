with open(r'd:\stellarismcp\stellaris_bridge\src\ipc_server.cpp', 'r', encoding='utf-8') as f:
    methods = []
    for line in f:
        if 'method ==' in line:
            parts = line.split('==')
            if len(parts) > 1:
                val = parts[1].strip().split()[0].strip('"();{')
                methods.append(val)

print(f"Total IPC methods in bridge: {len(methods)}")
for idx, m in enumerate(methods, 1):
    print(f"{idx:2d}. {m}")
