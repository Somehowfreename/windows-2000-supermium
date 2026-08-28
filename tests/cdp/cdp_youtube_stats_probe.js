const fs = await import("node:fs");

const base = process.argv[2] || "http://127.0.0.1:19225";
const outputPath = process.argv[3];

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

const report = {
  capturedAt: new Date().toISOString(),
  base,
  targetId: target.id,
};

try {
  const result = await command("Runtime.evaluate", {
    expression: `(() => {
      const player = document.getElementById('movie_player');
      const video = document.querySelector('video');
      const stats = typeof player?.getStatsForNerds === 'function'
        ? player.getStatsForNerds() : {};
      const videoStats = typeof player?.getVideoStats === 'function'
        ? player.getVideoStats() : {};
      return {
        page: location.origin + location.pathname,
        title: document.title,
        codecs: stats.codecs || null,
        resolution: stats.resolution || null,
        dimensionsAndFrames: stats.dims_and_frames || null,
        bandwidthKbps: stats.bandwidth_kbps || null,
        bufferHealthSeconds: stats.buffer_health_seconds || null,
        volume: stats.volume || null,
        videoItag: videoStats.fmt || null,
        audioItag: videoStats.afmt || null,
        playerState: typeof player?.getPlayerState === 'function'
          ? player.getPlayerState() : null,
        playbackQuality: typeof player?.getPlaybackQuality === 'function'
          ? player.getPlaybackQuality() : null,
        currentTime: video?.currentTime ?? null,
        paused: video?.paused ?? null,
        readyState: video?.readyState ?? null,
        videoWidth: video?.videoWidth ?? null,
        videoHeight: video?.videoHeight ?? null,
        audioDecodedBytes: video?.webkitAudioDecodedByteCount ?? null,
        videoDecodedBytes: video?.webkitVideoDecodedByteCount ?? null,
        mediaError: video?.error ? {
          code: video.error.code,
          message: video.error.message || ''
        } : null,
      };
    })()`,
    returnByValue: true,
  });
  if (result.exceptionDetails) report.exception = result.exceptionDetails.text;
  else report.result = result.result?.value;
} catch (error) {
  report.fatalError = String(error?.stack || error);
} finally {
  report.completedAt = new Date().toISOString();
  if (outputPath) fs.writeFileSync(outputPath, `${JSON.stringify(report, null, 2)}\n`);
  console.log(JSON.stringify(report, null, 2));
  ws.close();
}
