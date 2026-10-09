#!/usr/bin/env python3
"""Unit tests for the installer's helpers (no game files, no network): python3 test_setup.py"""
import hashlib
import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import setup  # noqa: E402

KEY_HEX = "0011223344556677" "8899aabbccddeeff"  # made-up test value


class Keys(unittest.TestCase):
    def test_raw_and_hex(self):
        raw = bytes(range(16))
        self.assertEqual(setup.parse_key(raw), raw)
        self.assertEqual(setup.parse_key(KEY_HEX), bytes.fromhex(KEY_HEX))
        self.assertEqual(setup.parse_key(KEY_HEX.upper() + "\r\n"), bytes.fromhex(KEY_HEX))
        self.assertEqual(setup.parse_key("0x" + KEY_HEX), bytes.fromhex(KEY_HEX))
        self.assertEqual(setup.parse_key(" ".join(KEY_HEX[i:i + 8] for i in range(0, 32, 8))), bytes.fromhex(KEY_HEX))
        self.assertEqual(setup.parse_key(KEY_HEX.encode()), bytes.fromhex(KEY_HEX))

    def test_malformed(self):
        for bad in ("", "1234", KEY_HEX[:-1], KEY_HEX + "0", "zz" * 16, b"\xff" * 15, b"\xff" * 17):
            self.assertIsNone(setup.parse_key(bad), bad)

    def test_key_file(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "k.key")
            with open(p, "wb") as f:
                f.write(bytes(range(16)))
            self.assertEqual(setup.read_key_file(p), bytes(range(16)))
            with open(p, "w") as f:
                f.write(KEY_HEX + "\n")
            self.assertEqual(setup.read_key_file(p), bytes.fromhex(KEY_HEX))
            with open(p, "wb") as f:
                f.write(b"x" * 5000)
            self.assertIsNone(setup.read_key_file(p))
            self.assertIsNone(setup.read_key_file(os.path.join(d, "missing")))

    def test_stdin_blob(self):
        k = setup.Keys()
        k.disc, k.common = bytes(16), bytes.fromhex(KEY_HEX)
        self.assertEqual(k.stdin_blob(), ("disc %s\ncommon %s\n" % ("00" * 16, KEY_HEX)).encode())


