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

    def setUp(self):
        """Each test owns an empty SQLite dataset; no ordering dependencies."""
        with catalog.connection() as db:
            db.execute("DELETE FROM messages")
            db.execute("DELETE FROM conversations")

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
        catalog.append_pair("openai", key, "Antes", "Pregunta", "Respuesta")
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

    def test_reload_reads_t160_without_new_provider_call(self):
        key = "reload-without-ask"
        catalog.append_pair("openai", key, "AS2K diagnóstico",
                            "Primera consulta", "Respuesta guardada")
        out = self.rpc("LIST")
        listing = next(v for t, v in out if t == "SESSION" and v.startswith(key + "\t"))
        self.assertIn("\tready\t", listing)
        before = catalog.context("openai", key)
        response = self.rpc("RELOAD", session=key)
        self.assertEqual(response[-1][0], "DONE")
        metadata = next(v for t, v in response if t == "META")
        self.assertTrue(metadata.startswith("idle\t"))
        self.assertIn("AS2K diagnóstico", metadata)
        self.assertEqual(len([1 for kind, _ in response if kind == "ITEM"]), 2)
        self.assertEqual(catalog.context("openai", key), before)
        after = self.rpc("LIST")
        self.assertTrue(any("\tidle\t" in v for t,v in after
                            if t == "SESSION" and v.startswith(key + "\t")))

    def test_duplicate_ask_cannot_poison_active_status(self):
        key = "concurrent-ask"
        catalog.begin_request("openai", key, "Consulta existente")
        reply = self.rpc("ASK", session=key,
                         argument=base64.b64encode(b"new attempt").decode())
        self.assertEqual(reply[-1][0], "ERROR")
        listed = self.rpc("LIST")
        self.assertTrue(any(key + "\t" in value and "\tworking\t" in value
                            for kind, value in listed if kind == "SESSION"))
        catalog.mark_failed("openai", key)

    def test_second_client_update_visible_after_manual_refresh(self):
        """An external client writes to T160; refreshing does not repeat ASK."""
        provider, key = "openai", "shared-between-two-clients"
        catalog.append_pair(provider, key, "AlphaSmart", "Cliente A",
                            "Respuesta A")
        # Simulate a second frontend resuming exactly the same T160 session.
        catalog.append_pair(provider, key, "AlphaSmart", "Cliente B",
                            "Respuesta B")
        context_before = catalog.context(provider, key)
        self.assertEqual(len(context_before), 4)
        response = self.rpc("RELOAD", provider=provider, session=key)
        self.assertEqual(response[-1][0], "DONE")
        bodies = [text for kind, text in response if kind == "ITEM"]
        self.assertEqual(len(bodies), 4)
        self.assertTrue(any(text == "user\tCliente B"
                            for text in bodies))
        self.assertTrue(any(text == "assistant\tRespuesta B"
                            for text in bodies))
        self.assertEqual(catalog.context(provider, key), context_before)
        # The next ASK is built using the freshly persisted shared context.
        self.assertEqual(catalog.context(provider, key)[-1],
                         {"role": "assistant", "content": "Respuesta B"})

    def test_reload_during_working_does_not_resend(self):
        key = "reload-while-working"
        catalog.begin_request("openai", key, "Consulta en curso")
        before = catalog.context("openai", key)
        out = self.rpc("RELOAD", session=key)
        self.assertEqual(out[-1][0], "DONE")
        self.assertEqual(len([1 for kind, _ in out if kind == "ITEM"]), 0)
        self.assertTrue(next(value for kind, value in out
                             if kind == "META").startswith("working\t"))
        self.assertEqual(catalog.context("openai", key), before)
        catalog.mark_failed("openai", key)
        out = self.rpc("RELOAD", session=key)
        self.assertTrue(next(value for kind, value in out
                             if kind == "META").startswith("error\t"))

    def test_reload_unknown_and_invalid_argument(self):
        response = self.rpc("RELOAD", session="not-existing")
        self.assertEqual(response[-1][0], "ERROR")
        response = self.rpc("RELOAD", session="not-existing",
                            argument="bad")
        self.assertEqual(response[-1][0], "ERROR")

    def test_status_transitions_and_unread(self):
        key = "test-working-state"
        catalog.begin_request("anthropic", key, "Prueba de proceso")
        listed = self.rpc("LIST", provider="anthropic")
        self.assertTrue(any(key + "\t" in value and "\tworking\t" in value
                            for kind, value in listed if kind == "SESSION"))
        with self.assertRaises(ValueError):
            catalog.begin_request("anthropic", key, "duplicado")
        catalog.mark_failed("anthropic", key)
        listed = self.rpc("LIST", provider="anthropic")
        self.assertTrue(any(key + "\t" in value and "\terror\t" in value
                            for kind, value in listed if kind == "SESSION"))
        catalog.begin_request("anthropic", key, "intento nuevo")
        catalog.append_pair("anthropic", key, "intento nuevo", "Pregunta",
                            "Respuesta completada")
        listed = self.rpc("LIST", provider="anthropic")
        self.assertTrue(any(key + "\t" in value and "\tready\t" in value
                            for kind, value in listed if kind == "SESSION"))
        response = self.rpc("HISTORY", provider="anthropic", session=key,
                            argument="0u")
        self.assertEqual(response[-1][0], "DONE")
        listed = self.rpc("LIST", provider="anthropic")
        self.assertTrue(any(key + "\t" in value and "\tready\t" in value
                            for kind, value in listed if kind == "SESSION"))
        self.rpc("HISTORY", provider="anthropic", session=key,
                 argument="0")
        listed = self.rpc("LIST", provider="anthropic")
        self.assertTrue(any(key + "\t" in value and "\tidle\t" in value
                            for kind, value in listed if kind == "SESSION"))

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
