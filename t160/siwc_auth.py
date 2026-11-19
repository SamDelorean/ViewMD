#!/usr/bin/env python3
"""Surf AI ChatGPT-plan OAuth for LOCAL open-source T160 host.

Official SIWC dynamic-agent OAuth + PKCE. Never handles OpenAI API keys.
Secrets stay in an owner-only directory on T160; this file is safe for Git.
No network request or browser is opened by importing this module.

Docs: https://developers.openai.com/siwc/token-sharing-open-source/sign-in
"""
import argparse
import base64
import contextlib
import fcntl
import hashlib
import hmac
from http.server import BaseHTTPRequestHandler, HTTPServer
import json
import os
from pathlib import Path
import re
import secrets
import stat
import tempfile
import threading
import time
from urllib.parse import parse_qs, urlencode, urlparse
from urllib.request import Request, urlopen
import uuid
import webbrowser

ISSUER = "https://auth.openai.com"
AUTH_URL = ISSUER + "/api/accounts/authorize"
TOKEN_URL = ISSUER + "/api/accounts/oauth/token"
JWKS_URL = ISSUER + "/.well-known/jwks.json"
RESOURCE = "https://api.openai.com/v1"
MODELS_URL = RESOURCE + "/models"
APP_NAME = "Surf AI"
SCOPES = "openid profile email offline_access resource.invoke chatgpt.tokens.use.direct"
SHARING_SCOPE = "chatgpt.tokens.use.direct"
CALLBACK_PATH = "/auth/callback"


class AuthError(RuntimeError):
    pass


def auth_dir():
    path = Path(os.environ.get(
        "SURF_SIWC_DIR", str(Path.home() / ".config/surf-ai/siwc")))
    # A pre-existing directory must be private before any credentials are read.
    path.mkdir(mode=0o700, parents=True, exist_ok=True)
    if path.is_symlink() or not path.is_dir():
        raise AuthError("Directorio de autenticación inseguro")
    path.chmod(0o700)
    return path


def _private_write(path, payload):
    """Atomic replacement with private permissions; no token is logged."""
    path = Path(path)
    with tempfile.NamedTemporaryFile(
        mode="w", encoding="utf-8", dir=str(path.parent),
        prefix=".siwc-", delete=False
    ) as f:
        name = f.name
        os.chmod(name, 0o600)
        try:
            json.dump(payload, f, ensure_ascii=False, indent=2)
            f.write("\n")
            f.flush()
            os.fsync(f.fileno())
        except BaseException:
            os.unlink(name)
            raise
    os.replace(name, path)


def _private_read(path):
    path = Path(path)
    if not path.exists():
        return None
    mode = path.stat().st_mode
    if not stat.S_ISREG(mode) or mode & 0o077 or path.is_symlink():
        raise AuthError("Permisos inseguros en la credencial SIWC")
    with path.open(encoding="utf-8") as f:
        return json.load(f)


def host_id():
    root = auth_dir()
    path = root / "host.json"
    existing = _private_read(path)
    if existing:
        value = existing.get("ext_agent_host_id")
        if isinstance(value, str) and value.startswith("urn:uuid:"):
            uuid.UUID(value[9:])
            return value
        raise AuthError("Host ID guardado inválido")
    value = "urn:uuid:" + str(uuid.uuid4())
    _private_write(path, {"ext_agent_host_id": value})
    return value


def _profile_path():
    return auth_dir() / "profile.json"


def profile():
    data = _private_read(_profile_path())
    if data and (not data.get("client_id") or not data.get("subject")):
        raise AuthError("Perfil OAuth incompleto")
    return data


def _random():
    return secrets.token_urlsafe(36)


def make_authorization(port, saved=None):
    """Return (URL, state, nonce, verifier, redirect URI); no network activity."""
    if not 1 <= port <= 65535:
        raise ValueError("Puerto de callback inválido")
    state, nonce, verifier = _random(), _random(), _random()
    challenge = base64.urlsafe_b64encode(
        hashlib.sha256(verifier.encode("ascii")).digest()
    ).decode("ascii").rstrip("=")
    redirect = f"http://127.0.0.1:{port}{CALLBACK_PATH}"
    client = saved["client_id"] if saved else "dynamic_agent_client"
    params = {
        "client_id": client,
        "redirect_uri": redirect,
        "response_type": "code",
        "scope": SCOPES,
        "resource": RESOURCE,
        "state": state,
        "nonce": nonce,
        "code_challenge": challenge,
        "code_challenge_method": "S256",
        "ext_agent_host_id": host_id(),
    }
    if saved:
        if saved.get("id_token"):
            params["id_token_hint"] = saved["id_token"]
        elif saved.get("email"):
            params["login_hint"] = saved["email"]
    else:
        params["agent_name_hint"] = APP_NAME
    return AUTH_URL + "?" + urlencode(params), state, nonce, verifier, redirect


