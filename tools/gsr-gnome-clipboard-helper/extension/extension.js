import Gio from 'gi://Gio';
import GioUnix from 'gi://GioUnix';
import GLib from 'gi://GLib';
import St from 'gi://St';
import { Extension } from 'resource:///org/gnome/shell/extensions/extension.js';

const BUS_NAME = 'com.dec05eba.gpu_screen_recorder.Clipboard';
const OBJECT_PATH = '/com/dec05eba/gpu_screen_recorder/Clipboard';
const MAX_BYTES = 50 * 1024 * 1024; // 50 MiB

const NATIVE_EXES = new Set([
    '/usr/bin/gsr-clipboard-helper',
    '/usr/local/bin/gsr-clipboard-helper',
]);
const FLATPAK_HOST_EXE_RE = /^\/var\/lib\/flatpak\/app\/com\.dec05eba\.gpu_screen_recorder\/[^/]+\/[^/]+\/[0-9a-f]{64}\/files\/bin\/gsr-clipboard-helper$/;
const XDG_DBUS_PROXY_EXES = new Set([
    '/usr/bin/xdg-dbus-proxy',
    '/usr/libexec/xdg-dbus-proxy',
]);

const IFACE_XML = `
<node>
  <interface name="com.dec05eba.gpu_screen_recorder.Clipboard">
    <method name="SetImageFromFD">
      <arg type="h" direction="in" name="fd_index"/>
      <arg type="u" direction="out" name="bytes_written"/>
    </method>
  </interface>
</node>`;

function detectImageMimeType(bytes) {
    const data = bytes.toArray();
    if (data.length >= 8 &&
        data.at(0) === 0x89 && data.at(1) === 0x50 && data.at(2) === 0x4E && data.at(3) === 0x47 &&
        data.at(4) === 0x0D && data.at(5) === 0x0A && data.at(6) === 0x1A && data.at(7) === 0x0A) {
        return 'image/png';
    }
    if (data.length >= 3 &&
        data.at(0) === 0xFF && data.at(1) === 0xD8 && data.at(2) === 0xFF) {
        return 'image/jpeg';
    }
    if (data.length >= 12 &&
        data.at(0) === 0x52 && data.at(1) === 0x49 && data.at(2) === 0x46 && data.at(3) === 0x46 &&
        data.at(8) === 0x57 && data.at(9) === 0x45 && data.at(10) === 0x42 && data.at(11) === 0x50) {
        return 'image/webp';
    }
    return null;
}

function isFdReadOnlyAndValidType(fd) {
    const [ok, raw] = GLib.file_get_contents(`/proc/self/fdinfo/${fd}`);
    if (!ok)
        return false;
    const text = new TextDecoder().decode(raw);
    const match = text.match(/^flags:\s*0([0-7]+)/m);
    if (!match)
        return false;
    const flags = parseInt(match.at(1), 8);
    if (Number.isNaN(flags) || (flags & 3) !== 0) // O_ACCMODE = 3, O_RDONLY = 0
        return false;

    const procFdPath = `/proc/self/fd/${fd}`;
    if (GLib.file_test(procFdPath, GLib.FileTest.IS_REGULAR))
        return true;

    const target = GLib.file_read_link(procFdPath);
    return target.startsWith('pipe:[') || target.startsWith('/memfd:');
}

function checkProcStatusSandbox(pid) {
    const [ok, raw] = GLib.file_get_contents(`/proc/${pid}/status`);
    if (!ok)
        return { valid: false, reason: 'Cannot read /proc/<pid>/status' };

    const status = new TextDecoder().decode(raw);
    const capEff = status.match(/^CapEff:\s*([0-9a-fA-F]+)/m)?.at(1);
    const capPrm = status.match(/^CapPrm:\s*([0-9a-fA-F]+)/m)?.at(1);
    const noNewPrivs = status.match(/^NoNewPrivs:\s*(\d+)/m)?.at(1);
    const seccomp = status.match(/^Seccomp:\s*(\d+)/m)?.at(1);

    if (capEff !== '0000000000000000' || capPrm !== '0000000000000000')
        return { valid: false, reason: `Non-zero capabilities (CapEff=${capEff})` };

    if (noNewPrivs !== '1')
        return { valid: false, reason: `NoNewPrivs not enabled (${noNewPrivs})` };

    if (seccomp !== '2')
        return { valid: false, reason: `Seccomp filter mode not active (Seccomp=${seccomp})` };

    return { valid: true };
}

