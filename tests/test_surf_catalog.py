"""Tests for lazy conversation list, persistence, rename/delete and paging.
No network to OpenAI or Anthropic, no real provider keys.
"""
import base64
import os
import socket
import tempfile
import threading
import unittest

from t160 import bridge, catalog


class CatalogTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.original_db_path = catalog.DB_PATH
        catalog.DB_PATH = os.path.join(cls.tmp.name, "sessions.sqlite3")
        cls.server = bridge.Bridge(("127.0.0.1", 0), bridge.Handler)
        cls.server.mock = True
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join(timeout=3)
        catalog.DB_PATH = cls.original_db_path
        cls.tmp.cleanup()

    def rpc(self, op, provider="openai", session="catalog-test", argument="-"):
        wire = "\t".join((op, provider, session, argument)) + "\n"
        result = []
        with socket.create_connection(self.server.server_address, timeout=3) as sock:
            sock.sendall(wire.encode("ascii"))
            reader = sock.makefile("rb")
            while True:
                raw = reader.readline()
                if not raw:
                    break
                chunks = raw.rstrip(b"\r\n").split(b"\t", 1)
                event = chunks[0].decode("ascii")
                value = (base64.b64decode(chunks[1]).decode("utf-8")
                         if len(chunks) == 2 else None)
                result.append((event, value))
                if event in ("DONE", "ERROR"):
                    break
        return result

    def test_newest_first_and_metadata_only(self):
        for n in range(26):
            catalog.append_pair("openai", "conv-%03d" % n,
                                "Título %03d" % n,
                                "Mensaje secreto %03d" % n, "Respuesta")
        result = self.rpc("LIST")
        entries = [v for t, v in result if t == "SESSION"]
        self.assertEqual(len(entries), 20)
        self.assertEqual(entries[0].split("\t")[0], "conv-025")
        self.assertTrue(all("Mensaje secreto" not in item for item in entries))
        next_cursor = next(v for t, v in result if t == "NEXT")
        self.assertNotEqual(next_cursor, "-")
        next_result = self.rpc("LIST", argument=next_cursor)
        self.assertEqual(len([v for t, v in next_result if t == "SESSION"]), 6)

    def test_rename_and_delete(self):
        key = "conv-025"
        title = "Nuevo título técnico"
        out = self.rpc("RENAME", session=key,
                       argument=base64.b64encode(title.encode()).decode())
        self.assertEqual(out[-1][0], "DONE")
        rows = self.rpc("LIST")
        self.assertTrue(any(title in v for t,v in rows if t == "SESSION"))
        out = self.rpc("DELETE", session=key)
        self.assertEqual(out[-1][0], "DONE")
        rows = self.rpc("LIST")
        self.assertFalse(any(key in v for t,v in rows if t == "SESSION"))

    def test_history_five_message_window(self):
        catalog.append_pair("anthropic", "history-test", "Historial",
                            "Primera línea\nSegunda línea", "Respuesta 1")
        for i in range(3):
            catalog.append_pair("anthropic", "history-test", "Historial",
                                "Consulta %d" % i, "Respuesta %d" % (i+2))
        out = self.rpc("HISTORY", provider="anthropic",
                       session="history-test", argument="0")
        entries = [v for t,v in out if t == "ITEM"]
        self.assertEqual(len(entries), 5)
        self.assertIn("Respuesta 4", entries[-1])
        self.assertEqual(next(v for t,v in out if t == "MORE"), "1")
        out = self.rpc("HISTORY", provider="anthropic",
                       session="history-test", argument="5")
        self.assertEqual(len([v for t,v in out if t == "ITEM"]), 3)


if __name__ == "__main__":
    unittest.main()
