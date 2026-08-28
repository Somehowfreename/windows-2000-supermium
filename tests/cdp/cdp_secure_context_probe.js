const fs = await import("node:fs");

const base = process.argv[2] || "http://127.0.0.1:9223";
const url = process.argv[3] || "https://example.com/";
const outputPath = process.argv[4];

const targets = await (await fetch(`${base}/json/list`)).json();
const target = targets.find((item) => item.type === "page");
if (!target) throw new Error("No page target found");
const ws = new WebSocket(target.webSocketDebuggerUrl);
const pending = new Map();
const network = [];
const errors = [];
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
  if (message.method === "Network.responseReceived" && message.params?.response?.url === url) {
    const response = message.params.response;
    network.push({
      url: response.url,
      status: response.status,
      protocol: response.protocol,
      remoteIPAddress: response.remoteIPAddress,
      securityDetails: response.securityDetails,
    });
  }
  if (message.method === "Network.loadingFailed") errors.push(message.params);
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
  await command("Security.enable");
  report.navigation = await command("Page.navigate", { url });
  const expression = String.raw`(async () => {
    const delay = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
    const deadline = performance.now() + 60000;
    while (document.readyState !== "complete" && performance.now() < deadline) await delay(100);
    const report = {
      href: location.href,
      title: document.title,
      readyState: document.readyState,
      secureContext: isSecureContext,
      cryptoSubtle: Boolean(globalThis.crypto?.subtle),
      credentials: Boolean(navigator.credentials),
      paymentRequest: typeof PaymentRequest === "function",
      webGPU: { available: Boolean(navigator.gpu) },
      webCodecs: {
        videoDecoder: typeof VideoDecoder === "function",
        audioDecoder: typeof AudioDecoder === "function",
        videoEncoder: typeof VideoEncoder === "function",
        audioEncoder: typeof AudioEncoder === "function"
      }
    };
    if (globalThis.crypto?.subtle) {
      const digest = await crypto.subtle.digest("SHA-256", Uint8Array.from([1, 2, 3]));
      report.sha256 = Array.from(new Uint8Array(digest), (value) => value.toString(16).padStart(2, "0")).join("");
    }
    if (navigator.gpu) {
      try {
        const adapter = await navigator.gpu.requestAdapter({ powerPreference: "low-power" });
        report.webGPU.adapter = Boolean(adapter);
        if (adapter) {
          report.webGPU.features = [...adapter.features].sort();
          report.webGPU.limits = {
            maxTextureDimension2D: adapter.limits.maxTextureDimension2D,
            maxBindGroups: adapter.limits.maxBindGroups,
            maxBufferSize: adapter.limits.maxBufferSize
          };
        }
      } catch (error) {
        report.webGPU.error = String(error?.stack || error);
      }
    }
    const mediaKeyConfig = [{
      initDataTypes: ["cenc"],
      distinctiveIdentifier: "optional",
      persistentState: "optional",
      sessionTypes: ["temporary"],
      videoCapabilities: [{ contentType: 'video/mp4; codecs="avc1.42E01E"' }]
    }];
    report.eme = { available: typeof navigator.requestMediaKeySystemAccess === "function" };
    if (report.eme.available) {
      for (const keySystem of ["org.w3.clearkey", "com.widevine.alpha"]) {
        try {
          const access = await navigator.requestMediaKeySystemAccess(keySystem, mediaKeyConfig);
          report.eme[keySystem] = { supported: true, configuration: access.getConfiguration() };
        } catch (error) {
          report.eme[keySystem] = { supported: false, errorName: error?.name || "", error: String(error) };
        }
      }
    }
    return report;
  })()`;
  const evaluated = await command("Runtime.evaluate", {
    expression,
    awaitPromise: true,
    returnByValue: true,
  });
  report.exceptionDetails = evaluated.exceptionDetails || null;
  report.result = evaluated.result?.value ?? evaluated.result;
  report.network = network;
  report.networkErrors = errors;
} catch (error) {
  report.fatalError = String(error?.stack || error);
} finally {
  report.completedAt = new Date().toISOString();
  const rendered = `${JSON.stringify(report, null, 2)}\n`;
  if (outputPath) fs.writeFileSync(outputPath, rendered);
  console.log(rendered);
  ws.close();
}
