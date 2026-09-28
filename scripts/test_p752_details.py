import json

with open(r"\\.\pipe\stellaris_mcp_bridge", "r+b", buffering=0) as pipe:
    pipe.write(b'{"jsonrpc":"2.0","id":1,"method":"get_planet_details","params":{"planet_id":752}}\n')
    line = pipe.readline().decode()
    res = json.loads(line).get("result", {})
    print("name:", res.get("name"))
    print("system_name:", res.get("system_name"))
    print("is_capital:", res.get("is_capital"))
    print("kpi:", res.get("kpi"))
    print("overview:", res.get("overview"))
