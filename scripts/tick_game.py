import json, time

PIPE_PATH = r"\\.\pipe\stellaris_mcp_bridge"

with open(PIPE_PATH, "r+b", buffering=0) as pipe:
    # Unpause
    pipe.write(b'{"jsonrpc":"2.0","method":"set_paused","params":{"paused":false},"id":1}\n')
    print("Unpause:", pipe.readline().decode().strip())
    
    # Wait 2 seconds for a few days to tick
    time.sleep(1.5)
    
    # Pause
    pipe.write(b'{"jsonrpc":"2.0","method":"set_paused","params":{"paused":true},"id":2}\n')
    print("Pause:", pipe.readline().decode().strip())
