import sqlite3
from pathlib import Path

DB_PATH = Path(__file__).parent / "database" / "edgegateway.db"

def initdb():
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()

    cursor.execute("""
    CREATE TABLE IF NOT EXISTS systeminfo (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        key TEXT UNIQUE NOT NULL,
        value TEXT NOT NULL
    )
    """)

    cursor.execute("""
    CREATE TABLE IF NOT EXISTS configchanges (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        timestamp TEXT NOT NULL,
        clientip TEXT,
        clientmac TEXT,
        useragent TEXT,
        changedfields TEXT NOT NULL,
        oldconfig TEXT NOT NULL,
        newconfig TEXT NOT NULL
    )
    """)

    conn.commit()
    conn.close()

def logconfigchange(timestamp, clientip, clientmac, useragent, changedfields, oldconfig, newconfig):
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    cursor.execute("""
    INSERT INTO configchanges
    (timestamp, clientip, clientmac, useragent, changedfields, oldconfig, newconfig)
    VALUES (?, ?, ?, ?, ?, ?, ?)
    """, (timestamp, clientip, clientmac, useragent, changedfields, oldconfig, newconfig))
    conn.commit()
    conn.close()
