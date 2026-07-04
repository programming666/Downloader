// release-page/script.js
//
// Replaces the previous document.execCommand("copy") usage, which is
// deprecated and emits a console warning in modern Chromium. Uses the
// async Clipboard API when available; falls back to the legacy selection-
// +execCommand path on browsers without clipboard.writeText support.

(function () {
    "use strict";

    var copyButton = document.getElementById("copyBtn");
    var installCmd = document.getElementById("installCmd");
    if (!copyButton || !installCmd) {
        // Nothing to wire up on this page.
        return;
    }

    var feedbackTimer = null;

    function flashCopied() {
        var original = copyButton.textContent;
        copyButton.textContent = "Copied!";
        copyButton.disabled = true;

        if (feedbackTimer) {
            clearTimeout(feedbackTimer);
        }
        feedbackTimer = setTimeout(function () {
            copyButton.textContent = original;
            copyButton.disabled = false;
        }, 1500);
    }

    function legacyCopy(text) {
        // Range + execCommand fallback. Marked deprecated but still works in
        // every evergreen browser at the time of writing.
        var range = document.createRange();
        range.selectNodeContents(installCmd);
        var selection = window.getSelection();
        if (!selection) {
            return false;
        }
        selection.removeAllRanges();
        selection.addRange(range);

        var ok = false;
        try {
            ok = document.execCommand("copy");
        } catch (err) {
            ok = false;
        }

        selection.removeAllRanges();
        return ok;
    }

    copyButton.addEventListener("click", function () {
        var text = installCmd.textContent || "";

        if (navigator.clipboard && typeof navigator.clipboard.writeText === "function") {
            navigator.clipboard.writeText(text).then(flashCopied, function () {
                if (legacyCopy(text)) {
                    flashCopied();
                }
            });
        } else if (legacyCopy(text)) {
            flashCopied();
        }
    });
})();
