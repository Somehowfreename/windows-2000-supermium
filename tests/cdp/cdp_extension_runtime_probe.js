const fs = await import("node:fs");

const base = process.argv[2] || "http://127.0.0.1:9223";
const extensionId = process.argv[3];
const outputPath = process.argv[4];
if (!extensionId) {
  throw new Error("usage: cdp_extension_runtime_probe.js CDP_BASE EXTENSION_ID [OUTPUT]");
}

const targets = await (await fetch(`${base}/json/list`)).json();
const prefix = `chrome-extension://${extensionId}/`;
const target = targets.find((item) => item.url?.startsWith(prefix));
if (!target) throw new Error(`No live target found for ${extensionId}`);

const ws = new WebSocket(target.webSocketDebuggerUrl);
const pending = new Map();
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
  if (!message.id || !pending.has(message.id)) return;
  const { resolve, reject } = pending.get(message.id);
  pending.delete(message.id);
  if (message.error) reject(new Error(JSON.stringify(message.error)));
  else resolve(message.result);
};

await new Promise((resolve, reject) => {
  ws.onopen = resolve;
  ws.onerror = reject;
});

await command("Runtime.enable");
const expression = String.raw`(async () => {
  const result = {
    href: globalThis.location?.href || "",
    manifest: chrome.runtime.getManifest(),
    lastError: chrome.runtime.lastError?.message || "",
    dnrAvailable: Boolean(chrome.declarativeNetRequest),
  };
  const dnr = chrome.declarativeNetRequest;
  if (dnr) {
    for (const [name, fn] of [
      ["enabledRulesets", "getEnabledRulesets"],
      ["availableStaticRuleCount", "getAvailableStaticRuleCount"],
    ]) {
      try {
        const value = await dnr[fn]();
        result[name] = Array.isArray(value)
          ? { count: value.length, sample: value.slice(0, 12) }
          : value;
      } catch (error) {
        result[name] = { error: String(error?.stack || error) };
      }
    }
  }
  return result;
})()`;
const evaluated = await command("Runtime.evaluate", {
  expression,
  awaitPromise: true,
  returnByValue: true,
});

const report = {
  capturedAt: new Date().toISOString(),
  base,
  extensionId,
  target: { id: target.id, type: target.type, title: target.title, url: target.url },
  exceptionDetails: evaluated.exceptionDetails || null,
  result: evaluated.result?.value ?? evaluated.result,
  completedAt: new Date().toISOString(),
};

const rendered = `${JSON.stringify(report, null, 2)}\n`;
if (outputPath) fs.writeFileSync(outputPath, rendered);
console.log(rendered);
ws.close();
