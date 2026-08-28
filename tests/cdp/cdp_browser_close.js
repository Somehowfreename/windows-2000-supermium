const base = process.argv[2] || "http://127.0.0.1:9223";
const targets = await (await fetch(`${base}/json/list`)).json();
const target = targets.find((item) => item.type === "page") || targets[0];
if (!target) throw new Error("No DevTools target found");

const ws = new WebSocket(target.webSocketDebuggerUrl);
let finished = false;

await new Promise((resolve, reject) => {
  ws.onopen = resolve;
  ws.onerror = reject;
});

ws.onmessage = (event) => {
  const message = JSON.parse(event.data);
  if (message.id !== 1) return;
  finished = true;
  console.log(JSON.stringify({ base, targetId: target.id, result: message.result || null }));
  ws.close();
};

ws.send(JSON.stringify({ id: 1, method: "Browser.close" }));
setTimeout(() => {
  if (finished) return;
  console.log(JSON.stringify({ base, targetId: target.id, closeSent: true }));
  ws.close();
}, 3000);
