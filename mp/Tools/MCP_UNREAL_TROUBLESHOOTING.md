# Unreal MCP connectivity (UE 5.8 + Grok)

## Architecture

```
Grok
    |  HTTP Streamable MCP
    v
http://127.0.0.1:8010/mcp
    Tools/unreal_mcp_http_proxy.py   (kept alive by _ue_mcp_proxy_watchdog.py)
    |  raw socket + full SSE buffer
    v
http://127.0.0.1:8000/mcp
    Unreal Editor ModelContextProtocol plugin
```

## Why it failed before

Session logs showed:

```text
error sending request for url (http://127.0.0.1:8010/mcp), when send initialize request
```

That is **TCP connection refused** on `:8010` — the HTTP proxy was not running (it dies if started only inside an agent shell Job Object). UE on `:8000` can still be fine.

Also: UE `tools/call` is **SSE without Content-Length**. Clients must buffer until `data:`. The proxy does this.

## Fix in this repo

| Piece | Role |
|-------|------|
| `Tools/unreal_mcp_http_proxy.py` | Compatibility proxy `:8010` → `:8000` |
| `Tools/_ue_mcp_proxy_watchdog.py` | Restarts proxy if it exits |
| `Tools/_spawn_ue_mcp_proxy.py` | WMI-detach so agent shells cannot kill it |
| `Tools/start_unreal_mcp_proxy.ps1` | One-click start |
| `.mcp.json` / config.toml | `url = "http://127.0.0.1:8010/mcp"` |

Optional: `Tools/unreal_mcp_stdio_bridge.py` (stdio path; not used by Grok config right now).

## Every session checklist

1. **Unreal Editor** open — Unreal MCP plugin / `ModelContextProtocol.StartServer` (port **8000**)
2. **Start proxy once** (watchdog keeps it up):

```powershell
powershell -File "Tools/start_unreal_mcp_proxy.ps1"
```

Expect: `OK  http://127.0.0.1:8010/mcp  ->  http://127.0.0.1:8000/mcp`

3. Verify:

```powershell
grok mcp doctor unreal-mcp
# → handshake OK, 3 tools
```

4. **New Grok chat** from project root `mp/`  
   Failed sessions do **not** recover mid-chat.

## Typical errors

| Symptom | Cause | Fix |
|---------|--------|-----|
| `error sending request ... 8010` / handshake failed | Proxy down | `start_unreal_mcp_proxy.ps1` |
| Doctor OK, this chat has no UE tools | Session started while 8010 was down | **New chat** |
| `Unreal MCP upstream unavailable` | UE not on 8000 | Editor + StartServer |
| tools/call empty on raw :8000 | SSE without Content-Length | Always use **8010** proxy |

## Editor console

```
ModelContextProtocol.StopServer
ModelContextProtocol.StartServer
ModelContextProtocol.RefreshTools
```

## Logs

- `Tools/_ue_mcp_proxy.log` — proxy requests  
- `Tools/_ue_mcp_proxy.err.log` — proxy errors  
- `Tools/_ue_mcp_watchdog.log` — restarts  

## Verify without Grok

```powershell
python Tools/_mcp_diag_via_proxy.py
```

Expect: `serverInfo.name == unreal-mcp`, `list_toolsets ok True`.
