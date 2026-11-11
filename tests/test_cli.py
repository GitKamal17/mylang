"""End-to-end checks: generated C must actually compile and run."""
import pathlib
import subprocess
import tempfile
import unittest

CLI = pathlib.Path(__file__).resolve().parents[1] / 'mylang'

class CLIIntegration(unittest.TestCase):
    def invoke(self, *args, cwd=None):
        return subprocess.run([str(CLI), *map(str, args)], cwd=cwd, capture_output=True, text=True, timeout=20)

    def test_run_build_check_and_filename(self):
        with tempfile.TemporaryDirectory(prefix='vibes test ') as tmp:
            source = pathlib.Path(tmp) / 'hello$(touch pwned).vibes'
            source.write_text('fn add(a, b):\n    return a + b\nprint add(20, 22)\n')
            result = self.invoke('check', source, cwd=tmp)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(list(pathlib.Path(tmp).iterdir()), [source])
            result = self.invoke('run', source, cwd=tmp)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout.strip(), '42')
            result = self.invoke('build', source, cwd=tmp)
            self.assertEqual(result.returncode, 0, result.stderr)
            binary = source.with_suffix('')
            self.assertEqual(subprocess.check_output([str(binary)], text=True).strip(), '42')
            self.assertFalse((pathlib.Path(tmp) / 'pwned').exists())

    def test_invalid_and_missing_source_fail(self):
        with tempfile.TemporaryDirectory() as tmp:
            source = pathlib.Path(tmp) / 'bad.vibes'
            source.write_text('if :\n')
            self.assertNotEqual(self.invoke('check', source).returncode, 0)
            self.assertNotEqual(self.invoke('check', pathlib.Path(tmp) / 'missing.vibes').returncode, 0)

    def test_relative_import_from_unrelated_directory(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            (root / 'math.vibes').write_text('fn add(a, b):\n    return a + b\n')
            (root / 'main.vibes').write_text('import "math.vibes"\nprint add(2, 3)\n')
            result = self.invoke('run', root / 'main.vibes', cwd='/tmp')
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout.strip(), '5')

if __name__ == '__main__':
    unittest.main()
