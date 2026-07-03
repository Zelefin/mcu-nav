#!/usr/bin/env python3
"""Download the field-test PMTiles sidecar into control-app/.

The default source is intentionally explicit instead of scraping public raster
tiles. Pass --url when using a prepared PMTiles artifact.
"""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
from urllib.request import urlopen


DEFAULT_OUT = Path("control-app/kyiv-oblast.pmtiles")


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--url", required=True, help="HTTPS URL of the Kyiv PMTiles artifact")
    parser.add_argument("--out", default=str(DEFAULT_OUT), help="output path")
    parser.add_argument("--sha256", help="expected SHA-256 digest")
    args = parser.parse_args()

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    tmp = out.with_suffix(out.suffix + ".tmp")

    with urlopen(args.url) as response, tmp.open("wb") as f:
        while True:
            chunk = response.read(1024 * 1024)
            if not chunk:
                break
            f.write(chunk)

    digest = sha256(tmp)
    if args.sha256 and digest.lower() != args.sha256.lower():
        tmp.unlink(missing_ok=True)
        raise SystemExit(f"SHA-256 mismatch: expected {args.sha256}, got {digest}")

    tmp.replace(out)
    print(f"Wrote {out} ({out.stat().st_size} bytes)")
    print(f"sha256={digest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
