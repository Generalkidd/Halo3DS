from pathlib import Path
import sys
import unittest


N3DS = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(N3DS))
import build


class WideAbiTests(unittest.TestCase):
    def test_detects_native_wide_calls(self):
        output = """\
runtime.o: U wcslen
game.o: U wcscpy
game.o: U getwchar
game.o: U putwchar
"""
        self.assertEqual(build.incompatible_wide_references(output), output.splitlines())

    def test_allows_compatibility_and_narrow_calls(self):
        output = """\
runtime.o: U msvc_wcslen
game.o: U memcpy
game.o: U application_wcslen
"""
        self.assertEqual(build.incompatible_wide_references(output), [])


if __name__ == "__main__":
    unittest.main()
