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
        if len(fields) != 4 or fields[0] not in (b"ASK", b"HISTORY"):
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
            if fields[0] == b"HISTORY":
                offset = int(fields[3].decode("ascii"))
                if offset < 0 or offset > MAX_STORED_MESSAGES:
                    raise ValueError("Desplazamiento fuera de rango")
                with lock:
                    saved = list(sessions.get(ident, []))
                end = max(0, len(saved) - offset)
                start = max(0, end - PAGE_SIZE)
                for message in saved[start:end]:
                    send(self, "ITEM", message["role"] + "\t" + message["content"])
                send(self, "MORE", "1" if start > 0 else "0")
                send(self, "DONE")
                return
            prompt = base64.b64decode(fields[3], validate=True).decode("utf-8")
            if not prompt.strip() or len(prompt) > 100000:
                raise ValueError("Consulta vacía o demasiado extensa")
            with lock:
                previous = list(sessions.get(ident, []))
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
            with lock:
                sessions[ident] = (previous + [
                    {"role": "user", "content": prompt},
                    {"role": "assistant", "content": "".join(answer)}
                ])[-MAX_STORED_MESSAGES:]
            send(self, "DONE")
        except urllib.error.HTTPError as exc:
            send(self, "ERROR", "API HTTP {}: {}".format(
                exc.code, exc.reason))
        except (ValueError, UnicodeError, RuntimeError,
                urllib.error.URLError, TimeoutError, OSError) as exc:
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
    with Bridge((HOST, PORT), Handler) as server:
        server.mock = args.mock
        print("Surf AI bridge en {}:{} (mock={})".format(
            HOST, PORT, args.mock), flush=True)
        server.serve_forever()


if __name__ == "__main__":
    main()
