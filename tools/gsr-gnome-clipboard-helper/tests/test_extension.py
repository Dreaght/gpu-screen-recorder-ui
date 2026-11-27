#!/usr/bin/env python3
import ctypes
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time
from contextlib import contextmanager
import argparse

import gi
gi.require_version("Gio", "2.0")
from gi.repository import Gio, GLib

EXTENSION_UUID = "fd-clipboard@test"
DBUS_DEST = "com.dec05eba.gpu_screen_recorder.Clipboard"
DBUS_PATH = "/com/dec05eba/gpu_screen_recorder/Clipboard"
DBUS_IFACE = "com.dec05eba.gpu_screen_recorder.Clipboard"
SAMPLE_PNG = "/usr/share/pixmaps/fedora-logo-small.png"


def apply_seccomp_sandbox():
    """Enables PR_SET_NO_NEW_PRIVS and installs a minimal x86_64 seccomp-bpf filter."""
    libc = ctypes.CDLL("libc.so.6", use_errno=True)
    libc.prctl.argtypes = [ctypes.c_int, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_ulong]
    libc.prctl.restype = ctypes.c_int

    PR_SET_NO_NEW_PRIVS = 38
    assert libc.prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0, "Failed to set PR_SET_NO_NEW_PRIVS"

    def stmt(code: int, k: int) -> bytes:
        return struct.pack("HBBI", code, 0, 0, k)

    def jmp(code: int, jt: int, jf: int, k: int) -> bytes:
        return struct.pack("HBBI", code, jt, jf, k)

    # Classic BPF filter matching test_extension.sh (allows x86_64 syscalls except blocked ones)
    bpf_program = b"".join([
        stmt(0x20, 0),            # ld [0] (syscall nr)
        jmp(0x15, 2, 0, 2),       # jeq #2 (open) -> allow
        jmp(0x15, 1, 0, 257),     # jeq #257 (openat) -> allow
        jmp(0x15, 0, 1, 437),     # jeq #437 (openat2) -> allow
        stmt(0x06, 0x00050001),   # ret SECCOMP_RET_ERRNO | EPERM
        stmt(0x06, 0x7fff0000),   # ret SECCOMP_RET_ALLOW
    ])

    bpf_buf = ctypes.create_string_buffer(bpf_program)
    fprog = struct.pack("HP", len(bpf_program) // 8, ctypes.addressof(bpf_buf))
    fprog_buf = ctypes.create_string_buffer(fprog)

    PR_SET_SECCOMP = 22
    SECCOMP_MODE_FILTER = 2
    assert libc.prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, ctypes.addressof(fprog_buf), 0, 0) == 0, (
        "Failed to install seccomp filter"
    )


def call_set_image_from_fd(bus: Gio.DBusConnection, fd: int):
    fd_list = Gio.UnixFDList.new_from_array([fd])
    return bus.call_with_unix_fd_list_sync(
        DBUS_DEST,
        DBUS_PATH,
        DBUS_IFACE,
        "SetImageFromFD",
        GLib.Variant("(h)", (0,)),
        GLib.VariantType("(u)"),
        Gio.DBusCallFlags.NONE,
        -1,
        fd_list,
        None,
    )

