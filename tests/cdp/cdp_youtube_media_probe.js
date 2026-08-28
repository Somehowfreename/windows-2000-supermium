const fs = await import("node:fs");

const base = process.argv[2] || "http://127.0.0.1:9223";
const url = process.argv[3] || "https://www.youtube.com/watch?v=jNQXAC9IVRw";
const outputPath = process.argv[4];
const screenshotPath = process.argv[5];
const referrer = process.argv[6];

const delay = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
function withTimeout(promise, ms, label) {
  return Promise.race([
    promise,
    delay(ms).then(() => { throw new Error(`${label} timed out after ${ms}ms`); }),
  ]);
}

const targets = await (await fetch(`${base}/json/list`)).json();
const target = targets.find((item) => item.type === "page");
if (!target) throw new Error("No page target found");

const ws = new WebSocket(target.webSocketDebuggerUrl);
const pending = new Map();
const responses = [];
const failures = [];
const consoleMessages = [];
let nextId = 1;
let loadEvents = 0;

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
  const { method, params = {} } = message;
  if (method === "Page.loadEventFired") loadEvents++;
  else if (method === "Network.responseReceived") {
    const response = params.response || {};
    responses.push({
      type: params.type,
      url: response.url,
      status: response.status,
      mimeType: response.mimeType,
      protocol: response.protocol,
      remoteIPAddress: response.remoteIPAddress,
      fromDiskCache: response.fromDiskCache,
      fromServiceWorker: response.fromServiceWorker,
    });
  } else if (method === "Network.loadingFailed") {
    failures.push({
      requestId: params.requestId,
      type: params.type,
      errorText: params.errorText,
      canceled: params.canceled,
      blockedReason: params.blockedReason,
    });
  } else if (method === "Runtime.consoleAPICalled") {
    consoleMessages.push({
      type: params.type,
      args: (params.args || []).map((item) => item.value ?? item.description).slice(0, 8),
    });
  }
};

await new Promise((resolve, reject) => {
  ws.onopen = resolve;
  ws.onerror = reject;
});

async function evaluate(expression, options = {}) {
  const result = await withTimeout(command("Runtime.evaluate", {
    expression,
    returnByValue: true,
    awaitPromise: true,
    userGesture: options.userGesture || false,
  }), options.timeout || 60000, options.label || "Runtime.evaluate");
  if (result.exceptionDetails) {
    return { exception: result.exceptionDetails.text, description: result.result?.description };
  }
  return result.result?.value;
}

const mediaProbeExpression = `async () => {
  const video = document.querySelector('video');
  const ranges = (value) => Array.from({length:value?.length || 0}, (_, i) =>
    [value.start(i), value.end(i)]);
  const testTypes = [
    'video/mp4; codecs="avc1.42E01E"',
    'video/mp4; codecs="avc1.640028"',
    'video/webm; codecs="vp8"',
    'video/webm; codecs="vp9"',
    'video/webm; codecs="av01.0.05M.08"',
    'audio/mp4; codecs="mp4a.40.2"',
    'audio/webm; codecs="opus"'
  ];
  const probe = document.createElement('video');
  const canPlayType = Object.fromEntries(testTypes.map((type) => [type, probe.canPlayType(type)]));
  const mediaSource = Object.fromEntries(testTypes.map((type) =>
    [type, typeof MediaSource === 'function' ? MediaSource.isTypeSupported(type) : false]));
  let mediaCapabilities = {};
  if (navigator.mediaCapabilities?.decodingInfo) {
    const configs = {
      h264_aac: {type:'media-source', video:{contentType:'video/mp4; codecs="avc1.42E01E"',
        width:640,height:360,bitrate:800000,framerate:30},
        audio:{contentType:'audio/mp4; codecs="mp4a.40.2"',channels:'2',bitrate:128000,samplerate:44100}},
      vp9_opus: {type:'media-source', video:{contentType:'video/webm; codecs="vp9"',
        width:640,height:360,bitrate:800000,framerate:30},
        audio:{contentType:'audio/webm; codecs="opus"',channels:'2',bitrate:128000,samplerate:48000}},
      av1_opus: {type:'media-source', video:{contentType:'video/webm; codecs="av01.0.05M.08"',
        width:640,height:360,bitrate:800000,framerate:30},
        audio:{contentType:'audio/webm; codecs="opus"',channels:'2',bitrate:128000,samplerate:48000}}
    };
    for (const [name, config] of Object.entries(configs)) {
      try { mediaCapabilities[name] = await navigator.mediaCapabilities.decodingInfo(config); }
      catch (error) { mediaCapabilities[name] = {error:String(error)}; }
    }
  }
  return {
    href: location.href,
    title: document.title,
    readyState: document.readyState,
    bodyText: (document.body?.innerText || '').slice(0, 1200),
    videoCount: document.querySelectorAll('video').length,
    canPlayType,
    mediaSource,
    mediaCapabilities,
    video: video && {
      paused: video.paused,
      ended: video.ended,
      currentTime: video.currentTime,
      duration: video.duration,
      readyState: video.readyState,
      networkState: video.networkState,
      error: video.error && {code:video.error.code,message:video.error.message},
      videoWidth: video.videoWidth,
      videoHeight: video.videoHeight,
      volume: video.volume,
      muted: video.muted,
      buffered: ranges(video.buffered),
      seekable: ranges(video.seekable),
      currentSrc: video.currentSrc,
    },
    playerError: document.querySelector('.ytp-error, #error-screen, yt-playability-error-supported-renderers')?.innerText || '',
  };
}`;

