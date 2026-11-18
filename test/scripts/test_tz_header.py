import re
import subprocess
import tempfile
import unittest
from pathlib import Path


class TimezoneHeaderTest(unittest.TestCase):
    def test_ignored_lines_preserve_other_bytes(self):
        converter = Path(__file__).resolve().parents[2] / "cmake/ValhallaBin2Header.cmake"
        original = (b"first line skipped\n\n\n\n\n#\n# comment\r\n\n\r\n"
                    b" # not a comment\n\tcontinuation\r\nvalue;data\n#last")
        expected = b"\r\n # not a comment\n\tcontinuation\r\nvalue;data\n"
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / "source"
            header = Path(tmp) / "header.h"
            source.write_bytes(original)
            subprocess.run(["cmake", "-P", str(converter), str(source), str(header),
                            "--variable-name", "test", "--skip", "1", "--raw",
                            "--strip-tz-ignored-lines"], check=True)
            actual = bytes(int(value, 16) for value in
                           re.findall(r"0x([0-9a-f]{2})", header.read_text()))
            self.assertEqual(actual, expected)


if __name__ == "__main__":
    unittest.main()
