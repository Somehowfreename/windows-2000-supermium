const fs = await import("node:fs");

const base = process.argv[2] || "http://127.0.0.1:9223";
const outputPath = process.argv[3];
const pages = process.argv.slice(4).length ? process.argv.slice(4) : [
  "chrome://version/",
  "chrome://settings/",
  "chrome://extensions/",
  "chrome://downloads/",
  "chrome://gpu/",
];

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

await command("Page.enable");
await command("Runtime.enable");
const report = { capturedAt: new Date().toISOString(), base, targetId: target.id, results: [] };
for (const url of pages) {
  const result = { requestedUrl: url };
  try {
    result.navigation = await command("Page.navigate", { url });
    await new Promise((resolve) => setTimeout(resolve, 6000));
    const expression = String.raw`(() => {
      const pieces = [];
      const visited = new Set();
      const walk = (root) => {
        if (!root || visited.has(root)) return;
        visited.add(root);
        for (const node of root.querySelectorAll("*")) {
          if (node.shadowRoot) walk(node.shadowRoot);
          if (node.children.length === 0) {
            const value = (node.innerText || node.textContent || "").replace(/\s+/g, " ").trim();
            if (value) pieces.push(value);
          }
        }
      };
      walk(document);
      return {
        href: location.href,
        title: document.title,
        readyState: document.readyState,
        bodyChildCount: document.body?.children?.length || 0,
        shadowRootCount: visited.size - 1,
        text: pieces.join(" | ").slice(0, 4000),
        htmlLength: document.documentElement?.outerHTML?.length || 0
      };
    })()`;
    const evaluated = await command("Runtime.evaluate", { expression, returnByValue: true });
    result.exceptionDetails = evaluated.exceptionDetails || null;
    result.document = evaluated.result?.value ?? evaluated.result;
    result.pass = Boolean(
      !result.exceptionDetails &&
      result.document?.href?.startsWith(url.replace(/\/$/, "")) &&
      result.document?.htmlLength > 100
    );
  } catch (error) {
    result.error = String(error?.stack || error);
    result.pass = false;
  }
  report.results.push(result);
}
report.pass = report.results.every((result) => result.pass);
report.completedAt = new Date().toISOString();
const rendered = `${JSON.stringify(report, null, 2)}\n`;
if (outputPath) fs.writeFileSync(outputPath, rendered);
console.log(rendered);
ws.close();
