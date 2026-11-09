"""The source SDK archive carries the release tree and the pinned sources, reproducibly."""
from __future__ import annotations

import contextlib
import io
import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path
from typing import Any

import package_sdk as sdk
import publish_tested_release as publish

VERSION: str = "0.2.1"
GIT_ENVIRONMENT: dict[str, str] = {
    "GIT_AUTHOR_NAME": "fixture", "GIT_AUTHOR_EMAIL": "fixture@example.invalid",
    "GIT_COMMITTER_NAME": "fixture", "GIT_COMMITTER_EMAIL": "fixture@example.invalid",
    "GIT_AUTHOR_DATE": "2026-01-01T00:00:00Z", "GIT_COMMITTER_DATE": "2026-01-01T00:00:00Z",
    "GIT_CONFIG_NOSYSTEM": "1",
}


def git(directory: Path, *arguments: str) -> str:
    environment: dict[str, str] = dict(os.environ)
    environment.update(GIT_ENVIRONMENT)
    completed: subprocess.CompletedProcess[str] = subprocess.run(
        ["git", "-C", str(directory), "-c", "core.autocrlf=false", *arguments],
        check=True, capture_output=True, text=True, env=environment)
    return completed.stdout.strip()


def make_repository(directory: Path, files: dict[str, str]) -> str:
    """A Git repository at directory holding files in one commit; the commit."""
    directory.mkdir(parents=True, exist_ok=True)
    git(directory, "init", "-q")
    name: str
    for name in files:
        path: Path = directory / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(files[name].encode("utf-8"))
    git(directory, "add", "-A")
    git(directory, "commit", "-q", "-m", "fixture")
    return git(directory, "rev-parse", "HEAD")


class Fixture:
    """A release tree whose pin files name two fixture upstream repositories.

    stx (stx.lock's commit: the stx packages, stx_skia and the third-party
    sources) and miniaudio are elsewhere, as CI's FetchContent clones are,
    and reached through checkouts.
    """

    def __init__(self, base: Path) -> None:
        self.root: Path = base / "gui_forms"
        self.revisions: dict[str, str] = {
            "stx": make_repository(base / "deps/stx-src", {
                "LICENSE": "MIT\n", "CMakeLists.txt": "# stx\n", "stx_skia/CMakeLists.txt": "# stx_skia\n",
                "third_party/harfbuzz/src/hb.cc": "// hb\n", "third_party/freetype/LICENSE.TXT": "FTL\n",
                "third_party/libunibreak/src/linebreak.c": "/* lb */\n",
                ".github/workflows/stx.yml": "name: stx\n"}),
            "miniaudio": make_repository(base / "deps/miniaudio-src", {
                "LICENSE": "MIT-0\n", "miniaudio.h": "/* ma */\n", "extras/stb_vorbis.c": "/* v */\n"}),
        }
        self.checkouts: dict[str, Path] = {"stx": base / "deps/stx-src", "miniaudio": base / "deps/miniaudio-src"}
        self.revision: str = make_repository(self.root, {
            "CMakeLists.txt": "cmake_minimum_required(VERSION 3.25)\n",
            "tools/build_sdk.py": "# entry point\n",
            "cmake/llvm22.cmake": "# toolchain\n",
            "cmake/Audio.cmake": "FetchContent_Declare(gui_forms_miniaudio\n    GIT_TAG "
                                 + self.revisions["miniaudio"] + "\n)\n",
            "third_party/.gitignore": "/miniaudio/\n",
            "stx.lock": "[stx]\nrepository = \"malkuth-software/stx\"\ncommit = \"" + self.revisions["stx"] + "\"\n",
        })

    def package(self, out: Path) -> Path:
        with contextlib.redirect_stdout(io.StringIO()):
            return sdk.package(self.root, VERSION, self.revision, out, self.checkouts)


