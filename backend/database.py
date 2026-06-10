import sqlite3
from pathlib import Path

DB_PATH = Path("database/edge_gateway.db")


def init_db():
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
            changed_fields TEXT NOT NULL,
            old_config TEXT NOT NULL,
            new_config TEXT NOT NULL
        )
    """)

    conn.commit()
    conn.close()


def log_config_change(timestamp, client_ip, client_mac, user_agent, changed_fields, old_config, new_config):
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()

    cursor.execute("""
        INSERT INTO config_changes (
            timestamp, client_ip, client_mac, user_agent,
            changed_fields, old_config, new_config
        ) VALUES (?, ?, ?, ?, ?, ?, ?)
    """, (
        timestamp,
        client_ip,
        client_mac,
        user_agent,
        changed_fields,
        old_config,
        new_config
    ))

    conn.commit()
    conn.close()