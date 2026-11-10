# New3DS integration additions by Kevin Boggs.
"""Protocol checks without connecting to a console or touching an SD card."""
import importlib.util
from pathlib import Path
import struct
import unittest
from unittest.mock import MagicMock, patch
import zlib

spec = importlib.util.spec_from_file_location("hardware", Path(__file__).parents[1] / "tools/hardware.py")
hardware = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hardware)


# Kevin Boggs: HardwareTests
class HardwareTests(unittest.TestCase):
    # Kevin Boggs: test_fixed_destination_and_compression
    def test_fixed_destination_and_compression(self):
        connection = MagicMock()
        connection.__enter__.return_value = connection
        connection.recv.side_effect = [b"\0"] * 8  # fragmented replies
        data = b"3DSX" + bytes(range(256)) * 513
        with patch.object(hardware.socket, "create_connection", return_value=connection):
            hardware.upload_hardware_build("127.0.0.1", data)
        sent = [call.args[0] for call in connection.sendall.call_args_list]
        expected = (struct.pack("<I", len(hardware.DESTINATION)) + hardware.DESTINATION
                    + struct.pack("<I", len(data)))
        self.assertEqual(sent[0], expected)
        self.assertEqual(sent[-1], struct.pack("<I", len(hardware.ARGUMENTS)) + hardware.ARGUMENTS)
        compressed = bytearray()
        for packet in sent[1:-1]:
            size, = struct.unpack("<I", packet[:4])
            self.assertEqual(size, len(packet) - 4)
            self.assertLessEqual(size, 16384)
            compressed.extend(packet[4:])
        self.assertEqual(zlib.decompress(compressed), data)

    # Kevin Boggs: test_rejected_destination_stops_transfer
    def test_rejected_destination_stops_transfer(self):
        connection = MagicMock()
        connection.__enter__.return_value = connection
        connection.recv.return_value = struct.pack("<i", -1)
        with patch.object(hardware.socket, "create_connection", return_value=connection):
            with self.assertRaisesRegex(RuntimeError, "destination"):
                hardware.upload_hardware_build("127.0.0.1", b"3DSX" * 8)
        self.assertEqual(connection.sendall.call_count, 1)

    # Kevin Boggs: test_incomplete_or_failed_results_are_not_passes
    def test_incomplete_or_failed_results_are_not_passes(self):
        passed = "RESULT: PASS encounter\nHARDWARE RESULTS END status=0\n"
        self.assertTrue(hardware.hardware_result_passed(passed))
        for text in ["", "RESULT: PASS encounter", passed + "RESULT: FAIL something",
                     passed + "FATAL: error", passed.replace("status=0", "status=1"),
                     passed.replace("status=0", "status=01"),
                     passed.replace("PASS encounter", "PASS async reader")]:
            self.assertFalse(hardware.hardware_result_passed(text))
        menu = passed.replace("PASS encounter", "PASS menu idle and diagnostic gate")
        self.assertTrue(hardware.hardware_result_passed(menu, "RESULT: PASS menu idle and diagnostic gate"))

    # Kevin Boggs: test_netloader_releases_all_buttons
    def test_netloader_releases_all_buttons(self):
        connection = MagicMock()
        connection.__enter__.return_value = connection
        with patch.object(hardware.socket, "socket", return_value=connection), patch.object(hardware.time, "sleep"), patch.object(hardware, "netloader_ready", side_effect=[False, True]):
            hardware.enter_netloader("127.0.0.1")
        packets = [struct.unpack("<5I", c.args[0]) for c in connection.sendto.call_args_list]
        self.assertTrue(all(packet[0] == 0xfff for packet in packets[-6:]))
        self.assertTrue(any(packet[0] == (0xfff ^ 2048) for packet in packets))
        self.assertTrue(all(packet[0] in (0xfff, 0xfff ^ 2048) for packet in packets))

    # Kevin Boggs: test_netloader_requires_acknowledgement
    def test_netloader_requires_acknowledgement(self):
        connection = MagicMock()
        connection.__enter__.return_value = connection
        with patch.object(hardware.socket, "socket", return_value=connection), patch.object(hardware.time, "sleep"), patch.object(hardware, "netloader_ready", return_value=False) as ready:
            with self.assertRaisesRegex(RuntimeError, "no upload"):
                hardware.enter_netloader("127.0.0.1")
        self.assertEqual(ready.call_count, 3)  # initial probe and two bounded attempts
        self.assertEqual(struct.unpack("<5I", connection.sendto.call_args.args[0])[0], 0xfff)

    # Kevin Boggs: test_netloader_already_ready_sends_no_input
    def test_netloader_already_ready_sends_no_input(self):
        connection = MagicMock()
        connection.__enter__.return_value = connection
        with patch.object(hardware.socket, "socket", return_value=connection), patch.object(hardware, "netloader_ready", return_value=True):
            hardware.enter_netloader("127.0.0.1")
        connection.sendto.assert_not_called()
        connection.bind.assert_called_once_with(("", hardware.PORT))

    # Kevin Boggs: test_netloader_discovery_validates_peer_and_payload
    def test_netloader_discovery_validates_peer_and_payload(self):
        connection = MagicMock()
        connection.recvfrom.side_effect = [
            (b"boot3ds", ("127.0.0.2", hardware.PORT)),
            (b"wrong", ("127.0.0.1", hardware.PORT)),
            (b"boot3ds", ("127.0.0.1", hardware.PORT)),
        ]
        self.assertTrue(hardware.netloader_ready(connection, "127.0.0.1"))
        connection.recvfrom.side_effect = hardware.socket.timeout
        self.assertFalse(hardware.netloader_ready(connection, "127.0.0.1"))


if __name__ == "__main__":
    unittest.main()
