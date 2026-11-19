"""Offline Codex App Server integration tests. NEVER launch a real agent."""
import io
import os
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock

from t160 import bridge, catalog, codex_adapter


class FakeServer:
    calls = []
    requests = []
    incoming = []
    def __init__(self, token, timeout=190):
        self.token = token
        self.next_id = 0
        self.sent = []
        type(self).calls.append(self)

    def request(self, method, params):
        type(self).requests.append((method, params))
        self.next_id += 1
        if method == "initialize":
            return {"userAgent": "test"}
        if method == "thread/start":
            return {"thread": {"id": "test-thread-xyz"}}
        if method == "thread/resume":
            return {"thread": {"id": params["threadId"]}}
        raise AssertionError(method)

    def send(self, value):
        self.sent.append(value)

    def receive(self):
        return type(self).incoming.pop(0)

    def close(self):
        self.closed = True


class CodexAdapterTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.old_path = catalog.DB_PATH
        catalog.DB_PATH = str(Path(self.tmp.name)/"sessions.sqlite3")
        self.env = mock.patch.dict(os.environ, {
            "SURF_CODEX_TEXT_ONLY": "1",
            "SURF_CODEX_MODEL": "gpt-6.2-sol",
            "SURF_CODEX_HOME": str(Path(self.tmp.name)/"codex-home"),
            "SURF_CODEX_CWD": str(Path(self.tmp.name)/"empty-workspace"),
            "OPENAI_API_KEY": "must-not-be-used",
            "ANTHROPIC_API_KEY": "must-not-be-used",
        })
        self.env.start()
        self.actual_server_class = codex_adapter.AppServer
        self.adapter = mock.patch.object(
            codex_adapter, "AppServer", FakeServer)
        self.adapter.start()
        self.auth = mock.patch.object(
            codex_adapter.siwc_auth, "access_token",
            return_value="fake-oauth-token")
        self.auth.start()
        FakeServer.calls = []
        FakeServer.requests = []
        FakeServer.incoming = []

    def tearDown(self):
        self.auth.stop()
        self.adapter.stop()
        self.env.stop()
        catalog.DB_PATH = self.old_path
        self.tmp.cleanup()

    def events(self):
        return [
            {"id": 3, "result": {"turn": {"id": "t1"}}},
            {"method": "item/agentMessage/delta", "params": {
                "delta": "Texto Codex"}},
            {"method": "turn/completed", "params": {
                "turn": {"status": "completed"}}},
        ]

    def test_first_turn_and_resume_same_thread_without_repeating(self):
        FakeServer.incoming = self.events()
        self.assertEqual(
            "".join(codex_adapter.stream_text_only("session-1", "Pregunta 1")),
            "Texto Codex",
        )
        self.assertEqual(
            catalog.get_codex_thread("session-1"), "test-thread-xyz")
        self.assertEqual(FakeServer.requests[1][0], "thread/start")
        options = FakeServer.requests[1][1]
        self.assertEqual(options["sandbox"], "read-only")
        self.assertEqual(options["approvalPolicy"], "never")
        self.assertEqual(options["model"], "gpt-6.2-sol")
        turn = FakeServer.calls[-1].sent[-1]
        self.assertEqual(turn["method"], "turn/start")
        self.assertEqual(turn["params"]["sandboxPolicy"]["type"],
                         "readOnly")
        self.assertEqual(turn["params"]["approvalPolicy"], "never")
        self.assertEqual(FakeServer.calls[-1].token, "fake-oauth-token")
        self.assertTrue(FakeServer.calls[-1].closed)

        FakeServer.incoming = self.events()
        self.assertEqual(
            "".join(codex_adapter.stream_text_only("session-1", "Pregunta 2")),
            "Texto Codex",
        )
        self.assertEqual(FakeServer.requests[3][0], "thread/resume")
        self.assertEqual(FakeServer.requests[3][1]["threadId"],
                         "test-thread-xyz")

    def test_codex_home_has_no_user_project_access(self):
        args = codex_adapter.CODEX_FLAGS
        self.assertIn("features.shell_tool=false", args)
        self.assertIn("features.apps=false", args)
        self.assertIn("features.plugins=false", args)
        self.assertIn("agents.enabled=false", args)
        self.assertIn("features.request_permissions_tool=false", args)
        safe = codex_adapter.private_environment("test-oauth")
        self.assertEqual(safe["ACCESS_TOKEN"], "test-oauth")
        self.assertNotIn("OPENAI_API_KEY", safe)
        self.assertNotIn("ANTHROPIC_API_KEY", safe)
        self.assertNotIn("CODEX_API_KEY", safe)
        self.assertEqual(
            os.stat(safe["CODEX_HOME"]).st_mode & 0o777, 0o700)
        self.assertNotIn("alphasmart", str(codex_adapter.isolated_cwd()))

    def test_fail_closed_without_feature_flag_or_credentials(self):
        os.environ["SURF_CODEX_TEXT_ONLY"] = "0"
        with self.assertRaises(codex_adapter.CodexError):
            list(codex_adapter.stream_text_only("a", "hola"))
        self.assertEqual(len(FakeServer.calls), 0)

    def test_disable_all_approvals(self):
        self.assertEqual(
            codex_adapter.deny_server_request({
                "id": 4, "method": "item/commandExecution/requestApproval"
            })["result"]["decision"], "decline")
        self.assertEqual(
            codex_adapter.deny_server_request({
                "id": 5, "method": "item/fileChange/requestApproval"
            })["result"]["decision"], "decline")
        self.assertEqual(
            codex_adapter.deny_server_request({
                "id": 6, "method": "item/permissions/requestApproval"
            })["result"]["permissions"], {})
        self.assertIn("error", codex_adapter.deny_server_request(
            {"id": 7, "method": "unrecognized/serverRequest"}))

    def test_noncompleted_turn_is_error_not_saved(self):
        FakeServer.incoming = [
            {"id": 3, "result": {"turn": {"id": "t1"}}},
            {"method": "turn/completed", "params": {
                "turn": {"status": "failed"}}},
        ]
        with self.assertRaises(codex_adapter.CodexError):
            list(codex_adapter.stream_text_only("failed", "Test"))
        self.assertTrue(FakeServer.calls[-1].closed)

    def test_thread_binding_cannot_be_redirected(self):
        catalog.set_codex_thread("session", "thread-a")
        with self.assertRaises(ValueError):
            catalog.set_codex_thread("session", "thread-b")
        self.assertEqual(catalog.get_codex_thread("session"), "thread-a")
        catalog.append_pair("codex", "session", "t", "q", "a")
        catalog.delete_session("codex", "session")
        self.assertIsNone(catalog.get_codex_thread("session"))

    def test_command_event_refused_before_sending_to_surf(self):
        fake = self.actual_server_class.__new__(self.actual_server_class)
        fake.started_at = time.monotonic()
        fake.timeout = 30
        fake.process = mock.Mock()
        fake.process.stdout = io.BytesIO(
            b'{"method":"item/started","params":{"item":{"type":"commandExecution"}}}\n')
        fake.process.stdin = io.BytesIO()
        with mock.patch("t160.codex_adapter.select.select",
                        return_value=([fake.process.stdout], [], [])):
            with self.assertRaises(codex_adapter.CodexError):
                fake.receive()

    def test_bridge_supports_separate_codex_provider(self):
        # The bridge still does not launch Codex on import.
        with mock.patch.object(codex_adapter, "stream_text_only",
                               return_value=iter(["Respuesta"])) as stream:
            fragments=list(bridge.stream_provider(
                "codex", [{"role": "user", "content": "hola"}],
                session="session-c"))
        self.assertEqual(fragments, ["Respuesta"])
        stream.assert_called_once_with("session-c", "hola")


if __name__ == "__main__":
    unittest.main()
