import socket
import time
import configparser
from pathlib import Path

# -------------------------------
# platformio.ini auslesen
# -------------------------------
ini_path = Path(__file__).parent.parent / "platformio.ini"
config = configparser.ConfigParser()
config.read(ini_path)

if "common" not in config:
    print("Fehler: Section [common] nicht gefunden in platformio.ini")
    exit(1)

host = config["common"].get("monitor_host", "localhost")
port = int(config["common"].get("monitor_port", 23))

print(f"Starting TCP monitor to {host}:{port}")

# -------------------------------
# Endlosschleife mit Auto-Reconnect + ANSI-Farben
# -------------------------------
while True:
    try:
        with socket.create_connection((host, port), timeout=5) as s:
            print(f"\033[32mConnected to {host}:{port}\033[0m")
            while True:
                data = s.recv(1024)
                if not data:
                    print("\033[33mConnection closed by server, reconnecting...\033[0m")
                    break
                try:
                    print(data.decode("utf-8", errors="ignore"), end="")
                except Exception:
                    print(data, end="")
    except Exception as e:
        print(f"\033[31mError: {e}, reconnecting in 2s...\033[0m")
        time.sleep(2)