class _Callback(BaseHTTPRequestHandler):
    """One authorization attempt, bound to loopback, no request logging."""

    def log_message(self, *args):
        pass  # Never log OAuth query strings / authorization codes.

    def do_GET(self):
        parsed = urlparse(self.path)
        query = parse_qs(parsed.query, keep_blank_values=True)
        expected = self.server.expected_state
        valid = (
            parsed.path == CALLBACK_PATH
            and len(query.get("state", [])) == 1
            and hmac.compare_digest(query["state"][0], expected)
        )
        result = None
        if valid:
            if "error" in query:
                result = {"error": "Autorización denegada o cancelada"}
            elif len(query.get("code", [])) == 1:
                result = {
                    "code": query["code"][0],
                    "client_id": query.get("client_id", [""])[0],
                }
        if result is None:
            result = {"error": "Callback OAuth inválido o estado incorrecto"}
        self.server.callback = result
        body = (b"Autorizacion recibida. Puedes cerrar esta pestana."
                if "code" in result else
                b"Autorizacion no completada. Reintenta desde Surf AI.")
        self.send_response(200 if "code" in result else 400)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def _request_json(uri, form=None, bearer=None):
    if not uri.startswith(("https://auth.openai.com/", "https://api.openai.com/v1/")):
        raise AuthError("URL OAuth fuera de los dominios permitidos")
    headers = {"Accept": "application/json"}
    if bearer:
        headers["Authorization"] = "Bearer " + bearer
    body = urlencode(form).encode("utf-8") if form is not None else None
    if body is not None:
        headers["Content-Type"] = "application/x-www-form-urlencoded"
    req = Request(uri, data=body, headers=headers,
                  method="POST" if body is not None else "GET")
    try:
        with urlopen(req, timeout=25) as response:
            result = json.load(response)
    except Exception as exc:
        # Do not echo request bodies, response bodies or bearer credentials.
        raise AuthError("Falló el intercambio HTTPS con OpenAI") from None
    if not isinstance(result, dict):
        raise AuthError("Respuesta de autorización inválida")
    return result


def validate_id_token(token, client_id, nonce=None):
    """Verify signature, issuer, issued-client audience and optional nonce."""
    try:
        import jwt
        key = jwt.PyJWKClient(JWKS_URL).get_signing_key_from_jwt(token).key
        identity = jwt.decode(
            token, key, algorithms=["RS256"], audience=client_id,
            issuer=ISSUER, options={"require": ["sub", "exp", "iat"]},
            leeway=5,
        )
    except Exception as exc:
        raise AuthError("Firma o identidad OpenAI no válida") from None
    if not isinstance(identity.get("sub"), str) or not identity["sub"]:
        raise AuthError("Identidad OpenAI incompleta")
    if nonce is not None and not hmac.compare_digest(
        str(identity.get("nonce", "")), nonce
    ):
        raise AuthError("Nonce OAuth distinto al solicitado")
    return identity


def _validate_grant(tokens):
    scopes = set(str(tokens.get("scope", "")).split())
    if SHARING_SCOPE not in scopes or "resource.invoke" not in scopes:
        raise AuthError("No se autorizó uso del plan de ChatGPT")
    if tokens.get("token_type", "Bearer").lower() != "bearer":
        raise AuthError("Tipo de credencial OAuth inesperado")
    if not tokens.get("access_token"):
        raise AuthError("OpenAI no emitió un token de acceso")
    return scopes


def _record(tokens, client_id, identity, previous=None):
    scopes = _validate_grant(tokens)
    if previous and identity["sub"] != previous["subject"]:
        raise AuthError("La identidad no coincide con el perfil anterior")
    if not tokens.get("refresh_token"):
        raise AuthError("Sin token renovable: no se activará el servicio")
    expiration = int(tokens.get("expires_in", 0))
    if not 0 < expiration <= 86400:
        raise AuthError("Vigencia OAuth inesperada")
    return {
        "issuer": ISSUER,
        "client_id": client_id,
        "subject": identity["sub"],
        "email": identity.get("email", ""),
        "ext_agent_host_id": host_id(),
        "id_token": tokens.get("id_token") or (previous or {}).get("id_token"),
        "access_token": tokens["access_token"],
        "refresh_token": tokens["refresh_token"],
        "scopes": sorted(scopes),
        "expires_at": int(time.time()) + expiration,
    }


