import unittest
from customcolor.runner import ColorTestRunner

class TestMath(unittest.TestCase):
    def test_pass(self):
        self.assertEqual(2 + 2, 4)

    def test_fail(self):
        self.assertEqual(2 + 2, 5)

    def test_error(self):
        raise ValueError("Unexpected error")

if __name__ == "__main__":
    unittest.main(testRunner=ColorTestRunner(verbosity=2))
