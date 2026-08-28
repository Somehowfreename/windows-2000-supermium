const fs = await import("node:fs");

const base = process.argv[2] || "http://127.0.0.1:9223";
const url = process.argv[3];
const downloadPath = process.argv[4];
const outputPath = process.argv[5];
if (!url || !downloadPath) {
  throw new Error("usage: cdp_download_probe.js CDP_BASE URL GUEST_DOWNLOAD_PATH [OUTPUT]");
}

const targets = await (await fetch(`${base}/json/list`)).json();
const target = targets.find((item) => item.type === "page");
if (!target) throw new Error("No page target found");

const ws = new WebSocket(target.webSocketDebuggerUrl);
const pending = new Map();
const events = [];
let nextId = 1;
let complete;
const completed = new Promise((resolve) => { complete = resolve; });

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
  if (message.method === "Page.downloadWillBegin" ||
      message.method === "Page.downloadProgress" ||
      message.method === "Browser.downloadWillBegin" ||
      message.method === "Browser.downloadProgress") {
    events.push({ method: message.method, params: message.params });
    if (message.params?.state === "completed" || message.params?.state === "canceled") {
      complete(message.params);
    }
  }
};

await new Promise((resolve, reject) => {
  ws.onopen = resolve;
  ws.onerror = reject;
});

const report = {
  capturedAt: new Date().toISOString(),
  base,
  requestedUrl: url,
  downloadPath,
  targetId: target.id,
};

try {
  await command("Page.enable");
  try {
    report.behaviorMethod = "Browser.setDownloadBehavior";
    await command("Browser.setDownloadBehavior", {
      behavior: "allow",
      downloadPath,
      eventsEnabled: true,
    });
  } catch (error) {
    report.behaviorMethod = "Page.setDownloadBehavior";
    report.browserBehaviorError = String(error);
    await command("Page.setDownloadBehavior", { behavior: "allow", downloadPath });
  }
  report.navigation = await command("Page.navigate", { url });
  report.completion = await Promise.race([
    completed,
    new Promise((resolve) => setTimeout(() => resolve({ timedOut: true }), 60000)),
  ]);
  report.events = events;
} catch (error) {
  report.fatalError = String(error?.stack || error);
} finally {
  report.completedAt = new Date().toISOString();
  if (outputPath) fs.writeFileSync(outputPath, `${JSON.stringify(report, null, 2)}\n`);
  console.log(JSON.stringify(report, null, 2));
  ws.close();
}