class Paths(unittest.TestCase):
    def test_clean_path(self):
        self.assertEqual(setup.clean_path('"/tmp/a b/c.wux"'), os.path.abspath("/tmp/a b/c.wux"))
        self.assertEqual(setup.clean_path("'/tmp/x'"), os.path.abspath("/tmp/x"))
        self.assertEqual(setup.clean_path("  "), "")
        if not setup.IS_WIN:
            self.assertEqual(setup.clean_path("/tmp/My\\ Game.wux "), "/tmp/My Game.wux")

    def test_game_folder(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertFalse(setup.valid_game_folder(d))
            os.makedirs(os.path.join(d, "code"))
            os.makedirs(os.path.join(d, "content"))
            os.makedirs(os.path.join(d, "meta"))
            open(os.path.join(d, "code", "Zelda.rpx"), "wb").close()
            with open(os.path.join(d, "meta", "meta.xml"), "w") as f:
                f.write('<menu><title_id type="hexBinary" length="8">000500001019E500</title_id></menu>')
            self.assertTrue(setup.valid_game_folder(d))
            self.assertEqual(setup.game_folder_title(d), "000500001019e500")


class Titles(unittest.TestCase):
    def test_usa_ok(self):
        setup.check_title("000500001019e500")

    def test_other_titles(self):
        with self.assertRaisesRegex(setup.SetupError, "not Twilight Princess HD"):
            setup.check_title("0005000010143500")  # The Wind Waker HD
        with self.assertRaisesRegex(setup.SetupError, "not Twilight Princess HD"):
            setup.check_title("000500001010ec00")


def _title(tid, version, files=10, size=1000):
    return {"id": tid, "version": version, "folder": "%s_v%d" % (tid, version), "files": files, "bytes": size}


class ArchiveTitles(unittest.TestCase):
    """Which title of a Cemu archive (.wua) is used (info as wwhd-extract --title 000500001019e500 info prints it)."""

    BASE, UPDATE = _title("000500001019e500", 81), _title("0005000e1019e500", 16)

    def info(self, titles, selected=None):
        i = {"format": "wua", "titles": titles}
        if selected:
            i.update(selected=selected["folder"], title_id=selected["id"], version=str(selected["version"]),
                     files=str(selected["files"]), bytes=str(selected["bytes"]))
        return i

    def test_base_only(self):
        self.assertEqual(setup.archive_choice(self.info([self.BASE], self.BASE)), ("000500001019e500_v81", []))

    def test_update_not_used(self):
        folder, notes = setup.archive_choice(self.info([self.UPDATE, self.BASE, _title("0005000c1019e500", 3)], self.BASE))
        self.assertEqual(folder, "000500001019e500_v81")
        self.assertEqual(len(notes), 2)
        self.assertIn("the update for Twilight Princess HD (USA), version 16", notes[0])
        self.assertIn("version 81", notes[0])
        self.assertIn("downloadable content", notes[1])

    def test_update_without_game(self):
        with self.assertRaisesRegex(setup.SetupError, "only the update"):
            setup.archive_choice(self.info([self.UPDATE]))

    def test_other_game(self):
        with self.assertRaisesRegex(setup.SetupError, "does not contain Twilight Princess HD.*title 00050000-10143500"):
            setup.archive_choice(self.info([_title("0005000010143500", 0), _title("0005000e10143500", 16)]))
        with self.assertRaisesRegex(setup.SetupError, "does not contain Twilight Princess HD.*title 00050000-1010EC00"):
            setup.archive_choice(self.info([_title("000500001010ec00", 0)]))
        with self.assertRaisesRegex(setup.SetupError, "no Wii U titles"):
            setup.archive_choice(self.info([]))

    def test_other_version(self):
        v2 = _title("000500001019e500", 2)
        with self.assertRaisesRegex(setup.SetupError, "version 2.*built for version 81"):
            setup.archive_choice(self.info([v2], v2))

    def test_title_desc(self):
        self.assertEqual(setup.title_desc("000500001019e500", 81), "Twilight Princess HD (USA), version 81")
        self.assertEqual(setup.title_desc("0005000E1019E500"), "the update for Twilight Princess HD (USA)")

    def test_plan(self):
        self.assertEqual(setup.plan_steps("archive"), ["archive", "compiler", "extract", "translate", "compile", "app"])
        self.assertEqual(setup.EXTRACT_ERRORS[10], "wrong_title")


class GameVersion(unittest.TestCase):
    """code/Zelda.rpx must be the file the port is built for (USA v81); synthetic files, made-up bytes."""

    def make(self, d, rpx=b"made-up rpx", app_tid="000500001019E500", app_ver="0051"):
        os.makedirs(os.path.join(d, "code"), exist_ok=True)
        with open(os.path.join(d, "code", "Zelda.rpx"), "wb") as f:
            f.write(rpx)
        with open(os.path.join(d, "code", "app.xml"), "w") as f:
            f.write('<app><title_id type="hexBinary" length="8">%s</title_id>\n'
                    '<title_version type="hexBinary" length="2">%s</title_version></app>' % (app_tid, app_ver))

    def setUp(self):
        self.saved = setup.SUPPORTED_RPX_SHA256
        setup.SUPPORTED_RPX_SHA256 = hashlib.sha256(b"made-up rpx").hexdigest()

    def tearDown(self):
        setup.SUPPORTED_RPX_SHA256 = self.saved

    def test_expected_file(self):
        with tempfile.TemporaryDirectory() as d:
            self.make(d)
            setup.check_game_version(d)

    def test_update_merged_in(self):
        with tempfile.TemporaryDirectory() as d:
            self.make(d, rpx=b"other code", app_ver="0060")
            with self.assertRaisesRegex(setup.SetupError, "version 96 of the game.*update merged in.*"
                                                          "00050000-1019E500, version 81.*Use the game's own files"):
                setup.check_game_version(d)
            self.make(d, rpx=b"other code", app_tid="0005000E1019E500", app_ver="0000")
            with self.assertRaisesRegex(setup.SetupError, "from the update.*merged in"):
                setup.check_game_version(d)

    def test_unknown_build(self):
        with tempfile.TemporaryDirectory() as d:
            self.make(d, rpx=b"damaged")
            with self.assertRaisesRegex(setup.SetupError, "not the expected file \\(SHA-256 [0-9a-f]{16}\\.\\.\\.\\)"):
                setup.check_game_version(d)
            os.remove(os.path.join(d, "code", "app.xml"))
            with self.assertRaisesRegex(setup.SetupError, "not the expected file"):
                setup.check_game_version(d)

    def test_other_game(self):
        with tempfile.TemporaryDirectory() as d:
            self.make(d, rpx=b"ww", app_tid="0005000010143500")
            with self.assertRaisesRegex(setup.SetupError, "from title 00050000-10143500"):
                setup.check_game_version(d)

    def test_missing(self):
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaisesRegex(setup.SetupError, "cannot read"):
                setup.check_game_version(d)

    @unittest.skipUnless(os.environ.get("WWHD_GAME_DIR"), "WWHD_GAME_DIR (your own extracted game) not set")
    def test_real_game(self):
        setup.SUPPORTED_RPX_SHA256 = self.saved
        setup.check_game_version(os.environ["WWHD_GAME_DIR"])  # read only


class Recipe(unittest.TestCase):
    def test_substitution(self):
        m = {"sdk": "/p/sdk", "gamecode": "/w/libgamecode.a", "out": "/d/bin/wwhd"}
        self.assertEqual(setup.sub("{sdk}/obj/a.o", m), "/p/sdk/obj/a.o")
        self.assertEqual(setup.sub("-I{sdk}/include", m), "-I/p/sdk/include")
        self.assertEqual(setup.sub("{gamecode}", m), "/w/libgamecode.a")
        self.assertEqual(setup.sub("-O3", m), "-O3")
        self.assertEqual(setup.fwd("C:\\a\\b"), "C:/a/b")

    def test_toolchains_pinned(self):
        tcs = setup.load_toolchains()
        for name, tc in tcs["toolchains"].items():
            if tc["kind"] != "xcode-clt":
                self.assertRegex(tc["sha256"], r"^[0-9a-f]{64}$", name)
                self.assertTrue(tc["url"].startswith("https://"), name)
        for name, py in tcs["python"].items():
            self.assertRegex(py["sha256"], r"^[0-9a-f]{64}$", name)

    def test_jobs(self):
        self.assertGreaterEqual(setup.default_jobs(), 1)


class Arch(unittest.TestCase):
    def test_normalize(self):
        for a, want in (("x86_64", "x86_64"), ("AMD64", "x86_64"), ("aarch64", "aarch64"), ("arm64", "aarch64")):
            self.assertEqual(setup.normalize_arch(a), want)
        self.assertIn(setup.host_arch(), ("x86_64", "aarch64"))

    def test_linux_pins_per_arch(self):
        tcs = setup.load_toolchains()
        self.assertEqual(tcs["toolchains"]["zig-0.16.0"]["target"].split("-")[0], "x86_64")
        self.assertEqual(tcs["toolchains"]["zig-0.16.0-aarch64"]["target"].split("-")[0], "aarch64")
        self.assertIn("aarch64", tcs["toolchains"]["zig-0.16.0-aarch64"]["url"])
        self.assertIn("aarch64", tcs["python"]["linux-aarch64"]["url"])


class NoScriptHost(unittest.TestCase):
    """Antivirus heuristics read "unsigned program starts PowerShell" as a dropper (issue #58): the Windows setup
    uses the Windows API instead (the release ships Python, setup.py uses ctypes)."""

    def test_no_powershell(self):
        here = os.path.dirname(os.path.abspath(__file__))
        for name in ("setup.py", "install-windows.bat"):
            with open(os.path.join(here, name), encoding="utf-8") as f:
                self.assertNotIn("powershell", f.read().lower(), name)
        self.assertFalse(os.path.exists(os.path.join(here, "bootstrap-windows.ps1")))

    @unittest.skipUnless(setup.IS_WIN, "Windows only")
    def test_shortcut(self):
        self.assertTrue(os.path.isdir(setup.win_known_folder(0x02)))
        self.assertTrue(os.path.isdir(setup.win_known_folder(0x10)))
        with tempfile.TemporaryDirectory() as d:
            link = os.path.join(d, "Twilight Princess HD test.lnk")
            setup.win_shortcut(link, sys.executable, "--game game --save save", d, sys.executable)
            with open(link, "rb") as f:
                head = f.read(20)
            self.assertEqual(head[:4], b"\x4c\x00\x00\x00")  # a shell link header
            self.assertEqual(head[4:20], bytes.fromhex("0114020000000000c000000000000046"))  # its CLSID
            setup.win_shortcut(link, sys.executable, workdir=d)  # replaces it


class BundledPython(unittest.TestCase):
    """The Windows release ships the pinned embeddable Python in tools/python; guard.py allows exactly its files."""

    def setUp(self):
        sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "release"))
        import guard
        self.guard = guard
        with open(guard.PYTHON_FILES) as f:
            self.expected = json.load(f)

    def test_manifest_matches_pin(self):
        pin = setup.load_toolchains()["python"]["windows"]
        self.assertEqual(self.expected["sha256"], pin["sha256"])
        self.assertEqual(self.expected["url"], pin["url"])
        for name in ("python.exe", "pythonw.exe", "python3.dll", "LICENSE.txt"):
            self.assertIn(name, self.expected["files"])

    def test_guard_rejects_other_files(self):
        with tempfile.TemporaryDirectory() as d:
            py = os.path.join(d, "TwilightPrincessHD-x", "tools", "python")
            os.makedirs(py)
            with open(os.path.join(py, "python.exe"), "wb") as f:
                f.write(b"MZ not the real one")
            with open(os.path.join(py, "dropper.exe"), "wb") as f:
                f.write(b"MZ")
            problems, _ = self.guard.scan(d)
            text = "\n".join(problems)
            self.assertIn("python.exe: differs", text)
            self.assertIn("dropper.exe: not a file", text)
            self.assertIn("lacks files", text)

    def test_windows_finds_bundled_python(self):
        # what the GUI and --console-setup start (tools/installer/gui/console_setup_win.cpp)
        with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "gui", "console_setup_win.cpp")) as f:
            self.assertIn('"tools\\\\python\\\\python.exe"', f.read())