@contextmanager
def isolated_gnome_shell(allow_exe: str | None = None):
    """Starts an isolated gnome-shell --devkit --wayland session and enables the extension."""
    xdg_config_home = tempfile.mkdtemp(prefix="gsr-ext-test-")
    env = os.environ.copy()
    env["XDG_CONFIG_HOME"] = xdg_config_home
    if allow_exe is not None:
        env["GSR_CLIPBOARD_TEST_ALLOW_EXE"] = allow_exe
    else:
        env.pop("GSR_CLIPBOARD_TEST_ALLOW_EXE", None)

    shell_proc = subprocess.Popen(
        ["gnome-shell", "--devkit", "--wayland"],
        env=env,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    try:
        bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)

        # Wait until org.gnome.Shell.Extensions is registered on the session bus
        deadline = time.monotonic() + 10.0
        while time.monotonic() < deadline:
            try:
                bus.call_sync(
                    "org.gnome.Shell",
                    "/org/gnome/Shell",
                    "org.gnome.Shell.Extensions",
                    "ListExtensions",
                    None,
                    None,
                    Gio.DBusCallFlags.NONE,
                    500,
                    None,
                )
                break
            except GLib.Error:
                time.sleep(0.1)
        else:
            raise TimeoutError("Timed out waiting for gnome-shell D-Bus interface")

        subprocess.run(["gnome-extensions", "enable", EXTENSION_UUID], env=env, check=True)

        # Wait until gnome-shell discovers the extension and exports its D-Bus name
        deadline = time.monotonic() + 10.0
        while time.monotonic() < deadline:
            subprocess.run(
                ["gnome-extensions", "enable", EXTENSION_UUID],
                env=env,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            try:
                owner = bus.call_sync(
                    "org.freedesktop.DBus",
                    "/org/freedesktop/DBus",
                    "org.freedesktop.DBus",
                    "GetNameOwner",
                    GLib.Variant("(s)", (DBUS_DEST,)),
                    GLib.VariantType("(s)"),
                    Gio.DBusCallFlags.NONE,
                    500,
                    None,
                )
                if owner:
                    break
            except GLib.Error:
                time.sleep(0.1)
        else:
            raise TimeoutError(f"Timed out waiting for D-Bus name {DBUS_DEST}")

        yield bus
    finally:
        shell_proc.terminate()
        try:
            shell_proc.wait(timeout=3.0)
        except subprocess.TimeoutExpired:
            shell_proc.kill()
            shell_proc.wait()
        shutil.rmtree(xdg_config_home, ignore_errors=True)


def run_in_subprocess(target_fn, use_root_owned_python: bool, allow_exe: str | None = None):
    """Runs a single test assertion inside a clean child Python interpreter."""
    python_bin = os.path.realpath(sys.executable)
    temp_py = None
    try:
        if not use_root_owned_python:
            fd, temp_py = tempfile.mkstemp(prefix="user-python-")
            os.close(fd)
            shutil.copy2(python_bin, temp_py)
            os.chmod(temp_py, 0o755)
            exec_bin = temp_py
        else:
            exec_bin = python_bin

        env = os.environ.copy()
        if allow_exe is not None:
            env["GSR_CLIPBOARD_TEST_ALLOW_EXE"] = allow_exe

        res = subprocess.run(
            [exec_bin, __file__, "--worker", target_fn],
            env=env,
            capture_output=True,
            text=True,
        )
        if res.stdout:
            print(res.stdout.strip())
        if res.returncode != 0:
            raise AssertionError(f"Worker '{target_fn}' failed:\n{res.stderr}")
    finally:
        if temp_py and os.path.exists(temp_py):
            os.unlink(temp_py)


# --- Individual Worker Test Cases ---

def worker_test_non_root_binary_rejected():
    bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
    fd = os.open(SAMPLE_PNG, os.O_RDONLY)
    try:
        call_set_image_from_fd(bus, fd)
        raise AssertionError("User-owned binary was unexpectedly accepted")
    except GLib.Error as e:
        assert "not owned by root" in e.message, e.message
        print(f"PASS [Test 1 - Non-root binary rejected]: {e.message}")


def worker_test_wrong_root_path_rejected():
    bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
    fd = os.open(SAMPLE_PNG, os.O_RDONLY)
    try:
        call_set_image_from_fd(bus, fd)
        raise AssertionError("Unauthorized root-owned binary path was accepted")
    except GLib.Error as e:
        assert "Unauthorized binary path" in e.message, e.message
        print(f"PASS [Test 2 - Wrong root path rejected]: {e.message}")


def worker_test_unsandboxed_caller_rejected():
    bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
    fd = os.open(SAMPLE_PNG, os.O_RDONLY)
    try:
        call_set_image_from_fd(bus, fd)
        raise AssertionError("Unsandboxed caller was accepted")
    except GLib.Error as e:
        assert "NoNewPrivs not enabled" in e.message, e.message
        print(f"PASS [Test 3 - Un-sandboxed caller rejected]: {e.message}")


def worker_test_sandboxed_fd_validation():
    # Open all test file descriptors BEFORE enabling seccomp
    with tempfile.NamedTemporaryFile(delete=False) as tf:
        temp_path = tf.name
    fd_rw = os.open(temp_path, os.O_RDWR)
    os.unlink(temp_path)

    fd_bad_magic = os.open("/etc/os-release", os.O_RDONLY)
    fd_dir = os.open("/tmp", os.O_RDONLY | os.O_DIRECTORY)
    fd_png = os.open(SAMPLE_PNG, os.O_RDONLY)

    # Now lock down the process with NoNewPrivs + seccomp
    apply_seccomp_sandbox()
    bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)

    # Test 4: O_RDWR rejected
    try:
        call_set_image_from_fd(bus, fd_rw)
        raise AssertionError("O_RDWR FD accepted")
    except GLib.Error as e:
        assert "Error.InvalidFD" in e.message, e.message
        print(f"PASS [Test 4 - O_RDWR rejected]: {e.message}")

    # Test 5: Invalid magic bytes rejected
    try:
        call_set_image_from_fd(bus, fd_bad_magic)
        raise AssertionError("Bad magic bytes accepted")
    except GLib.Error as e:
        assert "Error.InvalidMagic" in e.message, e.message
        print(f"PASS [Test 5 - Bad Magic rejected]: {e.message}")

    # Test 6: Directory FD rejected
    try:
        call_set_image_from_fd(bus, fd_dir)
        raise AssertionError("Directory FD accepted")
    except GLib.Error as e:
        assert "Error.InvalidFD" in e.message, e.message
        print(f"PASS [Test 6 - Directory FD rejected]: {e.message}")

    # Test 7: Valid PNG from authorized root binary + NoNewPrivs + Seccomp accepted
    variant, _ = call_set_image_from_fd(bus, fd_png)
    assert variant.unpack() == (4680,), variant.unpack()
    print(f"PASS [Test 7 - Valid PNG in Root Binary + Seccomp]: {variant.unpack()}")


