#!/usr/bin/env python3
"""
Unreal MCP stdio bridge for Grok / MCP clients.

Grok spawns this process (stdio MCP). We forward JSON-RPC to the Unreal Editor
ModelContextProtocol HTTP server on 127.0.0.1:8000, fully buffering SSE
tools/call responses (UE omits Content-Length).

Why not HTTP on :8010?
  The HTTP proxy dies between sessions; Grok then fails with
  "error sending request ... when send initialize request".
  Stdio is owned by Grok and starts every session.

Requires: Unreal Editor running with ModelContextProtocol on port 8000.
"""
from __future__ import annotations

import json
import os
import socket
import sys
import time
from typing import Any, Optional

UPSTREAM = os.environ.get("UE_MCP_UPSTREAM", "http://127.0.0.1:8000")
TOOL_TIMEOUT = float(os.environ.get("UE_MCP_TOOL_TIMEOUT", "120"))
LOG_PATH = os.environ.get(
    "UE_MCP_STDIO_LOG",
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "_ue_mcp_stdio.log"),
)

_session_id: Optional[str] = None
_stderr_log = True


def elog(msg: str) -> None:
    """Log to file + stderr (stderr is safe for MCP stdio; stdout is protocol)."""
    line = f"[ue-mcp-stdio {time.strftime('%H:%M:%S')}] {msg}\n"
    try:
        with open(LOG_PATH, "a", encoding="utf-8", errors="replace") as f:
            f.write(line)
    except Exception:
        pass
    if _stderr_log:
        try:
            sys.stderr.write(line)
            sys.stderr.flush()
        except Exception:
            pass


def port_open(host: str, port: int, timeout: float = 0.5) -> bool:
    try:
        with socket.create_connection((host, port), timeout=timeout):
            return True
    except OSError:
        return False


def parse_upstream(url: str) -> tuple[str, int]:
    host_port = url.replace("http://", "").replace("https://", "")
    if "/" in host_port:
        host_port = host_port.split("/", 1)[0]
    if ":" in host_port:
        h, p = host_port.split(":", 1)
        return h, int(p)
    return host_port, 80


def normalize_body(body: bytes, is_sse: bool) -> bytes:
    """Convert SSE/data: payloads and empty serverInfo into plain JSON-RPC."""
    if body.lstrip().startswith(b"{"):
        try:
            obj = json.loads(body.decode("utf-8"))
            if isinstance(obj, dict) and isinstance(obj.get("result"), dict):
                res = obj["result"]
                if "serverInfo" in res:
                    si = res.get("serverInfo") or {}
                    if not si.get("name"):
                        si["name"] = "unreal-mcp"
                    if not si.get("title"):
                        si["title"] = "unreal-mcp"
                    if not si.get("version"):
                        si["version"] = "1.0"
                    res["serverInfo"] = si
                    return json.dumps(obj, ensure_ascii=False).encode("utf-8")
        except Exception:
            pass
        return body

    if is_sse or body.lstrip().startswith(b"event:") or b"data:" in body[:64]:
        try:
            text = body.decode("utf-8", "replace")
            for line in text.splitlines():
                if line.startswith("data:"):
                    payload = line[5:].strip()
                    if payload:
                        json.loads(payload)
                        return payload.encode("utf-8")
        except Exception:
            pass
    return body


def _sse_complete(body_part: bytes) -> bool:
    """True when SSE body has a data event terminated by a blank line."""
    if b"data:" not in body_part:
        return False
    # UE: event: message\r\ndata: {...}\r\n\r\n
    stripped = body_part.rstrip()
    return (
        body_part.endswith(b"\n\n")
        or body_part.endswith(b"\r\n\r\n")
        or stripped.endswith(b"}")
    )


