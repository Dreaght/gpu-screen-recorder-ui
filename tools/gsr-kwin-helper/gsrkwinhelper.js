const DAEMON_DBUS_NAME = "com.dec05eba.gpu_screen_recorder";

function dbusSendUpdateActiveWindow(title, isFullscreen) {
    callDBus(
        DAEMON_DBUS_NAME, "/", DAEMON_DBUS_NAME,
        "updateActiveWindow",
        title, isFullscreen,
    );
}

let prevWindow = null;
let prevEmitActiveWindowUpdate = null;

let prevCaption = null;
let prevFullScreen = null;

function emitActiveWindowUpdate(window) {
    if (workspace.activeWindow === window) {
        let caption = window.caption || "";
        let fullScreen = window.fullScreen || false;
        if (caption !== prevCaption || fullScreen !== prevFullScreen) {
            dbusSendUpdateActiveWindow(caption, fullScreen);
            prevCaption = caption;
            prevFullScreen = fullScreen;
        }
    }
}

function subscribeToWindow(window) {
    if (!window) return;
    if (prevWindow !== window) {
        if (prevWindow !== null) {
            prevWindow.captionChanged.disconnect(prevEmitActiveWindowUpdate);
            prevWindow.fullScreenChanged.disconnect(prevEmitActiveWindowUpdate);
        }
        let emitActiveWindowUpdateBound = emitActiveWindowUpdate.bind(null, window);
        window.captionChanged.connect(emitActiveWindowUpdateBound);
        window.fullScreenChanged.connect(emitActiveWindowUpdateBound);
        prevWindow = window;
        prevEmitActiveWindowUpdate = emitActiveWindowUpdateBound;
    }
}

function updateActiveWindow(window) {
    if (!window) return;
    if (window.resourceName === "gsr-ui" || window.resourceName === "gsr-notify") return; // ignore the overlay and notification
    emitActiveWindowUpdate(window);
    subscribeToWindow(window);
}

// handle window focus changes
workspace.windowActivated.connect(updateActiveWindow);

// handle initial state
if (workspace.activeWindow) {
    updateActiveWindow(workspace.activeWindow);
}
