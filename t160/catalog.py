"""T160 SQLite conversation index; list metadata only (20 per page).

Newest activity first (updated DESC, id DESC). No messages are transmitted
when listing. Renames and soft deletes are scoped to (provider, session).
"""
import os
import sqlite3
import threading
import time

DB_PATH = os.environ.get(
    "SURF_AI_DB", os.path.expanduser("~/Projects/surf-ai/sessions.sqlite3"))
PAGE_SIZE = 20
LOCK = threading.RLock()


def connection():
    path = os.path.abspath(DB_PATH)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    db = sqlite3.connect(path, timeout=10)
    db.execute("""CREATE TABLE IF NOT EXISTS conversations (
        id TEXT NOT NULL, provider TEXT NOT NULL, title TEXT NOT NULL,
        updated INTEGER NOT NULL, deleted INTEGER NOT NULL DEFAULT 0,
        PRIMARY KEY (id, provider))""")
    db.execute("""CREATE INDEX IF NOT EXISTS ix_recent
        ON conversations (provider, deleted, updated DESC, id DESC)""")
    return db


def touch(provider, session, title):
    now = time.time_ns()
    with LOCK, connection() as db:
        db.execute("""INSERT INTO conversations (id,provider,title,updated)
           VALUES (?,?,?,?)
           ON CONFLICT(id,provider) DO UPDATE
           SET updated=excluded.updated, deleted=0""",
           (session, provider, title[:100], now))


def list_page(provider, before_updated=None, before_id=None):
    with LOCK, connection() as db:
        if before_updated is None:
            rows = db.execute("""SELECT id,title,updated FROM conversations
                 WHERE provider=? AND deleted=0
                 ORDER BY updated DESC, id DESC LIMIT ?""",
                 (provider, PAGE_SIZE + 1)).fetchall()
        else:
            rows = db.execute("""SELECT id,title,updated FROM conversations
                 WHERE provider=? AND deleted=0 AND
                    (updated < ? OR (updated = ? AND id < ?))
                 ORDER BY updated DESC, id DESC LIMIT ?""",
                 (provider, before_updated, before_updated, before_id,
                  PAGE_SIZE + 1)).fetchall()
    has_more = len(rows) > PAGE_SIZE
    return rows[:PAGE_SIZE], has_more


def rename(provider, session, new_title):
    title = new_title.strip()
    if not title or len(title) > 100:
        raise ValueError("Título inválido (1 a 100 caracteres)")
    with LOCK, connection() as db:
        cur = db.execute("""UPDATE conversations SET title=?
             WHERE provider=? AND id=? AND deleted=0""",
             (title, provider, session))
        if cur.rowcount != 1:
            raise ValueError("Conversación no encontrada")
    return title


def soft_delete(provider, session):
    with LOCK, connection() as db:
        cur = db.execute("""UPDATE conversations SET deleted=1
             WHERE provider=? AND id=? AND deleted=0""",
             (provider, session))
        if cur.rowcount != 1:
            raise ValueError("Conversación no encontrada")
