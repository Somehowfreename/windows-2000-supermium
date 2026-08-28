const fs = await import("node:fs");

const base = process.argv[2] || "http://127.0.0.1:9224";
const url = process.argv[3] || "http://10.0.2.2:8770/media-playback.html";
const outputPath = process.argv[4];
const screenshotPath = process.argv[5];
const delay = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

const targets = await (await fetch(`${base}/json/list`)).json();
const target = targets.find((item) => item.type === "page");
if (!target) throw new Error("No page target found");
const ws = new WebSocket(target.webSocketDebuggerUrl);
const pending = new Map();
const responses = [];
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
    const response = message.params.response || {};
    responses.push({
      type: message.params.type,
      url: response.url,
      status: response.status,
      mimeType: response.mimeType,
      protocol: response.protocol,
    });
  }
};

await new Promise((resolve, reject) => {
  ws.onopen = resolve;
  ws.onerror = reject;
});

async function evaluate(expression, userGesture = false) {
  const evaluated = await command("Runtime.evaluate", {
    expression,
    awaitPromise: true,
    returnByValue: true,
    userGesture,
  });
  if (evaluated.exceptionDetails) {
    throw new Error(evaluated.exceptionDetails.text || "Runtime.evaluate failed");
  }
  return evaluated.result?.value;
}

const stateExpression = `(() => {
  const video = document.querySelector('video');
  if (!video) return {video:null, visibilityState:document.visibilityState};
  let quality;
  try { quality = video.getVideoPlaybackQuality?.(); } catch {}
  const ranges = (value) => Array.from({length:value?.length || 0}, (_, index) =>
    [value.start(index), value.end(index)]);
  return {
    visibilityState: document.visibilityState,
    video: {
      paused: video.paused,
      ended: video.ended,
      currentTime: video.currentTime,
      duration: Number.isFinite(video.duration) ? video.duration : null,
      readyState: video.readyState,
      networkState: video.networkState,
      error: video.error && {code:video.error.code,message:video.error.message},
      width: video.videoWidth,
      height: video.videoHeight,
      muted: video.muted,
      volume: video.volume,
      buffered: ranges(video.buffered),
      decodedFrames: video.webkitDecodedFrameCount,
      droppedFrames: video.webkitDroppedFrameCount,
      playbackQuality: quality && {
        totalVideoFrames: quality.totalVideoFrames,
        droppedVideoFrames: quality.droppedVideoFrames,
        corruptedVideoFrames: quality.corruptedVideoFrames,
      },
    },
  };
})()`;

const report = { capturedAt: new Date().toISOString(), base, url, samples: [] };
try {
  await command("Network.enable");
  await command("Page.enable");
  report.navigation = await command("Page.navigate", { url });
  const deadline = Date.now() + 60000;
  while (Date.now() < deadline) {
    const state = await evaluate(stateExpression);
    if (state?.video?.readyState >= 2) break;
    await delay(500);
  }
  report.beforePlay = await evaluate(stateExpression);
  report.playAttempt = await evaluate(`(async () => {
    const video = document.querySelector('video');
    if (!video) return {attempted:false};
    try { await video.play(); return {attempted:true,resolved:true}; }
    catch (error) { return {attempted:true,resolved:false,name:error.name,message:error.message}; }
  })()`, true);
  for (let index = 0; index < 20; ++index) {
    await delay(1000);
    report.samples.push(await evaluate(stateExpression));
  }
  report.afterPlay = report.samples.at(-1);
  report.mediaResponses = responses.filter((item) => item.type === "Media");
  const before = report.beforePlay?.video;
  const after = report.afterPlay?.video;
  report.pass = Boolean(
    report.playAttempt?.resolved &&
    before && after &&
    !after.error &&
    after.currentTime - before.currentTime >= 10 &&
    after.decodedFrames > 0 &&
    after.width > 0 && after.height > 0 &&
    after.muted === false && after.volume === 1
  );
  if (screenshotPath) {
    const screenshot = await command("Page.captureScreenshot", {
      format: "png",
      fromSurface: true,
    });
    fs.writeFileSync(screenshotPath, Buffer.from(screenshot.data, "base64"));
  }
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
