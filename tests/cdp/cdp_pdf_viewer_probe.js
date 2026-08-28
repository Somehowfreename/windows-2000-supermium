const fs = await import("node:fs");

const base = process.argv[2] || "http://127.0.0.1:9223";
const url = process.argv[3] || "http://10.0.2.2:8769/fixture.pdf";
const outputPath = process.argv[4];
const screenshotPath = process.argv[5];
const waitMs = Number(process.argv[6] || 12000);
const targets = await (await fetch(`${base}/json/list`)).json();
const target = targets.find((item) => item.type === "page");
if (!target) throw new Error("No page target found");
const ws = new WebSocket(target.webSocketDebuggerUrl);
const pending = new Map();
const responses = [];
const failures = [];
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
  if (message.method === "Network.responseReceived") {
    const response = message.params.response;
    responses.push({ url: response.url, status: response.status, mimeType: response.mimeType, protocol: response.protocol });
  }
  if (message.method === "Network.loadingFailed") failures.push(message.params);
};
await new Promise((resolve, reject) => {
  ws.onopen = resolve;
  ws.onerror = reject;
});

const report = { capturedAt: new Date().toISOString(), base, url, targetId: target.id };
try {
  await command("Page.enable");
  await command("Runtime.enable");
  await command("Network.enable");
  report.navigation = await command("Page.navigate", { url });
  await new Promise((resolve) => setTimeout(resolve, waitMs));
  const expression = String.raw`(() => {
    const pieces = [];
    const walk = (root) => {
      if (!root) return;
      for (const node of root.querySelectorAll("*")) {
        if (node.shadowRoot) walk(node.shadowRoot);
        const value = node.children.length === 0 ? (node.innerText || node.textContent || "").replace(/\s+/g, " ").trim() : "";
        if (value) pieces.push(value);
      }
    };
    walk(document);
    const embeds = [...document.querySelectorAll("embed")].map((embed) => ({ type: embed.type, src: embed.src }));
    return {
      href: location.href,
      title: document.title,
      readyState: document.readyState,
      embeds,
      text: pieces.join(" | ").slice(0, 3000),
      htmlLength: document.documentElement?.outerHTML?.length || 0
    };
  })()`;
  const evaluated = await command("Runtime.evaluate", { expression, returnByValue: true });
  report.exceptionDetails = evaluated.exceptionDetails || null;
  report.document = evaluated.result?.value ?? evaluated.result;
  report.responses = responses;
  report.failures = failures;
  if (screenshotPath) {
    const screenshot = await command("Page.captureScreenshot", { format: "png", fromSurface: true });
    fs.writeFileSync(screenshotPath, Buffer.from(screenshot.data, "base64"));
    report.screenshotPath = screenshotPath;
  }
  report.pass = Boolean(
    !report.exceptionDetails &&
    report.responses.some((response) => response.url === url && response.status === 200 && response.mimeType === "application/pdf") &&
    report.document?.embeds?.some((embed) => embed.type === "application/pdf")
  );
} catch (error) {
  report.fatalError = String(error?.stack || error);
  report.pass = false;
} finally {
  report.completedAt = new Date().toISOString();
  const rendered = `${JSON.stringify(report, null, 2)}\n`;
  if (outputPath) fs.writeFileSync(outputPath, rendered);
  console.log(rendered);
  ws.close();
}
