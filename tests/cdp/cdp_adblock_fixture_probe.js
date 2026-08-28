const fs = await import("node:fs");

const base = process.argv[2] || "http://127.0.0.1:9223";
const url = process.argv[3] || "http://10.0.2.2:8767/test.html";
const outputPath = process.argv[4];
const screenshotPath = process.argv[5];

const targets = await (await fetch(`${base}/json/list`)).json();
const target = targets.find((item) => item.type === "page");
if (!target) throw new Error("No page target found");

const ws = new WebSocket(target.webSocketDebuggerUrl);
const pending = new Map();
const network = [];
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
  if (message.method === "Network.requestWillBeSent") {
    const requestUrl = message.params?.request?.url || "";
    if (requestUrl.includes("10.0.2.2:8767")) {
      network.push({ event: "request", requestId: message.params.requestId,
        url: requestUrl, type: message.params.type });
    }
  } else if (message.method === "Network.responseReceived") {
    const responseUrl = message.params?.response?.url || "";
    if (responseUrl.includes("10.0.2.2:8767")) {
      network.push({ event: "response", requestId: message.params.requestId,
        url: responseUrl, status: message.params.response.status,
        protocol: message.params.response.protocol, fromDiskCache: message.params.response.fromDiskCache });
    }
  } else if (message.method === "Network.loadingFailed") {
    network.push({ event: "failed", requestId: message.params.requestId,
      errorText: message.params.errorText, blockedReason: message.params.blockedReason,
      type: message.params.type });
  }
};

await new Promise((resolve, reject) => {
  ws.onopen = resolve;
  ws.onerror = reject;
});

const report = { capturedAt: new Date().toISOString(), base, url, targetId: target.id };
try {
  await command("Page.enable");
  await command("Network.enable");
  await command("Network.setCacheDisabled", { cacheDisabled: true });
  const load = new Promise((resolve) => { loaded = resolve; });
  report.navigation = await command("Page.navigate", { url });
  await Promise.race([load, new Promise((resolve) => setTimeout(resolve, 15000))]);
  await new Promise((resolve) => setTimeout(resolve, 5000));

  const evaluation = await command("Runtime.evaluate", {
    expression: `(() => {
      const ad = document.getElementById('ad-banner-1');
      const control = document.getElementById('control');
      const snapshot = (element) => {
        if (!element) return null;
        const style = getComputedStyle(element);
        return {
          display: style.display,
          visibility: style.visibility,
          opacity: style.opacity,
          hidden: element.hidden,
          clientRects: element.getClientRects().length,
          offsetWidth: element.offsetWidth,
          offsetHeight: element.offsetHeight,
        };
      };
      return {
        href: location.href,
        title: document.title,
        readyState: document.readyState,
        fixtureStarted: window.fixtureStarted === true,
        fixtureInlineCompleted: window.fixtureInlineCompleted === true,
        allowedScriptLoaded: window.allowedScriptLoaded === true,
        blockTargetScriptLoaded: window.blockTargetScriptLoaded === true,
        ad: snapshot(ad),
        control: snapshot(control),
        bodyText: document.body && document.body.innerText,
      };
    })()`,
    returnByValue: true,
  });
  if (evaluation.exceptionDetails) report.exception = evaluation.exceptionDetails;
  else report.dom = evaluation.result?.value;
  report.network = network;
  report.assessment = {
    controlSucceeded: report.dom?.allowedScriptLoaded === true &&
      (report.dom?.control?.clientRects || 0) > 0,
    requestBlocked: report.dom?.blockTargetScriptLoaded === false &&
      network.some((item) => item.event === "failed" &&
        (item.blockedReason === "inspector" || item.blockedReason === "other" ||
         item.errorText === "net::ERR_BLOCKED_BY_CLIENT")),
    cosmeticHidden: report.dom?.ad != null &&
      (report.dom.ad.display === "none" || report.dom.ad.visibility === "hidden" ||
       report.dom.ad.clientRects === 0 || report.dom.ad.offsetWidth === 0 ||
       report.dom.ad.offsetHeight === 0),
  };
  if (screenshotPath) {
    const shot = await command("Page.captureScreenshot", { format: "png", captureBeyondViewport: false });
    fs.writeFileSync(screenshotPath, Buffer.from(shot.data, "base64"));
    report.screenshotPath = screenshotPath;
  }
} catch (error) {
  report.fatalError = String(error?.stack || error);
} finally {
  report.completedAt = new Date().toISOString();
  if (outputPath) fs.writeFileSync(outputPath, `${JSON.stringify(report, null, 2)}\n`);
  console.log(JSON.stringify(report, null, 2));
  ws.close();
}