const report = {
  capturedAt: new Date().toISOString(),
  base,
  targetId: target.id,
  requestedUrl: url,
};

try {
  await command("Network.enable", { maxTotalBufferSize: 100000000 });
  await command("Page.enable");
  await command("Runtime.enable");
  await command("Page.stopLoading").catch(() => {});
  const navigationParameters = referrer ? { url, referrer } : { url };
  report.navigation = await withTimeout(command("Page.navigate", navigationParameters), 60000, "Page.navigate")
    .catch((error) => ({ error: String(error) }));

  const pageDeadline = Date.now() + 120000;
  while (Date.now() < pageDeadline) {
    const state = await evaluate(`({title:document.title, readyState:document.readyState,
      hasVideo:!!document.querySelector('video'), bodyLength:(document.body?.innerText||'').length})`,
      { timeout: 15000, label: "page readiness" }).catch(() => undefined);
    /* YouTube inserts an empty video element before its consent UI and player
       controls are ready.  Wait for meaningful document text as well so the
       Reject-all action is not raced on slow single-CPU guests. */
    if ((state?.hasVideo && state?.bodyLength > 100) ||
        (state?.readyState === "complete" && state?.bodyLength > 100)) break;
    await delay(1000);
  }

  report.consentAction = await evaluate(`(() => {
    const candidates = [...document.querySelectorAll('button, [role=button]')];
    const button = candidates.find((item) => (item.innerText || '').trim() === 'Reject all');
    if (!button) return {clicked:false};
    button.click();
    return {clicked:true, text:(button.innerText || '').trim()};
  })()`, { userGesture: true, timeout: 30000, label: "consent action" })
    .catch((error) => ({ error: String(error) }));
  if (report.consentAction?.clicked) await delay(5000);

  report.beforePlay = await evaluate(`(${mediaProbeExpression})()`,
    { timeout: 60000, label: "media probe before play" });
  report.playAttempt = await evaluate(`(async () => {
    const video = document.querySelector('video');
    if (!video) return {attempted:false, reason:'no video element'};
    try { await video.play(); return {attempted:true, resolved:true}; }
    catch (error) { return {attempted:true, resolved:false, name:error.name, message:error.message}; }
  })()`, { userGesture: true, timeout: 60000, label: "video.play" });
  await delay(20000);
  report.afterPlay = await evaluate(`(${mediaProbeExpression})()`,
    { timeout: 60000, label: "media probe after play" });

  report.loadEvents = loadEvents;
  report.mediaResponses = responses.filter((item) =>
    item.type === "Media" || /videoplayback|\.m3u8|\.mpd(?:\?|$)/i.test(item.url)).slice(0, 200);
  report.documentResponses = responses.filter((item) => item.type === "Document").slice(0, 50);
  report.failures = failures.slice(0, 200);
  report.consoleMessages = consoleMessages.slice(-100);
  if (screenshotPath) {
    const screenshot = await command("Page.captureScreenshot", { format: "png", fromSurface: true });
    fs.writeFileSync(screenshotPath, Buffer.from(screenshot.data, "base64"));
  }
} catch (error) {
  report.fatalError = String(error?.stack || error);
} finally {
  report.completedAt = new Date().toISOString();
  if (outputPath) fs.writeFileSync(outputPath, `${JSON.stringify(report, null, 2)}\n`);
  console.log(JSON.stringify(report, null, 2));
  ws.close();
}
