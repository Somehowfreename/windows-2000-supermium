"use strict";

(() => {
  if (globalThis.__supermiumW2KRC1Diagnostics) return;
  globalThis.__supermiumW2KRC1Diagnostics = true;

  let nextInstance = 1;
  const mediaIds = new WeakMap();
  const previous = new WeakMap();
  const stallStarted = new WeakMap();
  const eventNames = [
    "abort", "canplay", "canplaythrough", "durationchange", "emptied", "ended",
    "error", "loadeddata", "loadedmetadata", "loadstart", "pause", "play",
    "playing", "ratechange", "seeked", "seeking", "stalled", "suspend",
    "volumechange", "waiting"
  ];

  function send(record) {
    chrome.runtime.sendMessage({
      source: "supermium-w2k-youtube-diagnostics",
      record: {schema: 1, ...record}
    }).catch(() => {});
  }

  function pageKind() {
    if (location.pathname === "/watch") return "watch";
    if (location.pathname.startsWith("/shorts/")) return "shorts";
    if (location.pathname === "/") return "home";
    return "other";
  }

  function finite(value) {
    return typeof value === "number" && Number.isFinite(value) ? value : undefined;
  }

  function rangeEnd(ranges) {
    try {
      return ranges.length ? ranges.end(ranges.length - 1) : 0;
    } catch {
      return 0;
    }
  }

  function numericPlayerState() {
    try {
      const player = document.querySelector("#movie_player");
      const state = player && typeof player.getPlayerState === "function"
        ? player.getPlayerState() : undefined;
      return finite(state);
    } catch {
      return undefined;
    }
  }

  function playbackQuality() {
    try {
      const player = document.querySelector("#movie_player");
      const quality = player && typeof player.getPlaybackQuality === "function"
        ? player.getPlaybackQuality() : undefined;
      return typeof quality === "string" ? quality : undefined;
    } catch {
      return undefined;
    }
  }

  function describe(media, kind, event) {
    const quality = typeof media.getVideoPlaybackQuality === "function"
      ? media.getVideoPlaybackQuality() : null;
    const bufferedEnd = rangeEnd(media.buffered);
    const record = {
      kind,
      event,
      instance: mediaIds.get(media),
      performance_ms: performance.now(),
      tag: media.tagName.toLowerCase(),
      page_kind: pageKind(),
      current_time: finite(media.currentTime),
      duration: finite(media.duration),
      paused: media.paused,
      ended: media.ended,
      seeking: media.seeking,
      ready_state: media.readyState,
      network_state: media.networkState,
      playback_rate: finite(media.playbackRate),
      volume: finite(media.volume),
      muted: media.muted,
      visible: document.visibilityState === "visible",
      width: finite(media.videoWidth),
      height: finite(media.videoHeight),
      buffered_ranges: media.buffered?.length || 0,
      buffered_end: finite(bufferedEnd),
      buffered_ahead: finite(Math.max(0, bufferedEnd - (media.currentTime || 0))),
      played_ranges: media.played?.length || 0,
      decoded_frames: finite(quality?.totalVideoFrames ?? media.webkitDecodedFrameCount),
      dropped_frames: finite(quality?.droppedVideoFrames ?? media.webkitDroppedFrameCount),
      corrupted_frames: finite(quality?.corruptedVideoFrames),
      audio_decoded_bytes: finite(media.webkitAudioDecodedByteCount),
      video_decoded_bytes: finite(media.webkitVideoDecodedByteCount),
      youtube_player_state: numericPlayerState(),
      quality: playbackQuality(),
      error_code: finite(media.error?.code)
    };
    return Object.fromEntries(Object.entries(record).filter(([, value]) => value !== undefined));
  }

  function attach(media) {
    if (!(media instanceof HTMLMediaElement) || mediaIds.has(media)) return;
    mediaIds.set(media, nextInstance++);
    send(describe(media, "media-attached"));
    for (const eventName of eventNames) {
      media.addEventListener(eventName, () => {
        send(describe(media, "media-event", `media-${eventName}`));
      }, true);
    }
  }

  function scan(root) {
    if (root instanceof HTMLMediaElement) attach(root);
    root?.querySelectorAll?.("video,audio").forEach(attach);
  }

  function sample(media) {
    attach(media);
    const now = performance.now();
    const last = previous.get(media);
    const current = media.currentTime;
    if (!media.paused && !media.seeking && media.readyState >= 2 && last &&
        current - last.current < 0.05) {
      const started = stallStarted.get(media) || last.at;
      stallStarted.set(media, started);
      const seconds = (now - started) / 1000;
      if (seconds >= 2 && Math.floor(seconds) !== Math.floor((last.stallSeconds || 0))) {
        const record = describe(media, "detected-stall");
        record.stall_seconds = seconds;
        send(record);
      }
      previous.set(media, {current, at: now, stallSeconds: seconds});
    } else {
      stallStarted.delete(media);
      previous.set(media, {current, at: now, stallSeconds: 0});
    }
    send(describe(media, "media-sample"));
  }

  scan(document);
  new MutationObserver((mutations) => {
    for (const mutation of mutations) {
      for (const node of mutation.addedNodes) scan(node);
    }
  }).observe(document, {subtree: true, childList: true});

  setInterval(() => {
    document.querySelectorAll("video,audio").forEach(sample);
  }, 1000);
})();