def login(open_browser=True, wait_seconds=300):
    saved = profile()
    host_id()
    server = HTTPServer(("127.0.0.1", 0), _Callback)
    server.timeout = wait_seconds
    try:
        auth_url, state, nonce, verifier, redirect = make_authorization(
            server.server_port, saved)
        server.expected_state = state
        server.callback = None
        # A reauthorization URL can contain id_token_hint; do not print it.
        if saved:
            if not open_browser:
                raise AuthError("La reautorización necesita el navegador en T160")
            print("Abriendo reautorización de ChatGPT en el navegador de T160.")
        else:
            print("Abre este enlace en un navegador de T160:")
            print(auth_url)
        if open_browser:
            if not webbrowser.open(auth_url):
                print("El navegador no se abrió automáticamente.")
        server.handle_request()
        result = server.callback
        if result is None:
            raise AuthError("Tiempo agotado sin autorizar la cuenta")
        if "error" in result:
            raise AuthError(result["error"])
        issued = result.get("client_id")
        if saved:
            client_id = saved["client_id"]
            if issued and issued != client_id:
                raise AuthError("El cliente OAuth de retorno no coincide")
        else:
            if not issued or not re.fullmatch(r"oaiapp_[A-Za-z0-9_-]+", issued):
                raise AuthError("La autorización no devolvió un client_id válido")
            client_id = issued
        tokens = _request_json(TOKEN_URL, {
            "grant_type": "authorization_code",
            "client_id": client_id,
            "code": result["code"],
            "code_verifier": verifier,
            "redirect_uri": redirect,
            "resource": RESOURCE,
        })
        identity = validate_id_token(tokens.get("id_token", ""), client_id,
                                     nonce=nonce)
        record = _record(tokens, client_id, identity, saved)
        _private_write(_profile_path(), record)
        print("ChatGPT conectado: autorización del plan verificada.")
        print("El puente no se ha activado ni se ha realizado inferencia.")
        return True
    finally:
        server.server_close()


def access_token():
    """Serialize rotation across threads and processes; never expose secrets."""
    root = auth_dir()
    lock_path = root / ".refresh.lock"
    fd = os.open(lock_path, os.O_CREAT | os.O_RDWR, 0o600)
    try:
        with os.fdopen(fd, "r+") as file:
            fcntl.flock(file, fcntl.LOCK_EX)
            saved = profile()
            if not saved:
                raise AuthError("Falta iniciar sesión con ChatGPT en la T160")
            if SHARING_SCOPE not in saved.get("scopes", []):
                raise AuthError("El plan de ChatGPT no está autorizado")
            if int(saved.get("expires_at", 0)) - time.time() > 180:
                return saved["access_token"]
            tokens = _request_json(TOKEN_URL, {
                "grant_type": "refresh_token",
                "client_id": saved["client_id"],
                "refresh_token": saved["refresh_token"],
                "resource": RESOURCE,
            })
            if tokens.get("id_token"):
                identity = validate_id_token(
                    tokens["id_token"], saved["client_id"])
            else:
                identity = {"sub": saved["subject"],
                            "email": saved.get("email", "")}
            updated = _record(tokens, saved["client_id"], identity, saved)
            _private_write(_profile_path(), updated)
            return updated["access_token"]
    finally:
        pass


def list_models():
    """Read available models; this does not generate chargeable text."""
    token = access_token()
    catalog = _request_json(MODELS_URL, bearer=token)
    models = catalog.get("models", [])
    if not isinstance(models, list):
        raise AuthError("Catálogo de modelos inválido")
    return [
        {"slug": x["slug"], "display_name": x.get("display_name", x["slug"])}
        for x in models
        if isinstance(x, dict) and x.get("visibility") == "list"
        and isinstance(x.get("slug"), str)
    ]


def main():
    parser = argparse.ArgumentParser(
        description="Autenticación local Surf AI, sin clave API ni facturación por uso")
    sub = parser.add_subparsers(dest="command", required=True)
    login_parser = sub.add_parser("login", help="Autorizar plan en T160")
    login_parser.add_argument("--no-browser", action="store_true")
    sub.add_parser("status", help="Estado sin mostrar tokens")
    sub.add_parser("models", help="Modelos autorizados para la cuenta")
    args = parser.parse_args()
    try:
        if args.command == "status":
            saved = profile()
            if not saved:
                print("ChatGPT no conectado; servicio mock permanece disponible.")
            else:
                print("ChatGPT registrado; permiso de plan: " +
                      ("sí" if SHARING_SCOPE in saved.get("scopes", [])
                       else "no"))
                print("Token expira en aproximadamente {} minutos.".format(
                    max(0, (int(saved.get("expires_at", 0)) -
                            int(time.time())) // 60)))
        elif args.command == "login":
            login(open_browser=not args.no_browser)
        elif args.command == "models":
            models = list_models()
            for item in models:
                print("{} — {}".format(item["slug"], item["display_name"]))
            if not models:
                print("No hay modelos disponibles para esta cuenta.")
    except AuthError as exc:
        parser.exit(1, "SIWC: " + str(exc) + "\n")


if __name__ == "__main__":
    main()
