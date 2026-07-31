import json, re, sys
p = r"C:/Users/win11/.grok/sessions/F%3A%5CUnreal%20Projects%5Cnewtest%5Cmp/019f763a-6794-7f42-ad74-07a0b5659ff4/mcp/call-f3116d8f-2139-4e6b-b199-6fc6f5e3b690-36.json"
d = open(p, encoding="utf-8").read()
names = sorted(set(re.findall(r'"name":"([^"]+)"', d)))
for n in names:
    if any(k in n for k in ("CVar", "Console", "Exec", "Command", "SetC")):
        print(n)
print("--- total", len(names))
