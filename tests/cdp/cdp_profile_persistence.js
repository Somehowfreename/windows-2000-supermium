const fs = await import("node:fs");

const base = process.argv[2] || "http://127.0.0.1:9223";
const url = process.argv[3] || "http://10.0.2.2:8768/standards.html";
const mode = process.argv[4] || "verify";
const outputPath = process.argv[5];
if (!["seed", "verify"].includes(mode)) throw new Error("mode must be seed or verify");

const targets = await (await fetch(`${base}/json/list`)).json();
const target = targets.find((item) => item.type === "page");
if (!target) throw new Error("No page target found");
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

const expected = "candidate19-profile-persistence-ok-v1";
const report = { capturedAt: new Date().toISOString(), base, url, mode, targetId: target.id, expected };
try {
  await command("Page.enable");
  await command("Runtime.enable");
  report.navigation = await command("Page.navigate", { url });
  const expression = `(${async function profileProbe(mode, expected) {
    const delay = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
    const deadline = performance.now() + 30000;
    while (document.readyState !== "complete" && performance.now() < deadline) await delay(100);
    if (mode === "seed") {
      localStorage.setItem("candidate19-profile-state", expected);
      document.cookie = `candidate19_profile=${encodeURIComponent(expected)}; Max-Age=86400; Path=/; SameSite=Lax`;
    }
    const indexedDBResult = await new Promise((resolve) => {
      const request = indexedDB.open("candidate19-profile-persistence", 1);
      request.onupgradeneeded = () => request.result.createObjectStore("values");
      request.onerror = () => resolve({ error: request.error?.name || "open failed" });
      request.onsuccess = () => {
        const db = request.result;
        if (mode === "seed") {
          const write = db.transaction("values", "readwrite");
          write.objectStore("values").put(expected, "state");
          write.onabort = () => resolve({ error: write.error?.name || "write failed" });
          write.oncomplete = () => resolve({ value: expected, seeded: true });
        } else {
          const read = db.transaction("values", "readonly").objectStore("values").get("state");
          read.onerror = () => resolve({ error: read.error?.name || "read failed" });
          read.onsuccess = () => resolve({ value: read.result ?? null });
        }
      };
    });
    const cookie = document.cookie.split("; ").find((item) => item.startsWith("candidate19_profile="));
    const values = {
      localStorage: localStorage.getItem("candidate19-profile-state"),
      cookie: cookie ? decodeURIComponent(cookie.split("=").slice(1).join("=")) : null,
      indexedDB: indexedDBResult.value ?? null,
      indexedDBError: indexedDBResult.error || ""
    };
    return { href: location.href, mode, values, pass: Object.values({
      localStorage: values.localStorage,
      cookie: values.cookie,
      indexedDB: values.indexedDB
    }).every((value) => value === expected) };
  }})(${JSON.stringify(mode)}, ${JSON.stringify(expected)})`;
  const evaluated = await command("Runtime.evaluate", { expression, awaitPromise: true, returnByValue: true });
  report.exceptionDetails = evaluated.exceptionDetails || null;
  report.result = evaluated.result?.value ?? evaluated.result;
} catch (error) {
  report.fatalError = String(error?.stack || error);
} finally {
  report.completedAt = new Date().toISOString();
  const rendered = `${JSON.stringify(report, null, 2)}\n`;
  if (outputPath) fs.writeFileSync(outputPath, rendered);
  console.log(rendered);
  ws.close();
}
