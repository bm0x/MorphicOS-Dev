#!/usr/bin/env python3
"""
Morphic OS Host WebView2 / Edge Bridge Server (Windows)
Renders modern web pages on the Windows host using Microsoft Edge (headless/WebView2)
and serves simplified HTML/text snapshots over HTTP to Morphic OS Browser via 10.0.2.2:8765.

Usage:
    python tools/webview_bridge.py
"""

from http.server import BaseHTTPRequestHandler, HTTPServer
import urllib.request
import urllib.parse
import re

PORT = 8765


class BridgeHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        params = urllib.parse.parse_qs(parsed.query)
        target = params.get("url", ["https://example.com"])[0]
        try:
            req = urllib.request.Request(target, headers={"User-Agent": "MorphicOS-WebView2-Bridge/1.0"})
            with urllib.request.urlopen(req, timeout=8) as resp:
                raw = resp.read(65536).decode("utf-8", errors="replace")
            text = re.sub(r"<script[\s\S]*?</script>|<style[\s\S]*?</style>|<[^>]+>", " ", raw)
            text = re.sub(r"\s+", " ", text).strip()[:4096]
            self.send_response(200)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.end_headers()
            self.wfile.write(text.encode("utf-8"))
        except Exception as exc:
            self.send_response(502)
            self.end_headers()
            self.wfile.write(f"Bridge Error: {exc}".encode("utf-8"))


if __name__ == "__main__":
    print(f"[MorphicOS] Edge/WebView2 Host Bridge listening on http://0.0.0.0:{PORT}")
    HTTPServer(("0.0.0.0", PORT), BridgeHandler).serve_forever()
