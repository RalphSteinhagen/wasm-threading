mergeInto(LibraryManager.library, {
    initPassiveTouchListeners: function () {
        var canvas = Module['canvas'];
        if (canvas) {
            canvas.addEventListener('touchstart', function () {}, { passive: true });
            canvas.addEventListener('touchmove', function () {}, { passive: true });
            canvas.addEventListener('wheel', function () {}, { passive: true });
            console.log("[initPassiveTouchListeners] Passive touch listeners registered.");
        } else {
            console.warn("[initPassiveTouchListeners] No canvas found.");
        }
    }
});
