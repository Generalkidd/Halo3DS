#!/usr/bin/env python3
# New3DS integration additions by Kevin Boggs.
"""Upload to one Halo3DS entry; optionally collect a bounded hardware test.

Requires hbmenu netloader and, for --enter-netloader, Rosalina InputRedirection.
Protocol: devkitPro/3ds-hbmenu source/ui/netloader.c and
LumaTeam/Luma3DS sysmodules/rosalina/source/input_redirection.c.
"""
import argparse
import hashlib
import ipaddress
import json
from pathlib import Path
import socket
import struct
import time
import zlib

DESTINATION = b"Halo3DS/Halo3DS.3dsx"
ARGUMENTS = b"sdmc:/3ds/Halo3DS/Halo3DS.3dsx\0"
PORT = 17491
MAX_RESULTS = 8 * 1024 * 1024


# Kevin Boggs: receive_exact
def receive_exact(connection, count):
    data = bytearray()
    while len(data) < count:
        chunk = connection.recv(count - len(data))
        if not chunk:
            raise RuntimeError("Console closed the transfer connection")
        data.extend(chunk)
    return bytes(data)


# Kevin Boggs: netloader_ready
def netloader_ready(connection, address, attempts=3):
    """Verify hbmenu's discovery reply without consuming its TCP upload slot."""
    # hbmenu replies to fixed UDP port 17491, not the sender's ephemeral port.
    connection.settimeout(1)
    for _ in range(attempts):
        connection.sendto(b"3dsboot", (address, PORT))
        try:
            reply, peer = connection.recvfrom(64)
        except socket.timeout:
            continue
        if peer == (address, PORT) and reply == b"boot3ds":
            return True
    return False


# Kevin Boggs: enter_netloader
def enter_netloader(address):
    """Bounded Y presses with release and positive netloader confirmation."""
    neutral = struct.pack("<5I", 0xfff, 0x02000000, 0x007ff7ff, 0x80800081, 0)
    pressed = struct.pack("<5I", 0xfff ^ 2048, 0x02000000, 0x007ff7ff, 0x80800081, 0)
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as connection:
        connection.bind(("", PORT))
        if netloader_ready(connection, address, 1):
            return
        # Result delivery precedes the application-to-launcher handoff. A brief
        # tap can be lost during startup; hold through multiple input polls.
        time.sleep(3)
        target = (address, 4950)
        for attempt in range(2):
            try:
                connection.sendto(neutral, target)
                time.sleep(0.25)
                for _ in range(90):
                    connection.sendto(pressed, target)
                    time.sleep(1 / 60)
            finally:
                for _ in range(6):
                    connection.sendto(neutral, target)
                    time.sleep(1 / 60)
            if netloader_ready(connection, address, 4):
                return
            if not attempt:
                time.sleep(2)
    raise RuntimeError("Homebrew Launcher did not confirm netloader readiness; no upload was attempted")


# Kevin Boggs: upload_hardware_build
def upload_hardware_build(address, data):
    # Never change the destination to the local test filename: hbmenu persists
    # netloader uploads, which otherwise creates a new launcher entry each run.
    with socket.create_connection((address, PORT), timeout=15) as connection:
        connection.settimeout(30)
        connection.sendall(struct.pack("<I", len(DESTINATION)) + DESTINATION + struct.pack("<I", len(data)))
        status = struct.unpack("<i", receive_exact(connection, 4))[0]
        if status:
            raise RuntimeError(f"Console could not open the destination: {status}")
        compressed = zlib.compress(data)
        for offset in range(0, len(compressed), 16384):
            chunk = compressed[offset:offset + 16384]
            connection.sendall(struct.pack("<I", len(chunk)) + chunk)
        status = struct.unpack("<i", receive_exact(connection, 4))[0]
        if status:
            raise RuntimeError(f"Console rejected the executable: {status}")
        connection.sendall(struct.pack("<I", len(ARGUMENTS)) + ARGUMENTS)


# Kevin Boggs: collect_hardware_results
def collect_hardware_results(listener, address, path, timeout):
    deadline = time.monotonic() + timeout
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("No test result from the console; no further input was sent")
        listener.settimeout(remaining)
        connection, peer = listener.accept()
        if peer[0] != address:
            connection.close()
            continue
        with connection, path.open("wb") as output:
            size = 0
            while True:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError("Test result transfer exceeded its deadline")
                connection.settimeout(min(30, remaining))
                chunk = connection.recv(8192)
                if not chunk:
                    break
                size += len(chunk)
                if size > MAX_RESULTS:
                    raise RuntimeError("Test result exceeded the 8 MiB limit")
                output.write(chunk)
                output.flush()
        return path.read_text(encoding="utf-8", errors="replace")


# Kevin Boggs: hardware_result_passed
def hardware_result_passed(text, expected="RESULT: PASS encounter"):
    lines = text.splitlines()
    return (any(line == expected or line.startswith(expected + " ") for line in lines)
            and "RESULT: FAIL" not in text and "FATAL:" not in text
            and "HARDWARE RESULTS END status=0" in lines)


# Kevin Boggs: hardware_main
def hardware_main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path)
    parser.add_argument("--address", required=True, type=ipaddress.IPv4Address)
    parser.add_argument("--enter-netloader", action="store_true",
                        help="Send Y first; use only when the console is already in Homebrew Launcher")
    parser.add_argument("--results", type=Path,
                        help="Wait for a reporting test build, save results and verify its PASS status")
    parser.add_argument("--timeout", type=int, default=600)
    parser.add_argument("--expect", default="RESULT: PASS encounter",
                        help="Required fixture completion marker; initialization checks alone cannot pass a run")
    args = parser.parse_args()
    if not 30 <= args.timeout <= 1800:
        parser.error("Timeout must be between 30 and 1800 seconds")
    if args.results and args.results.suffix.lower() == ".json":
        parser.error("Use a text result filename; .json is reserved for measurement metadata")
    if not args.expect.startswith("RESULT: PASS ") or "\n" in args.expect:
        parser.error("Expected completion must be one RESULT: PASS line naming the test")
    data = args.build.read_bytes()
    if len(data) < 32 or data[:4] != b"3DSX":
        parser.error("Build is not a 3DSX executable")
    address = str(args.address)
    listener = None
    record = {"sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data),
              "destination": ARGUMENTS[:-1].decode(), "expected": args.expect, "passed": False}
    try:
        if args.results:
            args.results.parent.mkdir(parents=True, exist_ok=True)
            if args.results.exists() or args.results.with_suffix(".json").exists():
                parser.error("Choose a new result filename; previous measurements are preserved")
            listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            listener.bind(("", PORT))
            listener.listen(1)
        if args.enter_netloader:
            enter_netloader(address)
        upload_hardware_build(address, data)
        print("Uploaded to sdmc:/3ds/Halo3DS/Halo3DS.3dsx", flush=True)
        if listener:
            text = collect_hardware_results(listener, address, args.results, args.timeout)
            record["passed"] = hardware_result_passed(text, args.expect)
            args.results.with_suffix(".json").write_text(json.dumps(record, indent=2) + "\n")
            for line in text.splitlines():
                if line.startswith(("RESULT:", "ENCOUNTER COMPLETE:", "LEAN PROFILE:")):
                    print(line)
            return 0 if record["passed"] else 1
        return 0
    finally:
        if listener:
            listener.close()


if __name__ == "__main__":
    raise SystemExit(hardware_main())