WORKERS = {
    "test_non_root": worker_test_non_root_binary_rejected,
    "test_wrong_path": worker_test_wrong_root_path_rejected,
    "test_unsandboxed": worker_test_unsandboxed_caller_rejected,
    "test_sandboxed_fds": worker_test_sandboxed_fd_validation,
}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--worker", choices=list(WORKERS.keys()), default=None)
    args = parser.parse_args()

    if args.worker is not None:
        WORKERS.get(args.worker)()
        return

    # Re-exec inside an isolated dbus-run-session if not already running under one
    if os.environ.get("GSR_IN_DBUS_SESSION") != "1":
        env = os.environ.copy()
        env.update({"GSR_IN_DBUS_SESSION": "1"})
        os.execvpe("dbus-run-session", ["dbus-run-session", sys.executable, os.path.abspath(__file__)], env)


    root_python = os.path.realpath(sys.executable)

    # Phase A: Without test binary override
    with isolated_gnome_shell(allow_exe=None):
        run_in_subprocess("test_non_root", use_root_owned_python=False)
        run_in_subprocess("test_wrong_path", use_root_owned_python=True)

    # Phase B: With root-owned binary explicitly allowed via test env var
    with isolated_gnome_shell(allow_exe=root_python):
        run_in_subprocess("test_unsandboxed", use_root_owned_python=True, allow_exe=root_python)
        run_in_subprocess("test_sandboxed_fds", use_root_owned_python=True, allow_exe=root_python)

    print("\nAll extension integration tests passed!")


if __name__ == "__main__":
    main()
