"""T160 SQLite conversation index; list metadata only (20 per page).

Newest activity first (updated DESC, id DESC). No messages are transmitted
when listing. Renames and soft deletes are scoped to (provider, session).
"""
import os
from contextlib import contextmanager
import sqlite3
import threading
import time

DB_PATH = os.environ.get(
    "SURF_AI_DB", os.path.expanduser("~/Projects/surf-ai/sessions.sqlite3"))
PAGE_SIZE = 20
LOCK = threading.RLock()


@contextmanager
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
    db.execute("""CREATE TABLE IF NOT EXISTS messages (
        id TEXT NOT NULL, provider TEXT NOT NULL, idx INTEGER NOT NULL,
        role TEXT NOT NULL, content TEXT NOT NULL,
        PRIMARY KEY(id, provider, idx))""")
    try:
        with db:
            yield db
    finally:
        db.close()


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


def delete_session(provider, session):
    with LOCK, connection() as db:
        cur = db.execute("""DELETE FROM conversations
             WHERE provider=? AND id=?""", (provider, session))
        if cur.rowcount != 1:
            raise ValueError("Conversación no encontrada")
        db.execute("""DELETE FROM messages WHERE provider=? AND id=?""",
                   (provider, session))


def append_pair(provider, session, title, user_text, assistant_text):
    """Atomically persist one complete turn and update recent ordering."""
    now = time.time_ns()
    with LOCK, connection() as db:
        db.execute("""INSERT INTO conversations (id,provider,title,updated)
                      VALUES (?,?,?,?)
                      ON CONFLICT(id,provider) DO UPDATE
                      SET updated=excluded.updated, deleted=0""",
                   (session, provider, title[:100], now))
        idx = db.execute("""SELECT COALESCE(MAX(idx),-1)+1 FROM messages
            WHERE id=? AND provider=?""", (session, provider)).fetchone()[0]
        db.executemany("""INSERT INTO messages (id,provider,idx,role,content)
            VALUES (?,?,?,?,?)""",
            [(session, provider, idx, "user", user_text),
             (session, provider, idx+1, "assistant", assistant_text)])


def get_messages(provider, session, offset=0, limit=5):
    """Return oldest-to-newest messages for the requested reverse page."""
    offset = max(0, min(1000000, int(offset)))
    limit = max(1, min(5, int(limit)))
    with LOCK, connection() as db:
        exists = db.execute("""SELECT 1 FROM conversations
            WHERE provider=? AND id=? AND deleted=0""",
            (provider, session)).fetchone()
        if not exists:
            return [], False
        rows = db.execute("""SELECT role,content FROM messages
            WHERE id=? AND provider=? ORDER BY idx DESC LIMIT ? OFFSET ?""",
            (session, provider, limit+1, offset)).fetchall()
    return list(reversed(rows[:limit])), len(rows)>limit


def context(provider, session, limit=24):
    with LOCK, connection() as db:
        rows = db.execute("""SELECT role,content FROM messages
            WHERE id=? AND provider=? ORDER BY idx DESC LIMIT ?""",
            (session, provider, limit)).fetchall()
    return [{"role": r, "content": c} for r,c in reversed(rows)]
