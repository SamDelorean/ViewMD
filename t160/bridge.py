#!/usr/bin/env python3
"""Minimal T160 relay for Surf AI GTK3.

Binds to loopback only; Surf reaches it via SSH local forwarding.
Uses Python 3 standard library; API keys never leave T160.
Protocol and limitations: docs/SURF_AI.md.
SPDX-License-Identifier: MIT
"""
import argparse
import base64
import json
import os
import socketserver
import threading
import urllib.error
import urllib.request

try:
    from . import catalog
except ImportError:
    import catalog

HOST = "127.0.0.1"
PORT = int(os.environ.get("SURF_AI_PORT", "8765"))
MAX_LINE = 1024 * 1024
MAX_TURNS = 12  # provider context limit, not UI limit
PAGE_SIZE = 5
MAX_STORED_MESSAGES = 200
sessions = {}
lock = threading.Lock()


def send(handler, kind, text=None):
    if text is None:
        frame = kind + "\n"
    else:
        payload = base64.b64encode(text.encode("utf-8")).decode("ascii")
        frame = kind + "\t" + payload + "\n"
    handler.wfile.write(frame.encode("ascii"))
    handler.wfile.flush()


def stream_provider(provider, messages):
    if provider == "openai":
        key = os.environ.get("OPENAI_API_KEY")
        model = os.environ.get("SURF_OPENAI_MODEL")
        if not key or not model:
            raise RuntimeError("Configura OPENAI_API_KEY y SURF_OPENAI_MODEL en T160")
        uri = "https://api.openai.com/v1/responses"
        payload = {"model": model, "input": messages, "stream": True}
        headers = {"Authorization": "Bearer " + key}
    elif provider == "anthropic":
        key = os.environ.get("ANTHROPIC_API_KEY")
        model = os.environ.get("SURF_ANTHROPIC_MODEL")
        if not key or not model:
            raise RuntimeError("Configura ANTHROPIC_API_KEY y SURF_ANTHROPIC_MODEL en T160")
        uri = "https://api.anthropic.com/v1/messages"
        payload = {
            "model": model,
            "messages": messages,
            "max_tokens": int(os.environ.get("SURF_MAX_TOKENS", "1536")),
            "stream": True,
        }
        headers = {"x-api-key": key, "anthropic-version": "2023-06-01"}
    else:
        raise RuntimeError("Proveedor desconocido")

    headers["Content-Type"] = "application/json"
    request = urllib.request.Request(
        uri, data=json.dumps(payload, ensure_ascii=False).encode("utf-8"),
        headers=headers, method="POST"
    )
    with urllib.request.urlopen(request, timeout=180) as reply:
        for raw in reply:
            if not raw.startswith(b"data:"):
                continue
            data = raw[5:].strip()
            if not data or data == b"[DONE]":
                continue
            event = json.loads(data.decode("utf-8"))
            event_type = event.get("type")
            if provider == "openai":
                if event_type == "response.output_text.delta":
                    delta = event.get("delta", "")
                    if delta:
                        yield delta
                elif event_type in ("response.failed", "response.incomplete", "error"):
                    raise RuntimeError("La API OpenAI no completó la respuesta")
            else:
                if event_type == "content_block_delta":
                    delta = event.get("delta", {})
                    if delta.get("type") == "text_delta" and delta.get("text"):
                        yield delta["text"]
                elif event_type == "error":
                    raise RuntimeError("La API Anthropic informó un error")


