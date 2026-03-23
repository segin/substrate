import unittest
from unittest.mock import patch, MagicMock
import sys
import os
import io

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '../..')))
from merge_prs import run, run_no_check

class TestMergePrs(unittest.TestCase):
    @patch('sys.stdout', new_callable=io.StringIO)
    @patch('subprocess.run')
    def test_run_success(self, mock_run, mock_stdout):
        mock_result = MagicMock()
        mock_result.returncode = 0
        mock_result.stdout = "hello world\n"
        mock_result.stderr = ""
        mock_run.return_value = mock_result

        output = run("echo 'hello world'")
        self.assertEqual(output, "hello world")
        mock_run.assert_called_once_with("echo 'hello world'", shell=True, text=True, capture_output=True)
        self.assertIn("Running: echo 'hello world'", mock_stdout.getvalue())

    @patch('sys.stdout', new_callable=io.StringIO)
    @patch('subprocess.run')
    def test_run_failure_with_check(self, mock_run, mock_stdout):
        mock_result = MagicMock()
        mock_result.returncode = 1
        mock_result.stdout = ""
        mock_result.stderr = "error"
        mock_run.return_value = mock_result

        with self.assertRaises(SystemExit) as cm:
            run("false")
        self.assertEqual(cm.exception.code, 1)
        self.assertIn("Command failed: false", mock_stdout.getvalue())

    @patch('sys.stdout', new_callable=io.StringIO)
    @patch('subprocess.run')
    def test_run_failure_without_check(self, mock_run, mock_stdout):
        mock_result = MagicMock()
        mock_result.returncode = 1
        mock_result.stdout = "output\n"
        mock_result.stderr = "error\n"
        mock_run.return_value = mock_result

        # This should not raise SystemExit
        output = run("false", check=False)
        self.assertEqual(output, "output")
        self.assertIn("Running: false", mock_stdout.getvalue())

    @patch('sys.stdout', new_callable=io.StringIO)
    @patch('subprocess.run')
    def test_run_no_check_success(self, mock_run, mock_stdout):
        mock_result = MagicMock()
        mock_result.returncode = 0
        mock_result.stdout = "hello\n"
        mock_result.stderr = ""
        mock_run.return_value = mock_result

        code, out, err = run_no_check("echo 'hello'")
        self.assertEqual(code, 0)
        self.assertEqual(out, "hello")
        self.assertEqual(err, "")
        mock_run.assert_called_once_with("echo 'hello'", shell=True, text=True, capture_output=True)
        self.assertIn("Running: echo 'hello'", mock_stdout.getvalue())

    @patch('sys.stdout', new_callable=io.StringIO)
    @patch('subprocess.run')
    def test_run_no_check_failure(self, mock_run, mock_stdout):
        mock_result = MagicMock()
        mock_result.returncode = 1
        mock_result.stdout = "out\n"
        mock_result.stderr = "err\n"
        mock_run.return_value = mock_result

        code, out, err = run_no_check("false")
        self.assertEqual(code, 1)
        self.assertEqual(out, "out")
        self.assertEqual(err, "err")
        self.assertIn("Running: false", mock_stdout.getvalue())

if __name__ == '__main__':
    unittest.main()
