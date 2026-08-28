const fs = await import("node:fs");

const base = process.argv[2] || "http://127.0.0.1:9223";
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

const expression = String.raw`(async () => {
  const report = {
    audioContext: typeof AudioContext === "function",
    offlineAudioContext: typeof OfflineAudioContext === "function",
    audioDecoder: typeof AudioDecoder === "function"
  };
  try {
    const context = new OfflineAudioContext(1, 4096, 48000);
    const oscillator = context.createOscillator();
    const gain = context.createGain();
    oscillator.frequency.value = 440;
    gain.gain.value = 0.25;
    oscillator.connect(gain).connect(context.destination);
    oscillator.start(0);
    const rendered = await context.startRendering();
    const samples = rendered.getChannelData(0);
    let peak = 0;
    let nonZero = 0;
    for (const sample of samples) {
      peak = Math.max(peak, Math.abs(sample));
      if (sample !== 0) nonZero++;
    }
    report.offlineRender = {
      length: rendered.length,
      sampleRate: rendered.sampleRate,
      duration: rendered.duration,
      peak,
      nonZero,
      pass: rendered.length === 4096 && nonZero > 4000 && peak > 0.2
    };
  } catch (error) {
    report.offlineRender = { pass: false, error: String(error?.stack || error) };
  }
  try {
    const live = new AudioContext();
    const stateBeforeResume = live.state;
    await live.resume();
    const startedAt = live.currentTime;
    const oscillator = live.createOscillator();
    const gain = live.createGain();
    oscillator.frequency.value = 440;
    gain.gain.value = 0.02;
    oscillator.connect(gain).connect(live.destination);
    const ended = new Promise((resolve) => { oscillator.onended = () => resolve(true); });
    oscillator.start();
    oscillator.stop(live.currentTime + 1);
    const renderedToDevice = await Promise.race([
      ended,
      new Promise((resolve) => setTimeout(() => resolve(false), 5000))
    ]);
    report.liveContext = {
      created: true,
      stateBeforeResume,
      state: live.state,
      sampleRate: live.sampleRate,
      baseLatency: Number.isFinite(live.baseLatency) ? live.baseLatency : null,
      outputLatency: Number.isFinite(live.outputLatency) ? live.outputLatency : null,
      renderedToDevice,
      currentTimeAdvanced: live.currentTime - startedAt
    };
    await live.close();
  } catch (error) {
    report.liveContext = { created: false, error: String(error?.stack || error) };
  }
  report.pass = Boolean(
    report.offlineRender?.pass &&
    report.audioDecoder &&
    report.liveContext?.created &&
    report.liveContext?.state === "running" &&
    report.liveContext?.renderedToDevice &&
    report.liveContext?.currentTimeAdvanced >= 0.9
  );
  return report;
})()`;

const report = { capturedAt: new Date().toISOString(), base, targetId: target.id };
try {
  await command("Runtime.enable");
  const evaluated = await command("Runtime.evaluate", {
    expression,
    awaitPromise: true,
    returnByValue: true,
    userGesture: true
  });
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
