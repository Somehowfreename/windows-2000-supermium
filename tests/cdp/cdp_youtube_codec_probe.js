const fs = await import("node:fs");

const base = process.argv[2] || "http://127.0.0.1:9223";
const outputPath = process.argv[3];
const url = process.argv[4] || "https://www.youtube.com/watch?v=jNQXAC9IVRw";
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
  if (message.id && pending.has(message.id)) {
    const { resolve, reject } = pending.get(message.id);
    pending.delete(message.id);
    if (message.error) reject(new Error(JSON.stringify(message.error)));
    else resolve(message.result);
  }
};
await new Promise((resolve, reject) => {
  ws.onopen = resolve;
  ws.onerror = reject;
});

const types = [
  'video/mp4; codecs="avc1.42E01E"',
  'video/webm; codecs="vp8"',
  'video/webm; codecs="vp9"',
  'video/webm; codecs="av01.0.05M.08"',
  'audio/webm; codecs="opus"',
];
const report = { capturedAt: new Date().toISOString(), base, url };
try {
  await command("Page.enable");
  report.navigation = await command("Page.navigate", { url });
  await new Promise((resolve) => setTimeout(resolve, 12000));
  const result = await command("Runtime.evaluate", {
    expression: `(() => {
      const types = ${JSON.stringify(types)};
      const video = document.createElement('video');
      return {
        href:location.href,
        title:document.title,
        readyState:document.readyState,
        canPlayType:Object.fromEntries(types.map((type) => [type, video.canPlayType(type)])),
        mediaSource:Object.fromEntries(types.map((type) =>
          [type, window.MediaSource?.isTypeSupported?.(type) ?? null])),
        h264ifyStorage:{
          enable:localStorage['h264ify-enable'],
          block60fps:localStorage['h264ify-block_60fps'],
          batteryOnly:localStorage['h264ify-battery_only'],
        },
      };
    })()`,
    returnByValue: true,
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
