"""Protect tag selection, upgrade guidance and source-note freshness."""
from pathlib import Path
import runpy
import shutil
import subprocess
import tempfile
import unittest

check = runpy.run_path(str(Path(__file__).with_name("release-notes.py")))["check_notes"]
CURRENT = """## Current source
### Changes
- Use the published enrollment protocol.
### Upgrade impact
- Upgrade the paired relay before enrolling.
"""
TAG = """## v0.4.3
### Changes
- Add older-mail paging.
### Upgrade impact
- Keep the encrypted profile for its automatic migration.
"""


def fixture(root):
    """A one-commit repository holding the script and CHANGELOG.md; returns its git runner."""
    (root / "scripts").mkdir()
    shutil.copyfile(Path(__file__).with_name("release-notes.py"), root / "scripts/release-notes.py")
    (root / "CHANGELOG.md").write_text(CURRENT + TAG, encoding="utf-8")
    def git(*args):
        return subprocess.run(["git", "-c", "user.name=Fixture",
            "-c", "user.email=fixture@example.invalid", "-c", "commit.gpgsign=false",
            *args], cwd=root, text=True, capture_output=True, check=True).stdout.strip()
    git("init", "-b", "main")
    git("add", "scripts", "CHANGELOG.md")
    git("commit", "-m", "release source")
    return git


class ReleaseNotesTests(unittest.TestCase):
    def test_selects_current_or_exact_tag(self):
        self.assertIn("enrollment", check(CURRENT + TAG))
        self.assertIn("paging", check(CURRENT + TAG, tag="v0.4.3"))

    def test_refuses_missing_and_invalid_tags(self):
        for tag in ("v0.4.4", "v0.4.3\n## injected", "../v0.4.3"):
            with self.subTest(tag=tag), self.assertRaises(ValueError):
                check(CURRENT + TAG, tag=tag)

    def test_refuses_stale_source_even_if_history_changes(self):
        with self.assertRaisesRegex(ValueError, "unchanged"):
            check(CURRENT + TAG, CURRENT)
        check(CURRENT.replace("published", "updated published") + TAG, CURRENT)

    def test_refuses_empty_comment_or_link_only_guidance(self):
        for value in ("", "<!-- write notes -->", "[Compare](https://example.invalid/a...b)"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                check(CURRENT.replace("Use the published enrollment protocol.", value))

    def test_publication_output_is_bound_to_the_actual_tag_source(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            git = fixture(root)
            git("tag", "v0.4.3")
            command = ["python3", "scripts/release-notes.py", "--tag", "v0.4.3", "--output"]
            subprocess.run(command + ["notes.md"], cwd=root, check=True, capture_output=True)
            self.assertIn(git("rev-parse", "HEAD"), (root / "notes.md").read_text())
            (root / "other").write_text("a different source")
            git("add", "other")
            git("commit", "-m", "later source")
            refused = subprocess.run(command + ["wrong.md"], cwd=root, capture_output=True, text=True)
            self.assertNotEqual(refused.returncode, 0)
            self.assertIn("does not select this source commit", refused.stderr)
            self.assertFalse((root / "wrong.md").exists())


    def test_only_github_changes_need_no_new_notes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            git = fixture(root)
            command = ["python3", "scripts/release-notes.py", "--base", "HEAD^"]
            (root / ".github").mkdir()
            (root / ".github/ci.yml").write_text("on: push")
            git("add", ".github")
            git("commit", "-m", "ci only")
            subprocess.run(command, cwd=root, check=True, capture_output=True)
            # Out of .github/, then back in: a rename must not hide the shipped side.
            for source, target in ((".github/ci.yml", "shipped.yml"),
                                   ("shipped.yml", ".github/shipped.yml")):
                git("mv", source, target)
                git("commit", "-m", "moves " + source)
                refused = subprocess.run(command, cwd=root, capture_output=True, text=True)
                self.assertIn("unchanged", refused.stderr)


if __name__ == "__main__":
    unittest.main()