def ue_post(body: bytes, timeout: float) -> tuple[int, dict[str, str], bytes]:
    """POST body to UE /mcp; fully buffer SSE/JSON response."""
    global _session_id
    host, port = parse_upstream(UPSTREAM)
    path_q = "/mcp"
    req_headers = {
        "Accept": "application/json, text/event-stream",
        "Content-Type": "application/json",
    }
    if _session_id:
        req_headers["Mcp-Session-Id"] = _session_id

    # keep-alive: UE tools/call SSE may not deliver data if client requests close
    hdr_lines = [
        f"POST {path_q} HTTP/1.1",
        f"Host: {host}:{port}",
        f"Content-Length: {len(body)}",
        "Connection: keep-alive",
    ]
    for k, v in req_headers.items():
        hdr_lines.append(f"{k}: {v}")
    raw_req = ("\r\n".join(hdr_lines) + "\r\n\r\n").encode("utf-8") + body

    sock = socket.create_connection((host, port), timeout=min(10.0, timeout))
    try:
        sock.sendall(raw_req)
        sock.settimeout(0.4)
        chunks: list[bytes] = []
        deadline = time.time() + timeout
        header_done = False
        status = 502
        resp_headers: dict[str, str] = {}
        content_length: Optional[int] = None
        is_sse = False

        while time.time() < deadline:
            try:
                c = sock.recv(65536)
            except socket.timeout:
                joined = b"".join(chunks)
                if not header_done:
                    continue
                if is_sse:
                    body_part = joined.split(b"\r\n\r\n", 1)[-1]
                    if _sse_complete(body_part):
                        # brief grace for trailing bytes
                        sock.settimeout(0.2)
                        try:
                            extra = sock.recv(65536)
                            if extra:
                                chunks.append(extra)
                                continue
                        except socket.timeout:
                            pass
                        break
                if content_length is not None:
                    body_part = joined.split(b"\r\n\r\n", 1)[-1]
                    if len(body_part) >= content_length:
                        break
                continue

            if not c:
                # peer closed — accept what we have
                break
            chunks.append(c)
            joined = b"".join(chunks)

            if not header_done and b"\r\n\r\n" in joined:
                header_done = True
                header_blob, body_so_far = joined.split(b"\r\n\r\n", 1)
                lines = header_blob.decode("latin1", "replace").split("\r\n")
                try:
                    status = int(lines[0].split()[1])
                except Exception:
                    status = 502
                for line in lines[1:]:
                    if ":" not in line:
                        continue
                    k, v = line.split(":", 1)
                    resp_headers[k.strip()] = v.strip()
                cl = resp_headers.get("Content-Length") or resp_headers.get(
                    "content-length"
                )
                if cl is not None:
                    try:
                        content_length = int(cl)
                    except ValueError:
                        content_length = None
                ct = (
                    resp_headers.get("Content-Type")
                    or resp_headers.get("content-type")
                    or ""
                ).lower()
                is_sse = "text/event-stream" in ct
                if content_length == 0:
                    break
                if content_length is not None and content_length > 0:
                    if len(body_so_far) >= content_length:
                        break
                if is_sse and _sse_complete(body_so_far):
                    break

        joined = b"".join(chunks)
        if b"\r\n\r\n" not in joined:
            return (
                502,
                {},
                b'{"jsonrpc":"2.0","error":{"code":-32000,"message":"UE incomplete headers"},"id":null}',
            )
        _, body_out = joined.split(b"\r\n\r\n", 1)
        if content_length is not None and content_length >= 0:
            body_out = body_out[:content_length]

        sid = resp_headers.get("Mcp-Session-Id") or resp_headers.get("mcp-session-id")
        if sid:
            _session_id = sid

        if is_sse and not body_out.strip():
            return (
                502,
                resp_headers,
                json.dumps(
                    {
                        "jsonrpc": "2.0",
                        "error": {
                            "code": -32000,
                            "message": "UE SSE tools/call returned empty body",
                        },
                        "id": None,
                    }
                ).encode("utf-8"),
            )

        body_out = normalize_body(body_out, is_sse)
        return status, resp_headers, body_out
    finally:
        try:
            sock.close()
        except Exception:
            pass


def read_stdio_message() -> Optional[dict[str, Any]]:
    """Read one MCP stdio message (Content-Length framing)."""
    headers: dict[str, str] = {}
    while True:
        line = sys.stdin.buffer.readline()
        if not line:
            return None
        if line in (b"\r\n", b"\n"):
            break
        try:
            text = line.decode("utf-8", "replace").strip()
        except Exception:
            continue
        if ":" in text:
            k, v = text.split(":", 1)
            headers[k.strip().lower()] = v.strip()

    length_s = headers.get("content-length")
    if not length_s:
        return None
    try:
        length = int(length_s)
    except ValueError:
        return None
    raw = sys.stdin.buffer.read(length)
    if not raw or len(raw) < length:
        return None
    try:
        return json.loads(raw.decode("utf-8"))
    except json.JSONDecodeError as e:
        elog(f"bad json from client: {e}")
        return None


