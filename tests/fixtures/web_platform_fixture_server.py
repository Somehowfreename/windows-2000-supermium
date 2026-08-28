#!/usr/bin/env python3
"""Deterministic, ad-free web-platform and download fixture for guest tests."""

from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import argparse
import hashlib
import json
from pathlib import Path
import time


DOWNLOAD_BODY = (
    b"Supermium Windows 2000 Candidate 19 controlled download fixture\r\n"
    b"This file is deterministic and contains no executable content.\r\n"
)


def build_pdf():
    objects = [
        b"<< /Type /Catalog /Pages 2 0 R >>",
        b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
        b"/Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>",
        b"<< /Length 83 >>\nstream\nBT /F1 18 Tf 72 720 Td "
        b"(Supermium Windows 2000 PDF viewer PASS) Tj ET\nendstream",
        b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
    ]
    output = bytearray(b"%PDF-1.4\n%\xe2\xe3\xcf\xd3\n")
    offsets = [0]
    for number, obj in enumerate(objects, 1):
        offsets.append(len(output))
        output.extend(f"{number} 0 obj\n".encode("ascii"))
        output.extend(obj)
        output.extend(b"\nendobj\n")
    xref = len(output)
    output.extend(f"xref\n0 {len(objects) + 1}\n".encode("ascii"))
    output.extend(b"0000000000 65535 f \n")
    for offset in offsets[1:]:
        output.extend(f"{offset:010d} 00000 n \n".encode("ascii"))
    output.extend(
        f"trailer\n<< /Size {len(objects) + 1} /Root 1 0 R >>\n"
        f"startxref\n{xref}\n%%EOF\n".encode("ascii")
    )
    return bytes(output)


PDF_BODY = build_pdf()
SINTEL_TRAILER_PATH = Path(__file__).with_name("sintel-trailer.mp4")

MEDIA_PAGE = b"""<!doctype html>
<html lang="en">
<head><meta charset="utf-8"><title>Controlled H.264/AAC playback fixture</title></head>
<body>
  <h1>Controlled, ad-free H.264/AAC playback fixture</h1>
  <video id="fixture-video" controls preload="auto" width="640"
         src="/media/sintel-trailer.mp4"></video>
</body>
</html>
"""

