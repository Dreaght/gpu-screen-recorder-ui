const DAEMON_DBUS_NAME = "com.dec05eba.gpu_screen_recorder";

function dbusSendUpdateActiveWindow(title, isFullscreen, monitorName, pid) {
    callDBus(
        DAEMON_DBUS_NAME, "/", DAEMON_DBUS_NAME,
        "updateActiveWindow",
        title, isFullscreen, monitorName, pid,
    );
}

let prevWindow = null;
let prevEmitActiveWindowUpdate = null;

let prevCaption = null;
let prevFullScreen = null;
let prevMonitorName = null;
let prevPid = null;

function dbusSafeDisconnect(window, signalName, callback) {
    try {
        const signal = window?.[signalName];
        if (signal && callback) {
            signal.disconnect(callback);
        }
    } catch (e) {
        // ignore stale/missing signal disconnects
    }
}

function dbusSafeConnect(window, signalName, callback) {
    try {
        const signal = window?.[signalName];
        if (signal && callback) {
            signal.connect(callback);
        }
    } catch (e) {
        // ignore missing signals
    }
}

function emitActiveWindowUpdate(window) {
    if (workspace.activeWindow === window) {
        let caption = window.caption || "";
        let fullScreen = window.fullScreen || false;
        let monitorName = window.output?.name || "";
        let pid = window.pid || 0;
        if (caption !== prevCaption || fullScreen !== prevFullScreen || monitorName !== prevMonitorName || pid !== prevPid) {
            dbusSendUpdateActiveWindow(caption, fullScreen, monitorName, pid);
            prevCaption = caption;
            prevFullScreen = fullScreen;
            prevMonitorName = monitorName;
            prevPid = pid;
        }
    }
}

function subscribeToWindow(window) {
    if (!window) return;
    if (prevWindow !== window) {
        if (prevWindow !== null) {
            dbusSafeDisconnect(prevWindow, "captionChanged", prevEmitActiveWindowUpdate);
            dbusSafeDisconnect(prevWindow, "fullScreenChanged", prevEmitActiveWindowUpdate);
            dbusSafeDisconnect(prevWindow, "outputChanged", prevEmitActiveWindowUpdate);
        }
        let emitActiveWindowUpdateBound = emitActiveWindowUpdate.bind(null, window);
        dbusSafeConnect(window, "captionChanged", emitActiveWindowUpdateBound);
        dbusSafeConnect(window, "fullScreenChanged", emitActiveWindowUpdateBound);
        dbusSafeConnect(window, "outputChanged", emitActiveWindowUpdateBound);
        prevWindow = window;
        prevEmitActiveWindowUpdate = emitActiveWindowUpdateBound;
    }
}

function updateActiveWindow(window) {
    if (!window) return;
    if (!window.normalWindow) return;
    if (window.resourceName === "gsr-ui" || window.resourceName === "gsr-notify") return; // ignore the overlay and notification
    if (window.resourceClass === "org.kde.spectacle") return;
    emitActiveWindowUpdate(window);
    subscribeToWindow(window);
}

// handle window focus changes
workspace.windowActivated.connect(updateActiveWindow);

// handle initial state
if (workspace.activeWindow) {
    updateActiveWindow(workspace.activeWindow);
}
