"""Codex app-server adapter for Surf AI on T160.

Mode intentionally limited to text discussion of code:
- ChatGPT SIWC token only, no API key or extra billing fallback.
- Isolated CODEX_HOME and empty cwd, never a user project directory.
- Shell, connector/plugin, delegated-agent features disabled.
- Read-only sandbox, no approval escalation, auto-decline requests.
- No app-server process until an explicit ASK in Codex mode.
- No tool execution or file modifications are authorized by this module.

Codex app-server protocol:
https://developers.openai.com/siwc/token-sharing-open-source/codex-app-server
https://developers.openai.com/codex/app-server
"""
import json
import os
from pathlib import Path
import select
import shutil
import subprocess
import time

try:
    from . import catalog, siwc_auth
except ImportError:
    import catalog
    import siwc_auth


class CodexError(RuntimeError):
    pass


FORBIDDEN_ITEMS = frozenset({
    "commandExecution", "fileChange", "mcpToolCall", "dynamicToolCall",
    "collabToolCall", "webSearch", "imageView", "computerUse",
})

# Auth token is supplied via the child process environment as documented.
# Do not remove read-only and shell_tool=false without a distinct user approval.
CODEX_FLAGS = (
    'model_provider="openai_chatgpt_plan"',
    'model_providers.openai_chatgpt_plan.name="ChatGPT plan"',
    'model_providers.openai_chatgpt_plan.base_url="https://api.openai.com/v1"',
    'model_providers.openai_chatgpt_plan.env_key="ACCESS_TOKEN"',
    'model_providers.openai_chatgpt_plan.wire_api="responses"',
    'model_providers.openai_chatgpt_plan.requires_openai_auth=false',
    'model_providers.openai_chatgpt_plan.supports_websockets=false',
    "features.shell_tool=false",
    "features.apps=false",
    "features.plugins=false",
    "features.memories=false",
    "features.request_permissions_tool=false",
    "agents.enabled=false",
)


def codex_home():
    path = Path(os.environ.get(
        "SURF_CODEX_HOME", str(Path.home() / ".config/surf-ai/codex")))
    if path.is_symlink():
        raise CodexError("CODEX_HOME no puede ser un enlace simbólico")
    path.mkdir(mode=0o700, parents=True, exist_ok=True)
    path.chmod(0o700)
    return path


def isolated_cwd():
    path = Path(os.environ.get(
        "SURF_CODEX_CWD", str(Path.home() / ".local/share/surf-ai/codex-notes")))
    if path.is_symlink():
        raise CodexError("Directorio Codex inseguro")
    path.mkdir(mode=0o700, parents=True, exist_ok=True)
    path.chmod(0o700)
    return path


def codex_command():
    binary = shutil.which("codex")
    if not binary:
        raise CodexError("Codex CLI no está instalado en T160")
    args = [binary, "app-server", "--listen", "stdio://"]
    for flag in CODEX_FLAGS:
        args.extend(("-c", flag))
    return args


def private_environment(access_token):
    """Whitelist environment; never give Codex inherited API keys or credentials."""
    if not access_token:
        raise CodexError("El plan ChatGPT no está autorizado")
    user = str(Path.home())
    environment = {
        "HOME": user,
        "USER": os.environ.get("USER", "spc"),
        "PATH": "/usr/local/bin:/usr/bin:/bin",
        "LANG": os.environ.get("LANG", "C.UTF-8"),
        "CODEX_HOME": str(codex_home()),
        "ACCESS_TOKEN": access_token,
        "RUST_LOG": "error",
    }
    return environment


def deny_server_request(message):
    """Never authorize shell/file/connector/network access from Codex mode."""
    if "id" not in message:
        return None
    method = message.get("method", "")
    if method in (
        "item/commandExecution/requestApproval",
        "item/fileChange/requestApproval",
    ):
        return {"id": message["id"], "result": {"decision": "decline"}}
    if method == "item/permissions/requestApproval":
        return {"id": message["id"], "result": {"permissions": {}}}
    if method in (
        "mcpServer/elicitation/request", "item/tool/requestUserInput",
    ):
        return {"id": message["id"], "result": {"action": "decline"}}
    return {
        "id": message["id"],
        "error": {"code": -32601, "message": "Acción deshabilitada en Surf AI"},
    }