def _write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)


def _cemu_save(root, logs=("ZTP00.dat", "ZTP02.dat"), fill=b"\1"):
    """A SYNTHETIC save in Cemu's shape (made-up bytes): <root>/1019e500/user/80000001/ZTP0x.dat and COMMON.dat.
    Returns the title's save folder."""
    title = os.path.join(root, "1019e500")
    for n in logs:
        _write(os.path.join(title, "user", "80000001", n), fill * setup.QUEST_LOG_BYTES)
    _write(os.path.join(title, "user", "80000001", "COMMON.dat"), b"made-up common")
    return title


class SaveImport(unittest.TestCase):
    """setup --import-save on synthetic Cemu / Wii U save folders."""

    def test_folder_shapes(self):
        with tempfile.TemporaryDirectory() as d:
            title = _cemu_save(d)
            account = os.path.join(title, "user", "80000001")
            _write(os.path.join(title, "meta", "saveinfo.xml"), b"x")
            full = [(n, os.path.join(account, n)) for n in ("COMMON.dat", "ZTP00.dat", "ZTP02.dat")]
            for path in (title, os.path.join(title, "user"), account, account + os.sep):
                self.assertEqual(setup.save_files(path), full, path)
            # a folder with only the Quest Logs, or one of them; other files are left out
            flat = os.path.join(d, "copied")
            _write(os.path.join(flat, "ZTP01.dat"), bytes(setup.QUEST_LOG_BYTES))
            _write(os.path.join(flat, "notes.txt"), b"x")
            self.assertEqual([n for n, _ in setup.save_files(flat)], ["ZTP01.dat"])
            self.assertEqual([n for n, _ in setup.save_files(os.path.join(flat, "ZTP01.dat"))], ["ZTP01.dat"])

    def test_refused(self):
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaisesRegex(setup.SetupError, "no Twilight Princess HD save in"):
                setup.save_files(d)
            with self.assertRaisesRegex(setup.SetupError, "no Twilight Princess HD save in"):
                setup.save_files(os.path.join(d, "missing"))
            title = _cemu_save(d)
            _write(os.path.join(title, "user", "80000002", "ZTP00.dat"), bytes(setup.QUEST_LOG_BYTES))
            with self.assertRaisesRegex(setup.SetupError, "several Wii U users \\(80000001, 80000002\\)"):
                setup.save_files(title)
            self.assertEqual(len(setup.save_files(os.path.join(title, "user", "80000002"))), 1)  # one of them
            _write(os.path.join(title, "user", "80000001", "ZTP02.dat"), b"short")
            with self.assertRaisesRegex(setup.SetupError, "ZTP02.dat is not a Twilight Princess HD Quest Log: it has 5 "
                                                          "bytes, a Quest Log has 3584"):
                setup.save_files(os.path.join(title, "user", "80000001"))

    def test_import_and_backup(self):
        with tempfile.TemporaryDirectory() as d:
            save = os.path.join(d, "data", "save")
            user = os.path.join(save, "user")
            msg = setup.import_save(_cemu_save(os.path.join(d, "a")), save)
            self.assertEqual(os.listdir(save), ["user"])  # no backup: there was no save
            self.assertEqual(sorted(os.listdir(user)), ["COMMON.dat", "ZTP00.dat", "ZTP02.dat"])
            self.assertEqual(msg, "Copied COMMON.dat, ZTP00.dat, ZTP02.dat to %s." % user)
            # a second save: the first one is moved to a dated backup folder next to it, not overwritten
            msg = setup.import_save(_cemu_save(os.path.join(d, "b"), logs=("ZTP01.dat",), fill=b"\2"), save)
            backup = [n for n in os.listdir(save) if n != "user"]
            self.assertRegex(backup[0], r"^user\.backup-\d{8}-\d{6}$")
            self.assertEqual(sorted(os.listdir(os.path.join(save, backup[0]))),
                             ["COMMON.dat", "ZTP00.dat", "ZTP02.dat"])
            self.assertEqual(sorted(os.listdir(user)), ["COMMON.dat", "ZTP01.dat"])
            with open(os.path.join(user, "ZTP01.dat"), "rb") as f:
                self.assertEqual(f.read(), b"\2" * setup.QUEST_LOG_BYTES)
            self.assertIn("The save that was there is in %s." % os.path.join(save, backup[0]), msg)
            # a refused save changes nothing
            with self.assertRaises(setup.SetupError):
                setup.import_save(os.path.join(d, "a"), save)
            self.assertEqual(sorted(os.listdir(save)), sorted(backup + ["user"]))

    def test_cemu_default_folder(self):
        saved = setup.CEMU_SAVE
        try:
            with tempfile.TemporaryDirectory() as d:
                setup.CEMU_SAVE = os.path.join(d, "1019e500")
                self.assertIsNone(setup.cemu_save())
                _cemu_save(d)
                self.assertEqual(setup.cemu_save(), setup.CEMU_SAVE)
        finally:
            setup.CEMU_SAVE = saved
        self.assertTrue(saved.endswith("/Library/Application Support/Cemu/mlc01/usr/save/00050000/1019e500"))


class NonInteractive(unittest.TestCase):
    def test_ui_refuses_to_prompt(self):
        ui = setup.UI(False)
        with self.assertRaises(setup.SetupError):
            ui.ask("question")
        self.assertTrue(ui.yesno("q", True))
        self.assertFalse(ui.yesno("q", True, noninteractive=False))


if __name__ == "__main__":
    unittest.main(verbosity=2)