class SourceArchiveTests(unittest.TestCase):
    # One fixture and archive for the class: building the Git fixtures is
    # most of the cost, and no test changes them (tampering writes copies).
    directory: tempfile.TemporaryDirectory[str]
    base: Path
    fixture: Fixture
    archive: Path
    top: str

    @classmethod
    def setUpClass(cls) -> None:
        cls.directory = tempfile.TemporaryDirectory()
        cls.base = Path(cls.directory.name)
        cls.fixture = Fixture(cls.base)
        cls.archive = cls.fixture.package(cls.base / "dist")
        cls.top = sdk.sdk_directory_name(VERSION) + "/"

    @classmethod
    def tearDownClass(cls) -> None:
        cls.directory.cleanup()

    def rewrite(self, members: dict[str, sdk.Entry]) -> Path:
        path: Path = self.base / ("tampered-" + self.id().rpartition(".")[2]) / sdk.sdk_archive_name(VERSION)
        path.parent.mkdir(exist_ok=True)
        sdk.write_archive(members, path)
        return path

    def test_names(self) -> None:
        self.assertEqual(sdk.sdk_archive_name(VERSION), "gui-forms-sdk-0.2.1-source.tar.gz")
        self.assertEqual(sdk.sdk_directory_name(VERSION), "gui-forms-sdk-0.2.1")
        self.assertEqual(self.archive.name, "gui-forms-sdk-0.2.1-source.tar.gz")

    def test_the_archive_holds_the_tree_and_each_pinned_source_in_place(self) -> None:
        members: dict[str, sdk.Entry] = sdk.read_archive(self.archive)
        name: str
        for name in ("CMakeLists.txt", "tools/build_sdk.py", "SDK_MANIFEST.json",
                     "stx/LICENSE", "stx/stx_skia/CMakeLists.txt", "stx/third_party/harfbuzz/src/hb.cc",
                     "stx/third_party/freetype/LICENSE.TXT", "stx/third_party/libunibreak/src/linebreak.c",
                     "third_party/miniaudio/miniaudio.h"):
            self.assertIn(self.top + name, members)
        self.assertNotIn(self.top + "stx/.github/workflows/stx.yml", members)
        for name in members:
            self.assertTrue(name.startswith(self.top))
            self.assertNotIn("/.git/", name)

    def test_the_manifest_names_the_entry_point_paths_and_pins(self) -> None:
        record: dict[str, Any] = sdk.check_archive(self.archive, VERSION)
        self.assertEqual(record["build"], "tools/build_sdk.py")
        self.assertEqual(record["top_directory"], "gui-forms-sdk-0.2.1")
        self.assertEqual(record["revision"], self.fixture.revision)
        paths: dict[str, str] = {}
        revisions: dict[str, str] = {}
        source: dict[str, Any]
        for source in record["third_party"]:
            paths[source["name"]] = source["path"]
            revisions[source["name"]] = source["revision"]
        self.assertEqual(paths, {"stx": "stx", "miniaudio": "third_party/miniaudio"})
        self.assertEqual(revisions, self.fixture.revisions)
        network: list[str] = []
        for source in record["network_inputs"]:
            network.append(source["name"])
        # stx (with stx_skia and the third-party sources) is vendored, so nothing else is downloaded.
        self.assertEqual(network, ["llvm-runtimes-source"])
        self.assertTrue(record["prebuilt_inputs"])

    def test_the_archive_is_reproducible(self) -> None:
        again: Path = self.fixture.package(self.base / "again")
        self.assertEqual(self.archive.read_bytes(), again.read_bytes())

    def test_the_manifest_names_the_compiler_cache_only_where_it_is_published(self) -> None:
        record: dict[str, Any] = sdk.check_archive(self.archive, VERSION)
        self.assertIsNone(record["compiler_cache"]["release"])
        with contextlib.redirect_stdout(io.StringIO()):
            published: Path = sdk.package(self.fixture.root, VERSION, self.fixture.revision, self.base / "cached",
                                          self.fixture.checkouts, published_cache=True)
        cached: dict[str, Any] = sdk.check_archive(published, VERSION)["compiler_cache"]
        self.assertEqual(cached["release"], "build-" + self.fixture.revision)
        self.assertEqual(cached["asset"], "gui-forms-cache-{platform}.tar.gz")
        self.assertEqual(sorted(cached["platforms"]), ["linux-arm64", "linux-x64", "macos-arm64", "windows-x64"])
        self.assertEqual((cached["layout"]["source"], cached["layout"]["work"]),
                         ("gui_forms", ".build/native-{platform}"))

    def test_a_compiler_cache_of_another_revision_is_rejected(self) -> None:
        members: dict[str, sdk.Entry] = sdk.read_archive(self.archive)
        name: str = self.top + sdk.MANIFEST_NAME
        record: dict[str, Any] = json.loads((members[name].data or b"").decode("utf-8"))
        record["compiler_cache"]["release"] = "build-" + "0" * 40
        members[name] = sdk.Entry((json.dumps(record) + "\n").encode("utf-8"), None, False)
        with self.assertRaisesRegex(ValueError, "compiler_cache must name build-<revision>"):
            sdk.check_archive(self.rewrite(members), VERSION)

    def test_check_command_reads_an_archive(self) -> None:
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(sdk.main(["--version", VERSION, "--check", str(self.archive)]), 0)

    def test_a_changed_vendored_file_is_rejected(self) -> None:
        members: dict[str, sdk.Entry] = sdk.read_archive(self.archive)
        members[self.top + "stx/CMakeLists.txt"] = sdk.Entry(b"# changed\n", None, False)
        with self.assertRaisesRegex(ValueError, "stx .* differs from its recorded digest"):
            sdk.check_archive(self.rewrite(members), VERSION)

    def test_a_missing_vendored_source_is_rejected(self) -> None:
        members: dict[str, sdk.Entry] = {}
        archived: dict[str, sdk.Entry] = sdk.read_archive(self.archive)
        name: str
        for name in archived:
            if not name.startswith(self.top + "stx/"):
                members[name] = archived[name]
        with self.assertRaisesRegex(ValueError, "stx is absent"):
            sdk.check_archive(self.rewrite(members), VERSION)

    def test_missing_cmakelists_is_rejected(self) -> None:
        members: dict[str, sdk.Entry] = sdk.read_archive(self.archive)
        del members[self.top + "CMakeLists.txt"]
        with self.assertRaisesRegex(ValueError, "lacks CMakeLists.txt"):
            sdk.check_archive(self.rewrite(members), VERSION)

    def test_git_directories_and_build_output_are_rejected(self) -> None:
        name: str
        for name in (".git/HEAD", "stx/.git/config", ".build/native/build.ninja",
                     "src/core/object.o", "CMakeCache.txt"):
            with self.subTest(name=name):
                members: dict[str, sdk.Entry] = sdk.read_archive(self.archive)
                members[self.top + name] = sdk.Entry(b"x", None, False)
                with self.assertRaisesRegex(ValueError, "Git directory or build output"):
                    sdk.check_archive(self.rewrite(members), VERSION)

    def test_a_member_outside_the_top_directory_is_rejected(self) -> None:
        members: dict[str, sdk.Entry] = sdk.read_archive(self.archive)
        members["stray.txt"] = sdk.Entry(b"x", None, False)
        with self.assertRaisesRegex(ValueError, "outside the top directory"):
            sdk.check_archive(self.rewrite(members), VERSION)

    def test_another_version_is_rejected(self) -> None:
        with self.assertRaisesRegex(ValueError, "is not named"):
            sdk.check_archive(self.archive, "0.2.2")

    def test_a_checkout_without_the_pin_is_refused(self) -> None:
        other: Path = self.base / "other-miniaudio"
        make_repository(other, {"LICENSE": "other\n"})
        with self.assertRaisesRegex(ValueError, "does not hold miniaudio's pin"):
            sdk.package(self.fixture.root, VERSION, self.fixture.revision, self.base / "refused",
                        {"stx": self.fixture.checkouts["stx"], "miniaudio": other})

    def test_a_missing_pin_is_fetched_from_its_upstream(self) -> None:
        upstream: Path = self.fixture.checkouts["miniaudio"]
        source: sdk.VendoredSource = sdk.VendoredSource(
            "miniaudio", "third_party/miniaudio", upstream.as_uri(), "cmake/Audio.cmake", "", "LICENSE", ())
        work: Path = self.base / "fetch-work"
        work.mkdir(exist_ok=True)
        fetched: Path = sdk.fetch_pin(source, self.fixture.revisions["miniaudio"], work)
        self.assertTrue(sdk.has_commit(fetched, self.fixture.revisions["miniaudio"]))
        self.assertEqual(sdk.tree_digest(sdk.git_tree(fetched, self.fixture.revisions["miniaudio"])),
                         sdk.tree_digest(sdk.git_tree(upstream, self.fixture.revisions["miniaudio"])))

    def test_checkout_arguments_name_vendored_sources(self) -> None:
        self.assertEqual(sdk.parse_checkouts(["miniaudio=/tmp/m"]), {"miniaudio": Path("/tmp/m")})
        with self.assertRaises(ValueError):
            sdk.parse_checkouts(["skia=/tmp/s"])


