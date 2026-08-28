#!/usr/bin/env python3
"""Controlled, ad-free fixture used to verify blocker request and cosmetic rules."""

from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import argparse
import json
import time


PAGE = b"""<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <title>Controlled blocker fixture</title>
  <script>window.fixtureStarted = true;</script>
  <script src="/allowed.js"></script>
  <script src="/pagead/conversion.js"></script>
</head>
<body>
  <div id="control">CONTROL_VISIBLE</div>
  <div id="ad-banner-1">CONTROLLED_AD_PLACEHOLDER</div>
  <script>window.fixtureInlineCompleted = true;</script>
</body>
</html>
"""


class Handler(BaseHTTPRequestHandler):
    server_version = "SupermiumW2KFixture/1.0"

    def _send(self, status, content_type, body):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Cache-Control", "no-store, max-age=0")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        record = {
            "capturedAt": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "client": self.client_address[0],
            "path": self.path,
            "userAgent": self.headers.get("User-Agent"),
        }
        print(json.dumps(record, separators=(",", ":")), flush=True)
        path = self.path.split("?", 1)[0]
        if path in ("/", "/test.html"):
            self._send(200, "text/html; charset=utf-8", PAGE)
        elif path == "/allowed.js":
            self._send(200, "application/javascript", b"window.allowedScriptLoaded = true;\n")
        elif path == "/pagead/conversion.js":
            self._send(200, "application/javascript", b"window.blockTargetScriptLoaded = true;\n")
        elif path == "/favicon.ico":
            self._send(204, "image/x-icon", b"")
        else:
            self._send(404, "text/plain; charset=utf-8", b"not found\n")

    def log_message(self, _format, *_args):
        return


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8767)
    args = parser.parse_args()
    server = ThreadingHTTPServer((args.bind, args.port), Handler)
    print(json.dumps({"listening": args.bind, "port": args.port}), flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
