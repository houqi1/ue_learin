#!/usr/bin/env python3
"""
Unreal MCP Streamable-HTTP compatibility proxy.

Why this exists
---------------
UE 5.8 ModelContextProtocol serves tools/call as text/event-stream **without**
Content-Length / chunked encoding (raw keep-alive TCP). Many MCP HTTP clients
(including some Streamable HTTP implementations) either:
  - read 0 body bytes and treat the call as empty/failed, or
  - hang until timeout, then close the pipe (Windows error 232).

This proxy:
  - listens on 127.0.0.1:PROXY_PORT (default 8010)
  - forwards to UE 127.0.0.1:UE_PORT/mcp (default 8000)
  - fully buffers SSE tool responses and re-emits them with Content-Length
  - fills empty serverInfo.name/title with "unreal-mcp"

Usage
-----
  python Tools/unreal_mcp_http_proxy.py
  python Tools/unreal_mcp_http_proxy.py --listen 8010 --upstream 8000

Point Grok / .mcp.json at: http://127.0.0.1:8010/mcp
Editor must be running with ModelContextProtocol auto-start (port 8000).
"""
from __future__ import annotations

import argparse
import json
import socket
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Optional
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen


def log(msg: str) -> None:
    ts = time.strftime("%H:%M:%S")
    print(f"[ue-mcp-proxy {ts}] {msg}", flush=True)


