const DAEMON_DBUS_NAME = "com.dec05eba.gpu_screen_recorder.gsr_kwin_helper";

// utils
function sendNewActiveWindowTitle(title) {
    callDBus(
        DAEMON_DBUS_NAME, "/", DAEMON_DBUS_NAME,
        "setActiveWindowTitle", title
    );
}

function sendNewActiveWindowFullscreen(isFullscreen) {
    callDBus(
        DAEMON_DBUS_NAME, "/", DAEMON_DBUS_NAME,
        "setActiveWindowFullscreen", isFullscreen
    );
}

// track handlers to avoid duplicates
const windowEventHandlers = new Map();

function subscribeToClient(client) {
    if (!client || windowEventHandlers.has(client)) return;

    const emitActiveTitle = () => {
        if (workspace.activeWindow === client) {
            sendNewActiveWindowTitle(client.caption || "");
        }
    };

    const emitActiveFullscreen = () => {
        if (workspace.activeWindow === client) {
            sendNewActiveWindowFullscreen(client.fullScreen);
        }
    };

    windowEventHandlers.set(client, {
        title: emitActiveTitle,
        fs: emitActiveFullscreen,
    });

    client.captionChanged.connect(emitActiveTitle);
    client.fullScreenChanged.connect(emitActiveFullscreen);
}

function updateActiveWindow(client) {
    if (!client) return;
    sendNewActiveWindowTitle(client.caption || "");
    sendNewActiveWindowFullscreen(client.fullScreen);
    subscribeToClient(client);
}

// handle window focus changes
workspace.windowActivated.connect(updateActiveWindow);

// handle initial state
if (workspace.activeWindow) {
    updateActiveWindow(workspace.activeWindow);
}