class Handler(socketserver.StreamRequestHandler):
    def handle(self):
        self.connection.settimeout(240)
        raw = self.rfile.readline(MAX_LINE + 1)
        if not raw or len(raw) > MAX_LINE:
            return
        fields = raw.rstrip(b"\r\n").split(b"\t", 3)
        if len(fields) != 4 or fields[0] not in (
                b"ASK", b"HISTORY", b"LIST", b"RENAME", b"DELETE", b"RELOAD"):
            send(self, "ERROR", "Protocolo incorrecto")
            return
        try:
            provider = fields[1].decode("ascii")
            session = fields[2].decode("ascii")
            if provider not in ("openai", "anthropic"):
                raise ValueError("Proveedor inválido")
            if not session or len(session) > 80 or not all(
                    c.isalnum() or c in "-_" for c in session):
                raise ValueError("Identificador de sesión inválido")
            ident = (session, provider)
            if fields[0] == b"LIST":
                cursor = fields[3].decode("ascii")
                if cursor == "-":
                    rows, more = catalog.list_page(provider)
                else:
                    try:
                        updated, last_id = cursor.split(":", 1)
                        if not updated.isdecimal() or len(last_id) > 80:
                            raise ValueError()
                        rows, more = catalog.list_page(provider, int(updated), last_id)
                    except ValueError:
                        raise ValueError("Cursor de lista inválido")
                for item_id, title, updated, status in rows:
                    send(self, "SESSION", item_id + "\t" + str(updated)
                         + "\t" + status + "\t" + title)
                next_cursor = "{}:{}".format(rows[-1][2], rows[-1][0]) if more and rows else "-"
                send(self, "NEXT", next_cursor)
                send(self, "DONE")
                return
            if fields[0] == b"RELOAD":
                if fields[3] != b"-":
                    raise ValueError("Argumento de recarga inválido")
                metadata, rows, more = catalog.snapshot(
                    provider, session, PAGE_SIZE)
                title, status, updated = metadata
                send(self, "META", status + "\t" + str(updated) + "\t" + title)
                for role, body in rows:
                    send(self, "ITEM", role + "\t" + body)
                send(self, "MORE", "1" if more else "0")
                send(self, "DONE")
                return
            if fields[0] == b"RENAME":
                new_title = base64.b64decode(fields[3], validate=True).decode("utf-8")
                send(self, "TITLE", catalog.rename(provider, session, new_title))
                send(self, "DONE")
                return
            if fields[0] == b"DELETE":
                catalog.delete_session(provider, session)
                with lock:
                    sessions.pop(ident, None)
                send(self, "DONE")
                return
            if fields[0] == b"HISTORY":
                arg = fields[3].decode("ascii")
                # A trailing 'u' is for the automatic post-stream refresh:
                # do not clear the new-response notification until user action.
                preserve_unread = arg.endswith("u")
                if preserve_unread:
                    arg = arg[:-1]
                offset = int(arg)
                if offset < 0 or offset > MAX_STORED_MESSAGES:
                    raise ValueError("Desplazamiento fuera de rango")
                rows, more = catalog.get_messages(provider, session, offset, PAGE_SIZE)
                if offset == 0 and not preserve_unread:
                    catalog.mark_seen(provider, session)
                for role, body in rows:
                    send(self, "ITEM", role + "\t" + body)
                send(self, "MORE", "1" if more else "0")
                send(self, "DONE")
                return
            prompt = base64.b64decode(fields[3], validate=True).decode("utf-8")
            if not prompt.strip() or len(prompt) > 100000:
                raise ValueError("Consulta vacía o demasiado extensa")
            catalog.begin_request(provider, session, prompt[:60])
            previous = catalog.context(provider, session, MAX_TURNS * 2)
            history = previous[-MAX_TURNS * 2:] + [
                {"role": "user", "content": prompt}
            ]
            answer = []
            if self.server.mock:
                for fragment in (
                    "## Conexión local correcta\n\n",
                    "Surf AI se está comunicando con T160.\n\n",
                    "| Elemento | Estado |\n|---|---|\n",
                    "| GTK3 | Vista nativa |\n| Markdown | ViewMD |\n\n",
                    "~~~c\nputs(\"Prueba ARMv7\");\n~~~\n",
                ):
                    send(self, "DELTA", fragment)
                    answer.append(fragment)
            else:
                for fragment in stream_provider(provider, history):
                    send(self, "DELTA", fragment)
                    answer.append(fragment)
            if not answer:
                raise RuntimeError("La API no devolvió texto")
            catalog.append_pair(provider, session, prompt.strip()[:60],
                                prompt, "".join(answer))
            send(self, "DONE")
        except urllib.error.HTTPError as exc:
            if fields[0] == b"ASK":
                catalog.mark_failed(provider, session)
            send(self, "ERROR", "API HTTP {}: {}".format(
                exc.code, exc.reason))
        except (ValueError, UnicodeError, RuntimeError,
                urllib.error.URLError, TimeoutError, OSError) as exc:
            if fields[0] == b"ASK" and "provider" in locals() and "session" in locals():
                catalog.mark_failed(provider, session)
            try:
                send(self, "ERROR", str(exc)[:500])
            except OSError:
                pass


class Bridge(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--mock", action="store_true",
                        help="Respuestas locales de prueba, sin API ni coste")
    args = parser.parse_args()
    catalog.recover_incomplete()
    with Bridge((HOST, PORT), Handler) as server:
        server.mock = args.mock
        print("Surf AI bridge en {}:{} (mock={})".format(
            HOST, PORT, args.mock), flush=True)
        server.serve_forever()


if __name__ == "__main__":
    main()
