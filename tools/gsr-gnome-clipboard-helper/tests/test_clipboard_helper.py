#!/usr/bin/env python3
import array
import os
import socket
import struct
import subprocess
import tempfile
import time
import signal

HELPER_BIN = "buildDir/gsr-gnome-clipboard-helper"


def send_fd(sock: socket.socket, fd: int):
    msg = b"\x01"
    ancdata = [(socket.SOL_SOCKET, socket.SCM_RIGHTS, array.array("i", [fd]).tobytes())]
    sock.sendmsg([msg], ancdata)


def send_clear(sock: socket.socket):
    sock.sendmsg([b"\x00"])


def get_open_fds(pid: int) -> list[int]:
    return sorted(int(fd) for fd in os.listdir(f"/proc/{pid}/fd"))


def spawn_helper():
    parent_sock, child_sock = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET | socket.SOCK_NONBLOCK)
    parent_sock.shutdown(socket.SHUT_RD)
    child_sock.shutdown(socket.SHUT_WR)

    def preexec():
        os.dup2(child_sock.fileno(), 3)
        os.set_inheritable(3, True)

    proc = subprocess.Popen(
        [HELPER_BIN],
        pass_fds=(3,),
        preexec_fn=preexec,
        stderr=subprocess.PIPE,
        text=True,
    )
    child_sock.close()
    time.sleep(0.05)
    return proc, parent_sock



def test_negative_op1_without_fd():
    print("[TEST 2] Negative: op=1 without attached SCM_RIGHTS FD...")
    proc, sock = spawn_helper()
    try:
        sock.sendmsg([b"\x01"])
        rc = proc.wait(timeout=1.0)
        assert rc == 1, f"Expected clean error exit code 1, got {rc}"
        print("  PASS")
    finally:
        sock.close()
        if proc.poll() is None:
            proc.kill()
            proc.wait()


def test_negative_ctrunc_multiple_fds():
    print("[TEST 3] Negative: MSG_CTRUNC (sending 3 FDs to exceed 24-byte CMSG_SPACE)...")
    proc, sock = spawn_helper()
    try:
        with tempfile.NamedTemporaryFile() as f1, tempfile.NamedTemporaryFile() as f2, tempfile.NamedTemporaryFile() as f3:
            fds = array.array("i", [f1.fileno(), f2.fileno(), f3.fileno()]).tobytes()
            sock.sendmsg([b"\x01"], [(socket.SOL_SOCKET, socket.SCM_RIGHTS, fds)])
        rc = proc.wait(timeout=1.0)
        assert rc == 1, f"Expected clean error exit code 1 on MSG_CTRUNC, got {rc}"
        print("  PASS")
    finally:
        sock.close()
        if proc.poll() is None:
            proc.kill()
            proc.wait()



def test_negative_zero_byte_datagram():
    print("[TEST 4] Negative: 0-byte datagram (ambiguous EOF)...")
    proc, sock = spawn_helper()
    try:
        sock.sendmsg([b""])
        rc = proc.wait(timeout=1.0)
        assert rc == 0, f"Expected clean exit on 0-byte packet, got {rc}"
        print("  PASS")
    finally:
        sock.close()
        if proc.poll() is None:
            proc.kill()
            proc.wait()


def test_negative_invalid_op_byte():
    print("[TEST 5] Negative: Invalid op byte (0x02, 0xFF)...")
    for bad_op in (b"\x02", b"\xff"):
        proc, sock = spawn_helper()
        try:
            sock.sendmsg([bad_op])
            rc = proc.wait(timeout=1.0)
            assert rc == 1, f"Expected exit code 1 for op={bad_op!r}, got {rc}"
        finally:
            sock.close()
            if proc.poll() is None:
                proc.kill()
                proc.wait()
    print("  PASS")


