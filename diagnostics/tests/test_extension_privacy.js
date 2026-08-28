"use strict";

const assert = require("assert");
const fs = require("fs");
const path = require("path");
const vm = require("vm");

const extension = path.resolve(__dirname, "..", "youtube-extension");
const manifest = JSON.parse(fs.readFileSync(path.join(extension, "manifest.json"), "utf8"));
const sentBodies = [];
let listener;

const context = {
  Set,
  Object,
  Array,
  Number,
  JSON,
  Promise,
  fetch: async (_endpoint, options) => {
    sentBodies.push(JSON.parse(options.body));
    return {ok: true};
  },
  chrome: {
    runtime: {
      onMessage: {
        addListener(callback) {
          listener = callback;
        }
      }
    }
  }
};

vm.createContext(context);
vm.runInContext(
  fs.readFileSync(path.join(extension, "background.js"), "utf8"),
  context,
  {filename: "background.js"}
);

assert.strictEqual(manifest.manifest_version, 3);
assert.deepStrictEqual(manifest.permissions, []);
assert.deepStrictEqual(manifest.host_permissions, ["http://127.0.0.1:9223/*"]);
assert.deepStrictEqual(
  manifest.content_scripts[0].matches,
  ["https://www.youtube.com/*", "https://m.youtube.com/*"]
);
assert.strictEqual(typeof listener, "function");

function dispatch(record) {
  return new Promise((resolve) => {
    const keepAlive = listener(
      {source: "supermium-w2k-youtube-diagnostics", record},
      {},
      resolve
    );
    assert.strictEqual(keepAlive, true);
  });
}

(async () => {
  const response = await dispatch({
    schema: 1,
    kind: "media-sample",
    event: "playing",
    current_time: 12.5,
    paused: false,
    quality: "hd720",
    url: "https://www.youtube.com/watch?v=private",
    title: "Private video title",
    video_id: "private",
    username: "private-user",
    account: "private@example.com",
    arbitrary_text: "must never pass"
  });
  assert.strictEqual(response.ok, true);
  assert.deepStrictEqual(sentBodies[0], {
    schema: 1,
    kind: "media-sample",
    event: "playing",
    current_time: 12.5,
    paused: false,
    quality: "hd720"
  });

  await dispatch({
    schema: 1,
    kind: "tester-marker",
    marker: "secret-free-form-value"
  });
  assert.strictEqual(sentBodies.length, 1, "unapproved marker must not be posted");

  await dispatch({
    schema: 1,
    kind: "tester-marker",
    marker: "playback-working"
  });
  assert.deepStrictEqual(sentBodies[1], {
    schema: 1,
    kind: "tester-marker",
    marker: "playback-working"
  });

  process.stdout.write("extension privacy whitelist: PASS\n");
})().catch((error) => {
  process.stderr.write(`${error.stack}\n`);
  process.exitCode = 1;
});
