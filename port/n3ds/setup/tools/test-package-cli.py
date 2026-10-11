"""Asset-free packaging regressions; run on Windows as well as Unix hosts."""
import importlib.util
from pathlib import Path
import tarfile
import tempfile
import unittest
import zipfile

spec = importlib.util.spec_from_file_location("package_cli", Path(__file__).with_name("package-cli.py"))
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


class PackageTests(unittest.TestCase):
    def test_unix_archives_from_non_executable_files(self):
        for runtime in ("linux-x64", "linux-arm64", "osx-x64", "osx-arm64"):
            with self.subTest(runtime=runtime), tempfile.TemporaryDirectory() as temp:
                output = Path(temp)
                app = output / "app"
                (app / "Licenses").mkdir(parents=True)
                executable = app / "halo3ds-setup"
                executable.write_bytes(b"test launcher")
                executable.chmod(0o644)
                (app / "Licenses/notice.txt").write_text("test notice", encoding="utf-8")
                package.write_launcher(app, runtime)
                if runtime.startswith("osx-"):
                    (app / "Start Halo3DS Setup.command").chmod(0o644)
                with tarfile.open(package.create_archive(output, runtime)) as archive:
                    self.assertEqual(archive.getmember("app/halo3ds-setup").mode, 0o755)
                    self.assertEqual(archive.getmember("app").mode, 0o755)
                    self.assertEqual(archive.getmember("app/Licenses").mode, 0o755)
                    self.assertEqual(archive.getmember("app/Licenses/notice.txt").mode, 0o644)
                    self.assertEqual(archive.extractfile("app/halo3ds-setup").read(), b"test launcher")
                    if runtime.startswith("osx-"):
                        name = "app/Start Halo3DS Setup.command"
                        self.assertEqual(archive.getmember(name).mode, 0o755)
                        script = archive.extractfile(name).read()
                        self.assertTrue(script.startswith(b"#!/bin/bash\n"))
                        self.assertNotIn(b"\r", script)

    def test_windows_zip_keeps_launcher(self):
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp)
            app = output / "app"
            app.mkdir()
            (app / "halo3ds-setup.exe").write_bytes(b"test executable")
            package.write_launcher(app, "win-x64")
            with zipfile.ZipFile(package.create_archive(output, "win-x64")) as archive:
                self.assertEqual(archive.read("app/halo3ds-setup.exe"), b"test executable")
                self.assertIn(b'"%~dp0halo3ds-setup.exe" guided', archive.read("app/Start Halo3DS Setup.cmd"))


if __name__ == "__main__":
    unittest.main()
