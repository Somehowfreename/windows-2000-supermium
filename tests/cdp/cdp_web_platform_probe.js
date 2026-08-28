const fs = await import("node:fs");

const base = process.argv[2] || "http://127.0.0.1:9223";
const url = process.argv[3] || "http://10.0.2.2:8768/standards.html";
const outputPath = process.argv[4];
const screenshotPath = process.argv[5];

const targets = await (await fetch(`${base}/json/list`)).json();
const target = targets.find((item) => item.type === "page");
if (!target) throw new Error("No page target found");

const ws = new WebSocket(target.webSocketDebuggerUrl);
const pending = new Map();
const events = [];
let nextId = 1;

function command(method, params = {}) {
  const id = nextId++;
  return new Promise((resolve, reject) => {
    pending.set(id, { resolve, reject });
    ws.send(JSON.stringify({ id, method, params }));
  });
}

ws.onmessage = (event) => {
  const message = JSON.parse(event.data);
  if (message.id && pending.has(message.id)) {
    const { resolve, reject } = pending.get(message.id);
    pending.delete(message.id);
    if (message.error) reject(new Error(JSON.stringify(message.error)));
    else resolve(message.result);
    return;
  }
  if (["Runtime.exceptionThrown", "Runtime.consoleAPICalled", "Log.entryAdded"].includes(message.method)) {
    events.push({ method: message.method, params: message.params });
  }
};

await new Promise((resolve, reject) => {
  ws.onopen = resolve;
  ws.onerror = reject;
});

const report = { capturedAt: new Date().toISOString(), base, url, targetId: target.id };
try {
  await command("Page.enable");
  await command("Runtime.enable");
  await command("Log.enable");
  report.navigation = await command("Page.navigate", { url });
  const expression = String.raw`(async () => {
    const delay = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
    const deadline = performance.now() + 60000;
    while (!globalThis.__w2kPlatformResult && performance.now() < deadline) await delay(100);
    const fixture = globalThis.__w2kPlatformResult || { complete: false, error: "fixture timeout" };
    const idb = await new Promise((resolve) => {
      if (!globalThis.indexedDB) return resolve({ available: false });
      const request = indexedDB.open("candidate19-platform-probe", 1);
      request.onupgradeneeded = () => request.result.createObjectStore("values");
      request.onerror = () => resolve({ available: true, error: request.error?.name || "open failed" });
      request.onsuccess = () => {
        const db = request.result;
        const write = db.transaction("values", "readwrite");
        write.objectStore("values").put("indexeddb-ok", "status");
        write.onabort = () => resolve({ available: true, error: write.error?.name || "write failed" });
        write.oncomplete = () => {
          const read = db.transaction("values", "readonly").objectStore("values").get("status");
          read.onerror = () => resolve({ available: true, error: read.error?.name || "read failed" });
          read.onsuccess = () => resolve({ available: true, value: read.result });
        };
      };
    });
    const media = document.createElement("video");
    const mediaSupport = {
      h264AvcAac: media.canPlayType('video/mp4; codecs="avc1.42E01E, mp4a.40.2"'),
      h264HighAac: media.canPlayType('video/mp4; codecs="avc1.640028, mp4a.40.2"'),
      vp9Opus: media.canPlayType('video/webm; codecs="vp9, opus"'),
      av1Opus: media.canPlayType('video/webm; codecs="av01.0.05M.08, opus"'),
      mediaSource: typeof MediaSource === "function",
      h264MediaSource: typeof MediaSource === "function" && MediaSource.isTypeSupported('video/mp4; codecs="avc1.42E01E, mp4a.40.2"')
    };
    const probeGl = (kind) => {
      const canvas = document.createElement("canvas");
      const gl = canvas.getContext(kind);
      if (!gl) return { available: false };
      const debug = gl.getExtension("WEBGL_debug_renderer_info");
      return {
        available: true,
        version: gl.getParameter(gl.VERSION),
        vendor: debug ? gl.getParameter(debug.UNMASKED_VENDOR_WEBGL) : gl.getParameter(gl.VENDOR),
        renderer: debug ? gl.getParameter(debug.UNMASKED_RENDERER_WEBGL) : gl.getParameter(gl.RENDERER)
      };
    };
    return {
      fixture,
      indexedDB: idb,
      mediaSupport,
      webgl: probeGl("webgl"),
      webgl2: probeGl("webgl2"),
      environment: {
        href: location.href,
        title: document.title,
        readyState: document.readyState,
        secureContext: isSecureContext,
        userAgent: navigator.userAgent,
        platform: navigator.platform,
        hardwareConcurrency: navigator.hardwareConcurrency,
        deviceMemory: navigator.deviceMemory || null,
        webCrypto: Boolean(globalThis.crypto?.subtle),
        webRtc: typeof RTCPeerConnection === "function",
        workers: typeof Worker === "function",
        modules: "noModule" in HTMLScriptElement.prototype,
        intl: new Intl.DateTimeFormat("en", { dateStyle: "full" }).format(new Date(0))
      },
      layout: {
        mainDisplay: getComputedStyle(document.querySelector("main")).display,
        cardBackground: getComputedStyle(document.querySelector(".card")).backgroundColor,
        statusText: document.querySelector("#status")?.textContent || ""
      }
    };
  })()`;
  const evaluated = await command("Runtime.evaluate", {
    expression,
    awaitPromise: true,
    returnByValue: true,
  });
  report.exceptionDetails = evaluated.exceptionDetails || null;
  report.result = evaluated.result?.value ?? evaluated.result;
  report.events = events;
  if (screenshotPath) {
    const screenshot = await command("Page.captureScreenshot", { format: "png", fromSurface: true });
    fs.writeFileSync(screenshotPath, Buffer.from(screenshot.data, "base64"));
    report.screenshotPath = screenshotPath;
  }
} catch (error) {
  report.fatalError = String(error?.stack || error);
} finally {
  report.completedAt = new Date().toISOString();
  const rendered = `${JSON.stringify(report, null, 2)}\n`;
  if (outputPath) fs.writeFileSync(outputPath, rendered);
  console.log(rendered);
  ws.close();
}
