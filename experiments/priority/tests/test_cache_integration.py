#!/usr/bin/env python3
"""Check real replies, in-flight repeats and completed cache hits for all variants."""
import hashlib
import os
from pathlib import Path
import re
import socket
import struct
import subprocess
import sys
import tempfile
import time


def request(connection, target, priority):
    digest = hashlib.sha256(struct.pack("<Q", target)).digest()
    connection.sendall(digest + struct.pack(">QQB", 0, target + 1, priority))


def response(connection, target):
    data = b""
    while len(data) < 8:
        part = connection.recv(8 - len(data))
        assert part, "Connection closed before the answer"
        data += part
    assert struct.unpack(">Q", data)[0] == target
    assert connection.recv(1) == b"", "Server did not close a completed request"
    connection.close()


def check(binary):
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        port = reservation.getsockname()[1]
    with tempfile.TemporaryFile(mode="w+") as log:
        server = subprocess.Popen([str(binary), str(port)],
                                  env=dict(os.environ, POOL_THREADS="1"),
                                  stdout=log, stderr=log)
        sockets = []
        try:
            deadline = time.monotonic() + 10
            while True:
                try:
                    first = socket.create_connection(("127.0.0.1", port), timeout=30)
                    break
                except ConnectionRefusedError:
                    assert server.poll() is None, "Server exited before startup"
                    assert time.monotonic() < deadline, "Server startup timed out"
                    time.sleep(0.02)
            sockets.append(first)
            target = 5_000_000
            request(first, target, 1)
            for number, priority in ((target, 16), (20, 16), (target, 2)):
                connection = socket.create_connection(("127.0.0.1", port), timeout=30)
                sockets.append(connection)
                request(connection, number, priority)
            for connection, number in zip(sockets, (target, target, 20, target)):
                response(connection, number)
            hit = socket.create_connection(("127.0.0.1", port), timeout=30)
            sockets.append(hit)
            request(hit, target, 16)
            response(hit, target)
            log.seek(0)
            output = log.read()
            counts = re.findall(r"cache hits: (\d+), waits: (\d+)", output)
            assert counts and int(counts[-1][0]) >= 1, output
            assert int(counts[-1][1]) >= 2, output
            print(f"{binary.name}: five correct replies, in-flight repeats and cache hit passed")
        finally:
            for connection in sockets:
                connection.close()
            server.terminate()
            try:
                server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait()


if __name__ == "__main__":
    root = Path(sys.argv[1]).resolve()
    for name in ("bin/server-baseline", "bin/priority/server-priorityQueue",
                 "bin/priority/server-priorityChunked"):
        check(root / name)
