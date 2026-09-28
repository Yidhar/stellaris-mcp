import json

with open(r"\\.\pipe\stellaris_mcp_bridge", "r+b", buffering=0) as pipe:
    pipe.write(b'{"jsonrpc":"2.0","id":1,"method":"get_planet_details","params":{"planet_id":3}}\n')
    line = pipe.readline().decode()
    res = json.loads(line).get("result", {})
    print("name:", res.get("name"))
    print("system_name:", res.get("system_name"))
    print("is_capital:", res.get("is_capital"))
    print("kpi:", res.get("kpi"))
    print("monthly_population_summary:", res.get("monthly_population_summary"))
    print("workforce_summary:", res.get("workforce_summary"))
    print("population_breakdown:", res.get("population_breakdown"))
