const fs = await import("node:fs");

const base = process.argv[2] || "http://127.0.0.1:9222";
let outputPath;
let navigateTimeoutMs = 20000;
let loadTimeoutMs = 40000;
let evaluateTimeoutMs = 15000;
let hardTimeoutMs;
const urls = [];
for (const argument of process.argv.slice(3)) {
  if (argument.startsWith("--output=")) outputPath = argument.slice("--output=".length);
  else if (argument.startsWith("--navigate-timeout=")) {
    navigateTimeoutMs = Number(argument.slice("--navigate-timeout=".length));
  } else if (argument.startsWith("--load-timeout=")) {
    loadTimeoutMs = Number(argument.slice("--load-timeout=".length));
  } else if (argument.startsWith("--evaluate-timeout=")) {
    evaluateTimeoutMs = Number(argument.slice("--evaluate-timeout=".length));
  } else if (argument.startsWith("--hard-timeout=")) {
    hardTimeoutMs = Number(argument.slice("--hard-timeout=".length));
  }
  else urls.push(argument);
}
if (urls.length === 0) {
  urls.push(
    "https://example.com/",
    "https://www.google.com/",
    "https://github.com/win32ss/supermium",
    "https://old.reddit.com/",
    "https://www.youtube.com/",
  );
}

const targets = await (await fetch(`${base}/json/list`)).json();
const target = targets.find((item) => item.type === "page");
if (!target) throw new Error("No page target found");

const ws = new WebSocket(target.webSocketDebuggerUrl);
const pending = new Map();
let nextId = 1;
let active;

function command(method, params = {}) {
  const id = nextId++;
  return new Promise((resolve, reject) => {
    pending.set(id, { resolve, reject });
    ws.send(JSON.stringify({ id, method, params }));
  });
}

function delay(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function withTimeout(promise, milliseconds, label) {
  return Promise.race([
    promise,
    delay(milliseconds).then(() => {
      throw new Error(`${label} timed out after ${milliseconds}ms`);
    }),
  ]);
}

function mainDocumentRequest(record, requestId) {
  return record.documentRequestIds.has(requestId);
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
  if (!active) return;
  const { method, params = {} } = message;
  if (method === "Network.requestWillBeSent" && params.type === "Document") {
    active.documentRequestIds.add(params.requestId);
    active.requests.push({
      url: params.request?.url,
      method: params.request?.method,
      requestId: params.requestId,
    });
  } else if (method === "Network.responseReceived" && params.type === "Document") {
    const response = params.response || {};
    const security = response.securityDetails;
    active.responses.push({
      url: response.url,
      status: response.status,
      statusText: response.statusText,
      mimeType: response.mimeType,
      protocol: response.protocol,
      remoteIPAddress: response.remoteIPAddress,
      securityDetails: security && {
        protocol: security.protocol,
        keyExchange: security.keyExchange,
        cipher: security.cipher,
        certificateId: security.certificateId,
        subjectName: security.subjectName,
        sanList: security.sanList,
        issuer: security.issuer,
        validFrom: security.validFrom,
        validTo: security.validTo,
        signedCertificateTimestampList: security.signedCertificateTimestampList,
      },
    });
  } else if (method === "Network.loadingFailed" && mainDocumentRequest(active, params.requestId)) {
    active.failures.push({
      requestId: params.requestId,
      errorText: params.errorText,
      canceled: params.canceled,
      blockedReason: params.blockedReason,
    });
    active.finished = true;
  } else if (method === "Page.loadEventFired") {
    active.loadFinished = true;
    active.finished = true;
  } else if (method === "Security.visibleSecurityStateChanged") {
    const state = params.visibleSecurityState || {};
    const certificate = state.certificateSecurityState || {};
    active.visibleSecurityState = {
      securityState: state.securityState,
      certificateSecurityState: state.certificateSecurityState && {
        protocol: certificate.protocol,
        keyExchange: certificate.keyExchange,
        keyExchangeGroup: certificate.keyExchangeGroup,
        cipher: certificate.cipher,
        subjectName: certificate.subjectName,
        issuer: certificate.issuer,
        validFrom: certificate.validFrom,
        validTo: certificate.validTo,
        certificateHasWeakSignature: certificate.certificateHasWeakSignature,
        certificateHasSha1Signature: certificate.certificateHasSha1Signature,
        modernSSL: certificate.modernSSL,
        obsoleteSslProtocol: certificate.obsoleteSslProtocol,
        obsoleteSslKeyExchange: certificate.obsoleteSslKeyExchange,
        obsoleteSslCipher: certificate.obsoleteSslCipher,
        obsoleteSslSignature: certificate.obsoleteSslSignature,
      },
      safetyTipInfo: state.safetyTipInfo,
      securityStateIssueIds: state.securityStateIssueIds,
    };
  }
};

function publicRecord(record) {
  const { documentRequestIds, finished, ...result } = record;
  return result;
}

const allResults = [];
const hardTimer = setTimeout(() => {
  console.error("Hard timeout reached");
  ws.close();
  process.exitCode = 2;
}, hardTimeoutMs ?? Math.max(120000, urls.length *
  (navigateTimeoutMs + loadTimeoutMs + evaluateTimeoutMs + 5000)));

function writeCheckpoint() {
  const report = {
    capturedAt: new Date().toISOString(),
    devToolsBase: base,
    targetId: target.id,
    complete: allResults.length === urls.length,
    requestedCount: urls.length,
    completedCount: allResults.length,
    timeouts: { navigateTimeoutMs, loadTimeoutMs, evaluateTimeoutMs },
    results: allResults,
  };
  if (outputPath) fs.writeFileSync(outputPath, `${JSON.stringify(report, null, 2)}\n`);
  return report;
}

await new Promise((resolve, reject) => {
  ws.onopen = resolve;
  ws.onerror = reject;
});

try {
  await command("Network.enable");
  await command("Page.enable");
  await command("Security.enable");

  for (const url of urls) {
    active = {
      requestedUrl: url,
      navigation: undefined,
      loadFinished: false,
      timedOut: false,
      requests: [],
      responses: [],
      failures: [],
      visibleSecurityState: undefined,
      documentState: undefined,
      documentRequestIds: new Set(),
      finished: false,
    };
    await command("Page.stopLoading").catch(() => {});
    try {
      active.navigation = await withTimeout(
        command("Page.navigate", { url }), navigateTimeoutMs, `Page.navigate ${url}`,
      );
    } catch (error) {
      active.navigationError = String(error);
    }
    const deadline = Date.now() + loadTimeoutMs;
    while (!active.finished && Date.now() < deadline) await delay(250);
    if (!active.finished) active.timedOut = true;
    await delay(1000);
    active.documentState = await withTimeout(command("Runtime.evaluate", {
      expression: `({href:location.href,title:document.title,readyState:document.readyState,
        bodyText:(document.body?.innerText||'').slice(0,300),
        secureContext:self.isSecureContext,
        userAgent:navigator.userAgent})`,
      returnByValue: true,
    }), evaluateTimeoutMs, `Runtime.evaluate ${url}`)
      .catch((error) => ({ evaluationError: String(error) }));
    const result = publicRecord(active);
    allResults.push(result);
    console.log(JSON.stringify(result));
    writeCheckpoint();
    active = undefined;
  }

  const report = writeCheckpoint();
  console.log(JSON.stringify(report, null, 2));
} finally {
  clearTimeout(hardTimer);
  ws.close();
}
