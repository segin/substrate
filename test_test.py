import unittest
from unittest.mock import patch, mock_open
from test import modify_parser

class TestTestPy(unittest.TestCase):
    @patch('builtins.open', new_callable=mock_open, read_data='dummy content')
    def test_modify_parser(self, mock_file):
        modify_parser()
        mock_file.assert_called_with("usr.bin/cc/frontend/parser.c", "r")

if __name__ == '__main__':
    unittest.main()