class AppServer:
    """Short-lived stdio JSON-RPC connection; one turn per process.

    The persisted thread id resides in T160 SQLite, not in Surf's memory.
    """

    def __init__(self, access_token, timeout=190):
        self.timeout = timeout
        self.started_at = time.monotonic()
        self.process = subprocess.Popen(
            codex_command(), cwd=str(isolated_cwd()),
            env=private_environment(access_token),
            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL, bufsize=0,
        )
        self.next_id = 0

    def close(self):
        process = self.process
        if process.poll() is None:
            process.terminate()
        try:
            process.communicate(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.communicate(timeout=3)

    def send(self, message):
        payload = (json.dumps(message, ensure_ascii=False, separators=(",", ":"))
                   + "\n").encode("utf-8")
        if len(payload) > 100_000:
            raise CodexError("Mensaje Codex demasiado extenso")
        self.process.stdin.write(payload)
        self.process.stdin.flush()

    def receive(self):
        remaining = self.timeout - (time.monotonic() - self.started_at)
        if remaining <= 0:
            raise CodexError("Tiempo máximo de Codex agotado")
        ready, _, _ = select.select([self.process.stdout], [], [], remaining)
        if not ready:
            raise CodexError("Codex no respondió dentro del plazo")
        data = self.process.stdout.readline(256_001)
        if len(data) > 256_000:
            raise CodexError("Evento Codex demasiado extenso")
        if not data:
            raise CodexError("Codex app-server terminó inesperadamente")
        try:
            message = json.loads(data)
        except ValueError:
            raise CodexError("Respuesta Codex inválida") from None
        if not isinstance(message, dict):
            raise CodexError("Evento Codex no reconocido")
        # Server-initiated requests are never approved automatically.
        if "id" in message and "method" in message:
            decision = deny_server_request(message)
            if decision:
                self.send(decision)
            return {"method": "surf/approvalDeclined", "params": {}}
        if message.get("method") in ("item/started", "item/completed"):
            item = (message.get("params") or {}).get("item") or {}
            if item.get("type") in FORBIDDEN_ITEMS:
                raise CodexError(
                    "Codex intentó una herramienta deshabilitada; turno cancelado")
        return message

    def request(self, method, params):
        self.next_id += 1
        req_id = self.next_id
        self.send({"id": req_id, "method": method, "params": params})
        while True:
            event = self.receive()
            if event.get("id") != req_id:
                continue
            if "error" in event:
                raise CodexError(
                    "Codex rechazó " + method + " (" +
                    str((event["error"] or {}).get("code", "error")) + ")")
            if not isinstance(event.get("result"), dict):
                raise CodexError("Respuesta incompleta de " + method)
            return event["result"]


def stream_text_only(session, prompt):
    """Yield only agent text; no access to T160 projects or command tools."""
    if os.environ.get("SURF_CODEX_TEXT_ONLY", "0") != "1":
        raise CodexError("Codex no habilitado: modo de conversación pendiente")
    if not prompt.strip():
        raise CodexError("Consulta vacía")
    model = os.environ.get("SURF_CODEX_MODEL", "").strip()
    if not model:
        raise CodexError("Falta modelo Codex autorizado")
    # On every turn, SIWC refreshes credentials before any app-server launch.
    token = siwc_auth.access_token()
    server = AppServer(token)
    try:
        init = server.request("initialize", {
            "clientInfo": {
                "name": "surf_ai",
                "title": "Surf AI",
                "version": "0.2.0",
            },
        })
        server.send({"method": "initialized", "params": {}})
        thread_id = catalog.get_codex_thread(session)
        if thread_id:
            result = server.request("thread/resume", {
                "threadId": thread_id, "excludeTurns": True,
            })
        else:
            result = server.request("thread/start", {
                "model": model,
                "cwd": str(isolated_cwd()),
                "approvalPolicy": "never",
                "sandbox": "read-only",
                "serviceName": "surf_ai",
            })
        thread = result.get("thread", {})
        actual_id = thread.get("id")
        if not isinstance(actual_id, str) or not actual_id:
            raise CodexError("Codex no creó o recuperó una conversación")
        if thread_id and actual_id != thread_id:
            raise CodexError("Codex devolvió una sesión distinta")
        if not thread_id:
            catalog.set_codex_thread(session, actual_id)
        server.next_id += 1
        turn_id = server.next_id
        server.send({
            "id": turn_id,
            "method": "turn/start",
            "params": {
                "threadId": actual_id,
                "input": [{"type": "text", "text": prompt}],
                "cwd": str(isolated_cwd()),
                "model": model,
                "approvalPolicy": "never",
                "sandboxPolicy": {"type": "readOnly"},
            },
        })
        finished = False
        streamed = False
        while not finished:
            event = server.receive()
            if event.get("id") == turn_id and "error" in event:
                raise CodexError("Codex rechazó iniciar el turno")
            if event.get("method") == "item/agentMessage/delta":
                delta = (event.get("params") or {}).get("delta", "")
                if isinstance(delta, str) and delta:
                    streamed = True
                    yield delta
            if event.get("method") == "turn/completed":
                status = ((event.get("params") or {}).get("turn") or {}).get(
                    "status")
                if status != "completed":
                    raise CodexError("Codex terminó sin completar la respuesta")
                finished = True
        if not streamed:
            raise CodexError("Codex no devolvió texto")
    finally:
        server.close()
