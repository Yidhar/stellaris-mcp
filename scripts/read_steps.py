import json

with open(r'C:/Users/yidhar/.gemini/antigravity/brain/e1ac788d-4f2a-4b8e-af57-b2a4b53a4aa0/.system_generated/logs/transcript.jsonl', 'r', encoding='utf-8') as f:
    for line in f:
        d = json.loads(line)
        idx = d.get('step_index', 0)
        if 17940 <= idx <= 17965:
            if 'content' in d:
                print(f"Step {idx}: {d['content'][:300]}")
            if 'tool_calls' in d:
                print(f"Step {idx} tool: {d['tool_calls'][0].get('name')} {d['tool_calls'][0].get('args')}")