PAGE = b"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>W2K Candidate 19 web-platform fixture</title>
  <style>
    :root { --accent: rgb(28, 170, 104); }
    body { font-family: sans-serif; margin: 0; color: #122; }
    main { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 12px; padding: 24px; }
    .card { container-type: inline-size; border: 2px solid var(--accent); padding: 12px; }
    .card:has(output[data-pass="true"]) { background: color-mix(in srgb, var(--accent) 12%, white); }
    @container (min-width: 180px) { .card output { font-weight: 700; } }
  </style>
</head>
<body>
  <main>
    <section class="card"><h1>Controlled modern platform fixture</h1><output id="status">STARTING</output></section>
    <section class="card"><canvas id="canvas" width="16" height="16"></canvas><div id="shadow-host"></div></section>
  </main>
  <script type="module">
    const result = { started: true };
    try {
      const response = await fetch('/fetch.json', { cache: 'no-store' });
      result.fetch = await response.json();
      result.promise = await Promise.resolve(42);
      result.bigint = (9007199254740991n + 2n).toString();
      result.optionalChaining = ({ deep: { value: 'ok' } })?.deep?.value;
      class ModernClass { #value = 19; get value() { return this.#value; } }
      result.privateField = new ModernClass().value;
      result.structuredClone = structuredClone({ nested: ['ok'] }).nested[0];
      const stream = new ReadableStream({ start(controller) { controller.enqueue('stream-ok'); controller.close(); } });
      result.stream = (await stream.getReader().read()).value;
      const wasmBytes = Uint8Array.from([0,97,115,109,1,0,0,0,1,5,1,96,0,1,127,3,2,1,0,7,7,1,3,114,117,110,0,0,10,6,1,4,0,65,42,11]);
      const wasm = await WebAssembly.instantiate(wasmBytes);
      result.wasm = wasm.instance.exports.run();
      const host = document.querySelector('#shadow-host');
      host.attachShadow({ mode: 'open' }).innerHTML = '<span id="shadow-value">shadow-ok</span>';
      result.shadowDom = host.shadowRoot.querySelector('#shadow-value').textContent;
      customElements.define('w2k-platform-element', class extends HTMLElement {});
      result.customElements = Boolean(customElements.get('w2k-platform-element'));
      const canvas = document.querySelector('#canvas');
      const context = canvas.getContext('2d');
      context.fillStyle = 'rgb(11, 22, 33)';
      context.fillRect(0, 0, 16, 16);
      result.canvasPixel = Array.from(context.getImageData(1, 1, 1, 1).data);
      localStorage.setItem('candidate19-platform', 'persist-ok');
      result.localStorage = localStorage.getItem('candidate19-platform');
      result.css = {
        grid: CSS.supports('display', 'grid'),
        customProperties: CSS.supports('--candidate19', '1'),
        has: CSS.supports('selector(:has(*))'),
        containerQueries: CSS.supports('container-type', 'inline-size')
      };
      result.apis = {
        abortController: typeof AbortController === 'function',
        intersectionObserver: typeof IntersectionObserver === 'function',
        resizeObserver: typeof ResizeObserver === 'function',
        textEncoder: typeof TextEncoder === 'function',
        webAssembly: typeof WebAssembly === 'object'
      };
      result.complete = true;
    } catch (error) {
      result.complete = false;
      result.error = String(error?.stack || error);
    }
    globalThis.__w2kPlatformResult = result;
    const output = document.querySelector('#status');
    output.dataset.pass = String(result.complete);
    output.textContent = result.complete ? 'PASS' : 'FAIL';
  </script>
</body>
</html>
"""


class Handler(BaseHTTPRequestHandler):
    server_version = "SupermiumW2KPlatformFixture/1.0"

    def _send(self, status, content_type, body, headers=None):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Cache-Control", "no-store, max-age=0")
        self.send_header("Content-Length", str(len(body)))
        for name, value in (headers or {}).items():
            self.send_header(name, value)
        self.end_headers()
        self.wfile.write(body)

    def _send_media_file(self, path):
        body = path.read_bytes()
        total = len(body)
        start = 0
        end = total - 1
        status = 200
        requested_range = self.headers.get("Range", "")
        if requested_range.startswith("bytes="):
            first, _, last = requested_range[6:].partition("-")
            if first:
                start = min(int(first), total - 1)
            if last:
                end = min(int(last), total - 1)
            if end < start:
                end = start
            status = 206
        headers = {
            "Accept-Ranges": "bytes",
            "X-Content-SHA256": hashlib.sha256(body).hexdigest(),
        }
        if status == 206:
            headers["Content-Range"] = f"bytes {start}-{end}/{total}"
        self._send(status, "video/mp4", body[start:end + 1], headers)

    def do_GET(self):
        path = self.path.split("?", 1)[0]
        print(json.dumps({
            "capturedAt": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "client": self.client_address[0],
            "path": path,
            "userAgent": self.headers.get("User-Agent"),
        }, separators=(",", ":")), flush=True)
        if path in ("/", "/standards.html"):
            self._send(200, "text/html; charset=utf-8", PAGE)
        elif path == "/media-playback.html":
            self._send(200, "text/html; charset=utf-8", MEDIA_PAGE)
        elif path == "/fetch.json":
            self._send(200, "application/json", b'{"value":"fetch-ok","number":144}\n')
        elif path == "/download/candidate19-download-fixture.bin":
            self._send(200, "application/octet-stream", DOWNLOAD_BODY, {
                "Content-Disposition": 'attachment; filename="candidate19-download-fixture.bin"',
                "X-Content-SHA256": hashlib.sha256(DOWNLOAD_BODY).hexdigest(),
            })
        elif path == "/fixture.pdf":
            self._send(200, "application/pdf", PDF_BODY, {
                "Content-Disposition": 'inline; filename="candidate19-pdf-fixture.pdf"',
                "X-Content-SHA256": hashlib.sha256(PDF_BODY).hexdigest(),
            })
        elif path == "/media/sintel-trailer.mp4" and SINTEL_TRAILER_PATH.is_file():
            self._send_media_file(SINTEL_TRAILER_PATH)
        elif path == "/favicon.ico":
            self._send(204, "image/x-icon", b"")
        else:
            self._send(404, "text/plain; charset=utf-8", b"not found\n")

    def log_message(self, _format, *_args):
        return


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8768)
    args = parser.parse_args()
    server = ThreadingHTTPServer((args.bind, args.port), Handler)
    print(json.dumps({
        "listening": args.bind,
        "port": args.port,
        "downloadSha256": hashlib.sha256(DOWNLOAD_BODY).hexdigest(),
    }), flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