function verifyCaller(pid) {
    const procExePath = `/proc/${pid}/exe`;
    const exe = GLib.file_read_link(procExePath);
    if (exe.endsWith(' (deleted)'))
        return { valid: false, reason: 'Deleted executable' };

    // Verify executable is owned by root (uid 0)
    const exeInfo = Gio.File.new_for_path(procExePath).query_info(
        'unix::uid',
        Gio.FileQueryInfoFlags.NONE,
        null
    );
    const uid = exeInfo.get_attribute_uint32('unix::uid');
    if (uid !== 0)
        return { valid: false, reason: `Executable not owned by root (uid=${uid})` };

    // Case 1: Flatpak sandboxed caller via xdg-dbus-proxy
    if (XDG_DBUS_PROXY_EXES.has(exe)) {
        const [ok, cgRaw] = GLib.file_get_contents(`/proc/${pid}/cgroup`);
        if (!ok)
            return { valid: false, reason: 'Cannot read caller cgroup' };
        const cgroup = new TextDecoder().decode(cgRaw);
        if (!cgroup.includes('app-flatpak-com.dec05eba.gpu_screen_recorder-'))
            return { valid: false, reason: 'xdg-dbus-proxy not in gsr Flatpak cgroup' };
        return { valid: true };
    }

    // Case 2: Direct helper binary (native or flatpak-spawn --host)
    const testExe = GLib.getenv('GSR_CLIPBOARD_TEST_ALLOW_EXE');
    const pathAllowed = NATIVE_EXES.has(exe) ||
        FLATPAK_HOST_EXE_RE.test(exe) ||
        Boolean(testExe && exe === testExe);

    if (!pathAllowed)
        return { valid: false, reason: `Unauthorized binary path: ${exe}` };

    return checkProcStatusSandbox(pid);
}

export default class GsrClipboardExtension extends Extension {
    enable() {
        this._ownerId = Gio.bus_own_name(
            Gio.BusType.SESSION,
            BUS_NAME,
            Gio.BusNameOwnerFlags.NONE,
            (connection) => {
                this._dbus = Gio.DBusExportedObject.wrapJSObject(IFACE_XML, this);
                this._dbus.export(connection, OBJECT_PATH);
            },
            null,
            () => {
                if (this._dbus) {
                    this._dbus.unexport();
                    this._dbus = null;
                }
            }
        );
    }

    disable() {
        if (this._dbus) {
            this._dbus.unexport();
            this._dbus = null;
        }
        if (this._ownerId) {
            Gio.bus_unown_name(this._ownerId);
            this._ownerId = 0;
        }
    }

    async _getSenderPid(sender) {
        const res = await Gio.DBus.session.call(
            'org.freedesktop.DBus',
            '/org/freedesktop/DBus',
            'org.freedesktop.DBus',
            'GetConnectionUnixProcessID',
            new GLib.Variant('(s)', [sender]),
            new GLib.VariantType('(u)'),
            Gio.DBusCallFlags.NONE,
            1000,
            null
        );
        const [pid] = res.deepUnpack();
        return pid;
    }

    async SetImageFromFDAsync([fdIndex], invocation) {
        let fds = [];
        try {
            const pid = await this._getSenderPid(invocation.get_sender());
            const auth = verifyCaller(pid);
            if (!auth.valid) {
                invocation.return_dbus_error(
                    `${BUS_NAME}.Error.Unauthorized`,
                    `Caller rejected: ${auth.reason}`
                );
                return;
            }

            const fdList = invocation.get_message().get_unix_fd_list();
            if (!fdList) {
                invocation.return_dbus_error(`${BUS_NAME}.Error.NoFD`, 'No UnixFDList attached');
                return;
            }

            fds = fdList.steal_fds();
            if (fdIndex < 0 || fdIndex >= fds.length) {
                fds.forEach(fd => GLib.close(fd));
                invocation.return_dbus_error(`${BUS_NAME}.Error.InvalidFDIndex`, 'Invalid fd_index');
                return;
            }

            const targetFd = fds.at(fdIndex);
            fds.forEach((fd, idx) => { if (idx !== fdIndex) GLib.close(fd); });

            if (!isFdReadOnlyAndValidType(targetFd)) {
                GLib.close(targetFd);
                invocation.return_dbus_error(
                    `${BUS_NAME}.Error.InvalidFD`,
                    'FD must be O_RDONLY and point to a regular file, pipe, or memfd'
                );
                return;
            }

            const inStream = GioUnix.InputStream.new(targetFd, true);
            const outStream = Gio.MemoryOutputStream.new_resizable();

            outStream.splice_async(
                inStream,
                Gio.OutputStreamSpliceFlags.CLOSE_SOURCE | Gio.OutputStreamSpliceFlags.CLOSE_TARGET,
                GLib.PRIORITY_DEFAULT,
                null,
                (src, res) => {
                    try {
                        src.splice_finish(res);
                        const bytes = src.steal_as_bytes();
                        const size = bytes.get_size();

                        if (size === 0 || size > MAX_BYTES) {
                            invocation.return_dbus_error(
                                `${BUS_NAME}.Error.InvalidSize`,
                                `Image size out of bounds: ${size}`
                            );
                            return;
                        }

                        const mimeType = detectImageMimeType(bytes);
                        if (!mimeType) {
                            invocation.return_dbus_error(
                                `${BUS_NAME}.Error.InvalidMagic`,
                                'Payload header does not match supported image formats'
                            );
                            return;
                        }

                        St.Clipboard.get_default().set_content(St.ClipboardType.CLIPBOARD, mimeType, bytes);
                        invocation.return_value(new GLib.Variant('(u)', [size]));
                    } catch (err) {
                        invocation.return_dbus_error(`${BUS_NAME}.Error.SpliceFailed`, err.message);
                    }
                }
            );
        } catch (err) {
            fds.forEach(fd => { try { GLib.close(fd); } catch (_) {} });
            invocation.return_dbus_error(`${BUS_NAME}.Error.Internal`, err.message);
        }
    }
}
