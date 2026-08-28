const fs = await import("node:fs");

const base = process.argv[2] || "http://127.0.0.1:9223";
const outputPath = process.argv[3];
const targets = await (await fetch(`${base}/json/list`)).json();
const target = targets.find((item) => item.type === "page");
if (!target) throw new Error("No page target found");

const ws = new WebSocket(target.webSocketDebuggerUrl);
const pending = new Map();
let nextId = 1;
let loaded;

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
  if (message.method === "Page.loadEventFired" && loaded) loaded();
};

await new Promise((resolve, reject) => {
  ws.onopen = resolve;
  ws.onerror = reject;
});

const report = { capturedAt: new Date().toISOString(), base, targetId: target.id };
try {
  await command("Page.enable");
  const load = new Promise((resolve) => { loaded = resolve; });
  report.navigation = await command("Page.navigate", { url: "chrome://extensions-internals/" });
  await Promise.race([load, new Promise((resolve) => setTimeout(resolve, 10000))]);
  const result = await command("Runtime.evaluate", {
    expression: `(() => {
      const items = JSON.parse(document.body.innerText);
      return items.map((item) => ({
        id:item.id,
        name:item.name,
        version:item.version,
        manifest_version:item.manifest_version,
        location:item.location,
        path:item.path,
        state:item.state,
        disable_reasons:item.disable_reasons,
        permissions:item.permissions,
        host_permissions:item.host_permissions,
        event_listeners:item.event_listeners,
        service_worker:item.service_worker,
      }));
    })()`,
    returnByValue: true,
  });
  if (result.exceptionDetails) report.exception = result.exceptionDetails;
  else report.extensions = result.result?.value;
} catch (error) {
  report.fatalError = String(error?.stack || error);
} finally {
  report.completedAt = new Date().toISOString();
  if (outputPath) fs.writeFileSync(outputPath, `${JSON.stringify(report, null, 2)}\n`);
  console.log(JSON.stringify(report, null, 2));
  ws.close();
}
