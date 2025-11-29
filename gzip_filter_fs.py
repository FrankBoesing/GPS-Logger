import os, gzip, shutil, hashlib
from pathlib import Path

Import("env")

PROJECT_DIR = Path(env.subst("$PROJECT_DIR"))
SRC_DATA = PROJECT_DIR / "data"
DST_DATA = PROJECT_DIR / ".pio" / "data_gz_temp"

COMPRESS = {".css", ".js", ".json", ".txt"}
KEEP = {".html", ".jpg", ".jpeg", ".png", ".gif", ".ico", ".svg", ".bin", ".dat", ".vic"}

def hash_dir(p: Path):
    h = hashlib.sha1()
    for r, _, fs in os.walk(p):
        for f in sorted(fs):
            fp = Path(r)/f
            h.update(str(fp.relative_to(p)).encode())
            h.update(str(fp.stat().st_mtime).encode())
    return h.hexdigest()

def build():
    if not SRC_DATA.exists():
        print(f"[WARN] Kein {SRC_DATA}-Verzeichnis gefunden.")
        return
    hfile = DST_DATA / "../.datahash"
    old = hfile.read_text() if hfile.exists() else ""
    new = hash_dir(SRC_DATA)
    if old == new:
        print("[INFO] Keine Änderungen erkannt – verwende vorhandenes komprimiertes FS.")
        return
    print(f"[INFO] Erstelle komprimiertes FS in {DST_DATA}")
    if DST_DATA.exists():
        shutil.rmtree(DST_DATA)
    DST_DATA.mkdir(parents=True)
    for r, _, fs in os.walk(SRC_DATA):
        for f in fs:
            fp = Path(r)/f
            ext = fp.suffix.lower()
            rel = fp.relative_to(SRC_DATA)
            if ext in COMPRESS:
                out = DST_DATA / rel.with_suffix(rel.suffix + ".gz")
                out.parent.mkdir(parents=True, exist_ok=True)
                with open(fp, "rb") as i, gzip.open(out, "wb", compresslevel=9) as o:
                    shutil.copyfileobj(i, o)
                print(f"[GZ] {rel} → {out.relative_to(PROJECT_DIR)}")
            elif ext in KEEP or ext.endswith(".gz"):
                out = DST_DATA / rel
                out.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(fp, out)
                print(f"[COPY] {rel}")
            else:
                print(f"[SKIP] {rel}")
    hfile.write_text(new)

build()