def write_stdio_message(msg: dict[str, Any]) -> None:
    data = json.dumps(msg, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    header = f"Content-Length: {len(data)}\r\n\r\n".encode("ascii")
    sys.stdout.buffer.write(header)
    sys.stdout.buffer.write(data)
    sys.stdout.buffer.flush()


def timeout_for(method: str) -> float:
    if method in ("initialize", "tools/list", "ping", "resources/list", "prompts/list"):
        return min(20.0, TOOL_TIMEOUT)
    if method == "notifications/initialized" or method.startswith("notifications/"):
        return 8.0
    return TOOL_TIMEOUT


def forward(msg: dict[str, Any]) -> Optional[dict[str, Any]]:
    """Forward one JSON-RPC message to UE; return response or None for notifications."""
    method = msg.get("method") or ""
    is_notif = "id" not in msg
    body = json.dumps(msg, ensure_ascii=False).encode("utf-8")
    try:
        status, _hdrs, out = ue_post(body, timeout_for(method))
    except OSError as e:
        if is_notif:
            return None
        return {
            "jsonrpc": "2.0",
            "id": msg.get("id"),
            "error": {
                "code": -32000,
                "message": (
                    f"Unreal MCP upstream unavailable: {e}. "
                    "Open Unreal Editor and run ModelContextProtocol.StartServer (port 8000)."
                ),
            },
        }

    if is_notif:
        return None

    if not out:
        # 202 empty etc.
        if status in (200, 202, 204):
            return {
                "jsonrpc": "2.0",
                "id": msg.get("id"),
                "result": {},
            }
        return {
            "jsonrpc": "2.0",
            "id": msg.get("id"),
            "error": {
                "code": -32000,
                "message": f"Empty response from UE (HTTP {status})",
            },
        }

    try:
        parsed = json.loads(out.decode("utf-8"))
        if isinstance(parsed, dict):
            # bridge-generated errors may use id:null; stamp client id
            if parsed.get("id") is None and "id" in msg:
                parsed["id"] = msg["id"]
            return parsed
    except Exception as e:
        elog(f"UE body not JSON: {e} body={out[:200]!r}")

    return {
        "jsonrpc": "2.0",
        "id": msg.get("id"),
        "error": {
            "code": -32000,
            "message": f"Non-JSON from UE (HTTP {status}): {out[:200]!r}",
        },
    }


def wait_for_ue(max_wait: float = 15.0) -> bool:
    host, port = parse_upstream(UPSTREAM)
    t0 = time.time()
    while time.time() - t0 < max_wait:
        if port_open(host, port):
            return True
        time.sleep(0.4)
    return False


def main() -> int:
    # Ensure binary-ish stdio on Windows
    try:
        sys.stdin.reconfigure(encoding="utf-8")  # type: ignore[attr-defined]
        sys.stdout.reconfigure(encoding="utf-8")  # type: ignore[attr-defined]
    except Exception:
        pass

    host, port = parse_upstream(UPSTREAM)
    if not wait_for_ue(12.0):
        elog(f"UE not listening on {host}:{port} — bridge will error on first request")
    else:
        elog(f"ready; upstream {UPSTREAM}/mcp")

    while True:
        msg = read_stdio_message()
        if msg is None:
            elog("stdin closed; exit")
            return 0
        method = msg.get("method", "")
        mid = msg.get("id", None)
        elog(f"<- {method or 'response'} id={mid}")
        try:
            resp = forward(msg)
        except Exception as e:
            elog(f"forward error: {e}")
            if "id" in msg:
                resp = {
                    "jsonrpc": "2.0",
                    "id": msg.get("id"),
                    "error": {"code": -32000, "message": str(e)},
                }
            else:
                resp = None
        if resp is not None:
            write_stdio_message(resp)
            elog(f"-> id={resp.get('id')} keys={list(resp.keys())}")


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        raise SystemExit(0)
