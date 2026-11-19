"""Offline protocol test for the T160 bridge; no provider calls or credentials."""
import base64
import socket
import threading
import unittest

from t160 import bridge


class MockProtocolTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = bridge.Bridge(("127.0.0.1", 0), bridge.Handler)
        cls.server.mock = True
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join(timeout=3)

    def exchange(self, provider="openai", session="test-1", question="hola"):
        payload = base64.b64encode(question.encode("utf-8"))
        frame = b"ASK\t" + provider.encode("ascii") + b"\t" + session.encode("ascii") + b"\t" + payload + b"\n"
        with socket.create_connection(self.server.server_address, timeout=3) as sock:
            sock.sendall(frame)
            stream = sock.makefile("rb")
            events = []
            while True:
                raw = stream.readline()
                if not raw:
                    break
                parts = raw.rstrip(b"\r\n").split(b"\t", 1)
                events.append((parts[0].decode("ascii"),
                               base64.b64decode(parts[1]).decode("utf-8")
                               if len(parts) == 2 else None))
                if parts[0] in (b"DONE", b"ERROR"):
                    break
            return events

    def test_mock_gpt_markdown(self):
        response = self.exchange()
        self.assertEqual(response[-1][0], "DONE")
        text = "".join(data or "" for kind, data in response if kind == "DELTA")
        self.assertIn("| GTK3 |", text)
        self.assertIn("Prueba ARMv7", text)

    def test_mock_claude(self):
        response = self.exchange(provider="anthropic", session="test-2")
        self.assertEqual(response[-1][0], "DONE")

    def test_invalid_provider(self):
        response = self.exchange(provider="other")
        self.assertEqual(response[-1][0], "ERROR")

    def test_utf8(self):
        response = self.exchange(question="¿áéíóú 🤖?")
        self.assertEqual(response[-1][0], "DONE")


if __name__ == "__main__":
    unittest.main()