class PinTests(unittest.TestCase):
    def test_every_vendored_pin_is_read_from_the_real_tree(self) -> None:
        source: sdk.VendoredSource
        for source in sdk.VENDORED:
            with self.subTest(source=source.name):
                self.assertRegex(sdk.pinned_revision(sdk.ROOT, source), r"^[0-9a-f]{40}$")

    def test_the_real_network_inputs_name_their_pins(self) -> None:
        inputs: list[dict[str, Any]] = sdk.network_inputs(sdk.ROOT)
        self.assertEqual(inputs[0]["name"], "llvm-runtimes-source")
        self.assertRegex(inputs[0]["sha256"], r"^[0-9a-f]{64}$")


class ReleaseTests(unittest.TestCase):
    def test_the_release_expects_one_source_archive(self) -> None:
        self.assertEqual(publish.expected_sdk_archive(VERSION), "gui-forms-sdk-0.2.1-source.tar.gz")

    def test_the_release_notes_name_the_source_sdk(self) -> None:
        program: dict[str, Any] = {"id": "gallery", "display_name": "GUI.Forms Gallery", "status": "packaged",
                                   "artifacts": {"archive": "gallery.zip"}}
        notes: str = publish.release_notes(VERSION, "0" * 40, "malkuth-software/gui_forms",
                                           {"windows-x64": {"programs": [program]}}, [])
        self.assertIn("## SDK", notes)
        self.assertIn("`gui-forms-sdk-0.2.1-source.tar.gz`", notes)
        self.assertIn("tools/build_sdk.py --target", notes)


if __name__ == "__main__":
    unittest.main()
