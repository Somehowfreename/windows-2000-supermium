const fs = await import("node:fs");

const base = process.argv[2] || "http://127.0.0.1:9223";
const controllerId = process.argv[3];
const targetId = process.argv[4];
const action = process.argv[5] || "status";
const outputPath = process.argv[6];
if (!controllerId || !targetId) {
  throw new Error("usage: cdp_extension_lifecycle.js CDP_BASE CONTROLLER_ID TARGET_ID ACTION [OUTPUT]");
}

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

const report = { capturedAt: new Date().toISOString(), base, controllerId, targetId, action };
try {
  await command("Page.enable");
  const load = new Promise((resolve) => { loaded = resolve; });
  report.navigation = await command("Page.navigate", {
    url: `chrome-extension://${controllerId}/test.html`,
  });
  await Promise.race([load, new Promise((resolve) => setTimeout(resolve, 10000))]);

  const expression = `(async () => {
    const call = (method, ...args) => new Promise((resolve, reject) => {
      method(...args, (value) => {
        const error = chrome.runtime.lastError;
        if (error) reject(new Error(error.message)); else resolve(value);
      });
    });
    const before = await call(chrome.management.get, ${JSON.stringify(targetId)});
    let actionResult = null;
    if (${JSON.stringify(action)} === 'disable') {
      await call(chrome.management.setEnabled, ${JSON.stringify(targetId)}, false);
      actionResult = 'disabled';
    } else if (${JSON.stringify(action)} === 'enable') {
      await call(chrome.management.setEnabled, ${JSON.stringify(targetId)}, true);
      actionResult = 'enabled';
    }
    const after = await call(chrome.management.get, ${JSON.stringify(targetId)});
    const all = await call(chrome.management.getAll);
    const warnings = await call(chrome.management.getPermissionWarningsById,
      ${JSON.stringify(targetId)});
    const optionalBookmarks = await call(chrome.permissions.contains,
      {permissions:['bookmarks']});
    const own = await call(chrome.management.getSelf);
    const storage = await chrome.storage.local.get(null);
    return {
      before,
      actionResult,
      after,
      targetPresentInGetAll: all.some((item) => item.id === ${JSON.stringify(targetId)}),
      extensionCount: all.length,
      permissionWarnings: warnings,
      optionalBookmarks,
      controller: own,
      controllerStorage: storage,
    };
  })()`;
  const result = await command("Runtime.evaluate", {
    expression,
    returnByValue: true,
    awaitPromise: true,
    userGesture: true,
  });
  if (result.exceptionDetails) report.exception = result.exceptionDetails;
  else report.result = result.result?.value;
} catch (error) {
  report.fatalError = String(error?.stack || error);
} finally {
  report.completedAt = new Date().toISOString();
  if (outputPath) fs.writeFileSync(outputPath, `${JSON.stringify(report, null, 2)}\n`);
  console.log(JSON.stringify(report, null, 2));
  ws.close();
}
