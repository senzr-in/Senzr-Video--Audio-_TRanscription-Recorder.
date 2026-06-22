import sqlite3
from pathlib import Path

DB_PATH = Path(__file__).parent / "database" / "edge_gateway.db"


def init_db():
    DB_PATH.parent.mkdir(parents=True, exist_ok=True)
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()

    cursor.execute("""
        CREATE TABLE IF NOT EXISTS system_info (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            key TEXT UNIQUE NOT NULL,
            value TEXT NOT NULL
        )
    """)

    cursor.execute("""
        CREATE TABLE IF NOT EXISTS config_changes (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp TEXT NOT NULL,
            client_ip TEXT,
            client_mac TEXT,
            user_agent TEXT,
            changed_fields TEXT,
            old_config TEXT,
            new_config TEXT
        )
    """)

    conn.commit()
    conn.close()
