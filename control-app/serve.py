#!/usr/bin/env python3
"""Range-capable local server for the control-app field kit."""

from __future__ import annotations

import argparse
import os
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


class RangeRequestHandler(SimpleHTTPRequestHandler):
    def end_headers(self) -> None:
        self.send_header("Accept-Ranges", "bytes")
        super().end_headers()

    def send_head(self):  # type: ignore[override]
        path = self.translate_path(self.path)
        if os.path.isdir(path):
            return super().send_head()

        file_path = Path(path)
        if not file_path.exists():
            self.send_error(404, "File not found")
            return None

        size = file_path.stat().st_size
        range_header = self.headers.get("Range")
        if not range_header:
            return super().send_head()

        try:
            unit, value = range_header.split("=", 1)
            if unit.strip() != "bytes":
                raise ValueError
            start_text, end_text = value.split("-", 1)
            if start_text:
                start = int(start_text)
                end = int(end_text) if end_text else size - 1
            else:
                suffix_len = int(end_text)
                if suffix_len <= 0:
                    raise ValueError
                start = max(size - suffix_len, 0)
                end = size - 1
            if start < 0 or end < start or start >= size:
                raise ValueError
            end = min(end, size - 1)
        except ValueError:
            self.send_response(416)
            self.send_header("Content-Range", f"bytes */{size}")
            self.send_header("Content-Length", "0")
            self.end_headers()
            return None

        f = file_path.open("rb")
        f.seek(start)
        self.range = (start, end)
        self.send_response(206)
        self.send_header("Content-Type", self.guess_type(path))
        self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
        self.send_header("Content-Length", str(end - start + 1))
        self.send_header("Last-Modified", self.date_time_string(file_path.stat().st_mtime))
        self.end_headers()
        return f

    def copyfile(self, source, outputfile) -> None:  # type: ignore[override]
        byte_range = getattr(self, "range", None)
        if byte_range is None:
            return super().copyfile(source, outputfile)

        start, end = byte_range
        remaining = end - start + 1
        try:
            while remaining > 0:
                chunk = source.read(min(64 * 1024, remaining))
                if not chunk:
                    break
                outputfile.write(chunk)
                remaining -= len(chunk)
        finally:
            self.range = None


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--directory", default=str(Path(__file__).resolve().parent))
    args = parser.parse_args()

    handler = partial(RangeRequestHandler, directory=args.directory)
    server = ThreadingHTTPServer((args.host, args.port), handler)
    print(f"Serving {args.directory} at http://{args.host}:{args.port}/index.html")
    server.serve_forever()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
