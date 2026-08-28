"use strict";

const endpoint = "http://127.0.0.1:9223/youtube";
const markerNames = new Set([
  "playback-working",
  "audio-looping",
  "audio-missing",
  "video-stuttering"
]);

const numericFields = new Set([
  "schema", "instance", "performance_ms", "current_time", "duration",
  "ready_state", "network_state", "playback_rate", "volume", "width", "height",
  "buffered_ranges", "buffered_end", "buffered_ahead", "played_ranges",
  "decoded_frames", "dropped_frames", "corrupted_frames", "audio_decoded_bytes",
  "video_decoded_bytes", "youtube_player_state", "stall_seconds", "error_code"
]);
const booleanFields = new Set([
  "paused", "ended", "seeking", "muted", "visible"
]);
const enumFields = new Set([
  "kind", "event", "tag", "page_kind", "quality", "marker"
]);

function cleanRecord(input) {
  if (!input || typeof input !== "object" || Array.isArray(input)) return null;
  const output = {};
  for (const [key, value] of Object.entries(input)) {
    if (numericFields.has(key) && typeof value === "number" && Number.isFinite(value)) {
      output[key] = value;
    } else if (booleanFields.has(key) && typeof value === "boolean") {
      output[key] = value;
    } else if (enumFields.has(key) && typeof value === "string" && value.length <= 64 &&
               /^[a-zA-Z0-9_.:-]+$/.test(value)) {
      output[key] = value;
    }
  }
  if (!output.kind) return null;
  if (output.kind === "tester-marker" && !markerNames.has(output.marker)) return null;
  return output;
}

async function postRecord(record) {
  const clean = cleanRecord(record);
  if (!clean) return false;
  try {
    const response = await fetch(endpoint, {
      method: "POST",
      cache: "no-store",
      credentials: "omit",
      headers: {
        "Content-Type": "application/json",
        "X-Supermium-Diagnostics": "144-r5-w2k-rc1"
      },
      body: JSON.stringify(clean)
    });
    return response.ok;
  } catch {
    return false;
  }
}

chrome.runtime.onMessage.addListener((message, sender, sendResponse) => {
  if (message?.source !== "supermium-w2k-youtube-diagnostics") return false;
  postRecord(message.record).then((ok) => sendResponse({ok}));
  return true;
});