def read_ue_response(
    method: str,
    path: str,
    body: bytes,
    headers: dict[str, str],
    upstream: str,
    timeout: float,
) -> tuple[int, dict[str, str], bytes]:
    """POST/GET/DELETE to upstream; fully buffer body including SSE."""
    url = upstream.rstrip("/") + path
    # Build request headers for UE
    req_headers = {
        "Accept": headers.get("Accept") or "application/json, text/event-stream",
        "Content-Type": headers.get("Content-Type") or "application/json",
    }
    # Forward session id either way
    for key in ("Mcp-Session-Id", "mcp-session-id"):
        if key in headers:
            req_headers["Mcp-Session-Id"] = headers[key]
            break

    # Use raw socket for tools/call SSE reliability (urllib often returns empty).
    host_port = upstream.replace("http://", "").replace("https://", "")
    if "/" in host_port:
        host_port = host_port.split("/", 1)[0]
    host, port_s = host_port.split(":") if ":" in host_port else (host_port, "80")
    port = int(port_s)

    path_q = path if path.startswith("/") else "/" + path
    hdr_lines = [
        f"{method} {path_q} HTTP/1.1",
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
        sock.settimeout(0.35)
        chunks: list[bytes] = []
        t0 = time.time()
        deadline = t0 + timeout
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
                # SSE: complete if we have data event terminated by blank line
                if is_sse:
                    body_part = joined.split(b"\r\n\r\n", 1)[-1]
                    if b"data: " in body_part and (
                        body_part.rstrip().endswith(b"\n\n")
                        or body_part.rstrip().endswith(b"\r\n\r\n")
                    ):
                        # small grace for trailing events
                        sock.settimeout(0.25)
                        try:
                            extra = sock.recv(65536)
                            if extra:
                                chunks.append(extra)
                                continue
                        except socket.timeout:
                            pass
                        break
                # JSON with content-length
                if content_length is not None:
                    body_part = joined.split(b"\r\n\r\n", 1)[-1]
                    if len(body_part) >= content_length:
                        break
                continue

            if not c:
                break
            chunks.append(c)
            joined = b"".join(chunks)

            if not header_done and b"\r\n\r\n" in joined:
                header_done = True
                header_blob, _ = joined.split(b"\r\n\r\n", 1)
                lines = header_blob.decode("latin1", "replace").split("\r\n")
                # status
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
                ct = (resp_headers.get("Content-Type") or resp_headers.get("content-type") or "").lower()
                is_sse = "text/event-stream" in ct
                # notifications often 202 empty
                if content_length == 0:
                    break
                if content_length is not None and content_length > 0:
                    body_so_far = joined.split(b"\r\n\r\n", 1)[1]
                    if len(body_so_far) >= content_length:
                        break

        joined = b"".join(chunks)
        if b"\r\n\r\n" not in joined:
            return 502, {"Content-Type": "text/plain"}, b"upstream incomplete headers"
        _, body_out = joined.split(b"\r\n\r\n", 1)
        if content_length is not None and content_length >= 0:
            body_out = body_out[:content_length]

        # Normalize headers for client (case-insensitive, no hop-by-hop / duplicates)
        hop = {
            "transfer-encoding",
            "content-length",
            "connection",
            "keep-alive",
            "content-type",
            "server",
            "date",
        }
        out_headers: dict[str, str] = {}
        session_id: Optional[str] = None
        for k, v in resp_headers.items():
            lk = k.lower()
            if lk in hop:
                continue
            if lk == "mcp-session-id":
                session_id = v
                continue
            out_headers[k] = v

        # Prefer JSON if body is pure JSON-RPC (initialize / tools/list)
        if body_out.lstrip().startswith(b"{"):
            out_headers["Content-Type"] = "application/json; charset=utf-8"
            # Fix empty serverInfo for flaky clients
            try:
                obj = json.loads(body_out.decode("utf-8"))
                if isinstance(obj, dict) and "result" in obj:
                    res = obj["result"]
                    if isinstance(res, dict) and "serverInfo" in res:
                        si = res.get("serverInfo") or {}
                        if not si.get("name"):
                            si["name"] = "unreal-mcp"
                        if not si.get("title"):
                            si["title"] = "unreal-mcp"
                        if not si.get("version"):
                            si["version"] = "1.0"
                        res["serverInfo"] = si
                        body_out = json.dumps(obj, ensure_ascii=False).encode("utf-8")
            except Exception:
                pass
        elif is_sse or body_out.lstrip().startswith(b"event:"):
            # Prefer plain JSON-RPC for maximum client compatibility.
            # UE emits: event: message\ndata: {jsonrpc...}\n\n
            json_payload = None
            try:
                text = body_out.decode("utf-8", "replace")
                for line in text.splitlines():
                    if line.startswith("data:"):
                        json_payload = line[5:].strip()
                        break
                if json_payload:
                    # validate
                    json.loads(json_payload)
                    body_out = json_payload.encode("utf-8")
                    out_headers["Content-Type"] = "application/json; charset=utf-8"
                else:
                    out_headers["Content-Type"] = "text/event-stream; charset=utf-8"
                    if not body_out.endswith(b"\n\n"):
                        body_out += b"\n\n" if not body_out.endswith(b"\n") else b"\n"
            except Exception:
                out_headers["Content-Type"] = "text/event-stream; charset=utf-8"
        elif not body_out and status in (202, 204, 405):
            # empty notification / method-not-allowed: leave body empty
            out_headers.setdefault("Content-Type", "text/plain; charset=utf-8")
        else:
            out_headers.setdefault(
                "Content-Type",
                resp_headers.get("Content-Type")
                or resp_headers.get("content-type")
                or "application/octet-stream",
            )

        if session_id:
            out_headers["Mcp-Session-Id"] = session_id

        out_headers["Content-Length"] = str(len(body_out))
        out_headers["Connection"] = "close"
        return status, out_headers, body_out
    finally:
        try:
            sock.close()
        except Exception:
            pass


def make_handler(upstream: str, tool_timeout: float):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, fmt: str, *args) -> None:
            log(f"{self.address_string()} {fmt % args}")

        def _send(self, status: int, headers: dict[str, str], body: bytes) -> None:
            self.send_response(status)
            for k, v in headers.items():
                self.send_header(k, v)
            self.end_headers()
            if body and self.command != "HEAD":
                self.wfile.write(body)

        def _handle(self) -> None:
            length = int(self.headers.get("Content-Length") or 0)
            body = self.rfile.read(length) if length else b""
            path = self.path.split("?", 1)[0]

            # Grok / Streamable-HTTP clients probe OAuth metadata; UE has none.
            if "well-known" in path.lower() or "oauth" in path.lower():
                msg = b"{}"
                self._send(
                    404,
                    {
                        "Content-Type": "application/json; charset=utf-8",
                        "Content-Length": str(len(msg)),
                        "Connection": "close",
                    },
                    msg,
                )
                return

            # Only proxy MCP path
            if path != "/mcp" and not path.endswith("/mcp"):
                msg = b"use /mcp"
                self._send(
                    404,
                    {
                        "Content-Type": "text/plain; charset=utf-8",
                        "Content-Length": str(len(msg)),
                        "Connection": "close",
                    },
                    msg,
                )
                return

            # Optional server->client SSE: UE returns 405. Do the same — a short
            # 200 SSE that immediately closes confuses Streamable HTTP clients
            # (Grok/rmcp "Send message error" during handshake).
            if self.command == "GET":
                msg = b"Method Not Allowed"
                self._send(
                    405,
                    {
                        "Content-Type": "text/plain; charset=utf-8",
                        "Content-Length": str(len(msg)),
                        "Allow": "POST, DELETE",
                        "Connection": "close",
                    },
                    msg,
                )
                return

            # Heuristic timeout: tools/call needs longer
            timeout = tool_timeout
            try:
                if body:
                    j = json.loads(body.decode("utf-8", "replace"))
                    method = j.get("method") or ""
                    if method in ("initialize", "tools/list", "ping"):
                        timeout = min(15.0, tool_timeout)
                    elif method == "notifications/initialized":
                        timeout = 5.0
            except Exception:
                pass

            in_headers = {k: v for k, v in self.headers.items()}
            try:
                status, out_headers, out_body = read_ue_response(
                    self.command,
                    "/mcp",
                    body,
                    in_headers,
                    upstream,
                    timeout,
                )
            except (OSError, URLError, TimeoutError, HTTPError) as e:
                log(f"upstream error: {e}")
                msg = json.dumps(
                    {
                        "jsonrpc": "2.0",
                        "error": {
                            "code": -32000,
                            "message": f"Unreal MCP upstream unavailable: {e}. "
                            "Is the Editor running with ModelContextProtocol.StartServer?",
                        },
                        "id": None,
                    }
                ).encode("utf-8")
                self._send(
                    502,
                    {
                        "Content-Type": "application/json; charset=utf-8",
                        "Content-Length": str(len(msg)),
                        "Connection": "close",
                    },
                    msg,
                )
                return

            self._send(status, out_headers, out_body)

        def do_POST(self) -> None:  # noqa: N802
            self._handle()

        def do_GET(self) -> None:  # noqa: N802
            self._handle()

        def do_DELETE(self) -> None:  # noqa: N802
            self._handle()

    return Handler


def main() -> None:
    ap = argparse.ArgumentParser(description="Unreal MCP HTTP compatibility proxy")
    ap.add_argument("--listen-host", default="127.0.0.1")
    ap.add_argument("--listen-port", type=int, default=8010)
    ap.add_argument("--upstream", default="http://127.0.0.1:8000")
    ap.add_argument("--tool-timeout", type=float, default=120.0)
    args = ap.parse_args()

    handler = make_handler(args.upstream, args.tool_timeout)
    httpd = ThreadingHTTPServer((args.listen_host, args.listen_port), handler)
    log(
        f"listening http://{args.listen_host}:{args.listen_port}/mcp "
        f"-> {args.upstream}/mcp (tool_timeout={args.tool_timeout}s)"
    )
    log("Point Grok MCP url to this proxy. Keep Unreal Editor MCP on upstream port.")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        log("stopped")


if __name__ == "__main__":
    main()
