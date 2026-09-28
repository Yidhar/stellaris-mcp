import json
import time
import sys

PIPE_PATH = r"\\.\pipe\stellaris_mcp_bridge"

def send_request(pipe, req_id: int, method: str, params: dict | None = None) -> dict:
    msg = {
        "jsonrpc": "2.0",
        "method": method,
        "params": params or {},
        "id": req_id
    }
    line = json.dumps(msg) + "\n"
    pipe.write(line.encode("utf-8"))
    pipe.flush()

    while True:
        resp_line = pipe.readline().decode("utf-8").strip()
        if not resp_line:
            continue
        data = json.loads(resp_line)
        if data.get("id") == req_id:
            return data

def main():
    print(f"[*] Connecting to Stellaris Bridge: {PIPE_PATH}")
    with open(PIPE_PATH, "r+b", buffering=0) as pipe:
        print("[+] Connected!")

        # 1. Initial status
        st = send_request(pipe, 1, "get_status")["result"]
        minerals_0 = st["resources"]["minerals"]["stockpile"]
        energy_0 = st["resources"]["energy"]["stockpile"]
        date_0 = st["date"]["formatted"]
        print(f"[*] Initial date: {date_0} | Minerals: {minerals_0:.2f} | Energy: {energy_0:.2f}")

        # 2. Buy minerals
        print("\n[*] Buying 100 Minerals...")
        buy_res = send_request(pipe, 2, "market_trade", {"resource": "minerals", "action": "buy", "units": 100})
        print(f"[*] Buy result: {buy_res}")
        assert buy_res["result"]["success"] is True, "Buy failed!"

        # 3. Unpause and let game advance
        print("\n[*] Unpausing game to allow command execution...")
        send_request(pipe, 3, "set_speed", {"speed": 2})
        send_request(pipe, 4, "set_paused", {"paused": False})
        time.sleep(2)
        send_request(pipe, 5, "set_paused", {"paused": True})

        # 4. Check status after buy
        st = send_request(pipe, 6, "get_status")["result"]
        minerals_1 = st["resources"]["minerals"]["stockpile"]
        energy_1 = st["resources"]["energy"]["stockpile"]
        date_1 = st["date"]["formatted"]
        print(f"[+] After Buy -> Date: {date_1} | Minerals: {minerals_1:.2f} (diff: {minerals_1 - minerals_0:+.2f}) | Energy: {energy_1:.2f} (diff: {energy_1 - energy_0:+.2f})")

        # 5. Sell minerals
        print("\n[*] Selling 100 Minerals...")
        sell_res = send_request(pipe, 7, "market_trade", {"resource": "minerals", "action": "sell", "units": 100})
        print(f"[*] Sell result: {sell_res}")
        assert sell_res["result"]["success"] is True, "Sell failed!"

        # 6. Unpause and let game advance
        print("\n[*] Unpausing game to allow command execution...")
        send_request(pipe, 8, "set_paused", {"paused": False})
        time.sleep(2)
        send_request(pipe, 9, "set_paused", {"paused": True})

        # 7. Check status after sell
        st = send_request(pipe, 10, "get_status")["result"]
        minerals_2 = st["resources"]["minerals"]["stockpile"]
        energy_2 = st["resources"]["energy"]["stockpile"]
        date_2 = st["date"]["formatted"]
        print(f"[+] After Sell -> Date: {date_2} | Minerals: {minerals_2:.2f} (diff: {minerals_2 - minerals_1:+.2f}) | Energy: {energy_2:.2f} (diff: {energy_2 - energy_1:+.2f})")

        print("\n[SUCCESS] All market trade tests passed without crashes!")

if __name__ == "__main__":
    main()