def test_negative_oversized_payload():
    print("[TEST 6] Negative: Oversized payload (2 bytes -> MSG_TRUNC)...")
    proc, sock = spawn_helper()
    try:
        sock.sendmsg([b"\x00\x00"])
        rc = proc.wait(timeout=1.0)
        assert rc == 1, f"Expected exit code 1 on MSG_TRUNC, got {rc}"
        print("  PASS")
    finally:
        sock.close()
        if proc.poll() is None:
            proc.kill()
            proc.wait()


def test_negative_clear_with_attached_fd():
    print("[TEST 7] Negative: CLEAR (op=0) with unexpected SCM_RIGHTS FD...")
    proc, sock = spawn_helper()
    try:
        with tempfile.NamedTemporaryFile() as tmp:
            fds = array.array("i", [tmp.fileno()]).tobytes()
            sock.sendmsg([b"\x00"], [(socket.SOL_SOCKET, socket.SCM_RIGHTS, fds)])
        rc = proc.wait(timeout=1.0)
        assert rc == 1, f"Expected exit code 1 when op=0 carries an FD, got {rc}"
        print("  PASS")
    finally:
        sock.close()
        if proc.poll() is None:
            proc.kill()
            proc.wait()


def test_negative_two_fds_in_padding():
    print("[TEST 8] Negative: 2 FDs fitting inside 24-byte CMSG_SPACE alignment...")
    proc, sock = spawn_helper()
    try:
        with tempfile.NamedTemporaryFile() as f1, tempfile.NamedTemporaryFile() as f2:
            fds = array.array("i", [f1.fileno(), f2.fileno()]).tobytes()
            sock.sendmsg([b"\x01"], [(socket.SOL_SOCKET, socket.SCM_RIGHTS, fds)])
        rc = proc.wait(timeout=1.0)
        assert rc == 1, f"Expected exit code 1 when 2 FDs are sent, got {rc}"
        print("  PASS")
    finally:
        sock.close()
        if proc.poll() is None:
            proc.kill()
            proc.wait()


def test_dynamic_downlink_lifecycle():
    print("[TEST 1] Dynamic lifecycle: unready burst -> enable -> alive -> disable -> clear...")
    proc, sock = spawn_helper()
    try:
        baseline_fds = get_open_fds(proc.pid)


        for i in range(20):
            with tempfile.NamedTemporaryFile() as tmp:
                tmp.write(b"\x89PNG\r\n\x1a\n" + (b"x" * (1000 + i)))
                tmp.flush()
                send_fd(sock, tmp.fileno())

        time.sleep(0.05)
        assert len(get_open_fds(proc.pid)) == len(baseline_fds) + 1


        proc.send_signal(signal.SIGUSR1)
        time.sleep(0.05)
        assert get_open_fds(proc.pid) == baseline_fds


        with tempfile.NamedTemporaryFile() as tmp:
            tmp.write(b"\x89PNG\r\n\x1a\n1234")
            tmp.flush()
            send_fd(sock, tmp.fileno())
        time.sleep(0.05)
        assert get_open_fds(proc.pid) == baseline_fds


        proc.send_signal(signal.SIGUSR1)
        with tempfile.NamedTemporaryFile() as tmp:
            tmp.write(b"\x89PNG\r\n\x1a\n5678")
            tmp.flush()
            send_fd(sock, tmp.fileno())
        time.sleep(0.05)
        assert len(get_open_fds(proc.pid)) == len(baseline_fds) + 1

        send_clear(sock)
        time.sleep(0.05)
        assert get_open_fds(proc.pid) == baseline_fds

        sock.close()
        assert proc.wait(timeout=1.0) == 0
        print("  PASS")
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait()



if __name__ == "__main__":
    test_negative_op1_without_fd()
    test_negative_ctrunc_multiple_fds()
    test_negative_zero_byte_datagram()
    test_negative_invalid_op_byte()
    test_negative_oversized_payload()
    test_negative_clear_with_attached_fd()
    test_negative_two_fds_in_padding()
    test_dynamic_downlink_lifecycle()

    print("\nAll integration tests passed!")

