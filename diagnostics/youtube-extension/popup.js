"use strict";

const status = document.querySelector("#status");
document.querySelectorAll("button[data-marker]").forEach((button) => {
  button.addEventListener("click", async () => {
    status.textContent = "Recording marker...";
    try {
      const response = await chrome.runtime.sendMessage({
        source: "supermium-w2k-youtube-diagnostics",
        record: {
          schema: 1,
          kind: "tester-marker",
          marker: button.dataset.marker,
          performance_ms: performance.now()
        }
      });
      status.textContent = response?.ok ? "Marker recorded locally." : "Diagnostic listener unavailable.";
    } catch {
      status.textContent = "Diagnostic listener unavailable.";
    }
  });
});
