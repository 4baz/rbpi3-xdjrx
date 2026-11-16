import http.server
import tempfile
import threading
import time
import unittest
import urllib.request
from pathlib import Path

import server


class BrowseTest(unittest.TestCase):
    def test_existing_counter_and_button_are_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            server.CTRL = str(Path(directory) / "ctrl")
            server.CTRL_SOCK = None
            server._browse_counter = 0
            server._browse_active = False
            Path(server.CTRL).write_text("A 54 12\nB 5d 10\nB 33 80\nB 43 80\n")
            httpd = http.server.ThreadingHTTPServer(("127.0.0.1", 0), server.Handler)
            thread = threading.Thread(target=httpd.serve_forever)
            thread.start()
            try:
                request = urllib.request.Request(
                    "http://127.0.0.1:%d/browse" % httpd.server_port,
                    data=b"1", method="POST")
                with urllib.request.urlopen(request) as response:
                    self.assertEqual(response.status, 200)
                for _ in range(100):
                    snapshot = Path(server.CTRL).read_text()
                    if "A 54 13" in snapshot:
                        break
                    time.sleep(0.01)
                self.assertIn("A 54 13", snapshot)
                self.assertIn("B 5d 10", snapshot)
                self.assertIn("B 33 80", snapshot)
                self.assertIn("B 43 80", snapshot)
            finally:
                httpd.shutdown()
                thread.join()
                httpd.server_close()


if __name__ == "__main__":
    unittest.main()
