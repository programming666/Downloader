"""
Anti-Range 测试服务器：
接受 Range 头，**忽略实际字节范围**，始终返回 206 + Content-Range: bytes 0-end/total。
模拟"anti-Range"服务器（实际是 Range 不可信），复现多 worker 协调的真实场景。
"""
from http.server import HTTPServer, BaseHTTPRequestHandler
import os
import sys

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 18080
FILE = sys.argv[2] if len(sys.argv) > 2 else r"D:/QT6Project/Downloader/test_anti_range.dat"
SIZE = int(sys.argv[3]) if len(sys.argv) > 3 else 1024 * 1024  # 1MB 默认

# 自动准备测试文件
if not os.path.exists(FILE):
    os.makedirs(os.path.dirname(FILE), exist_ok=True)
    with open(FILE, "wb") as f:
        f.write(os.urandom(SIZE))
    print(f"[setup] created test file {FILE} ({SIZE} bytes)")

class AntiRangeHandler(BaseHTTPRequestHandler):
    def do_HEAD(self):
        file_size = os.path.getsize(FILE)
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Content-Length", str(file_size))
        self.end_headers()

    def do_GET(self):
        self.send_response(206)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Accept-Ranges", "bytes")
        # 关键：始终从 0 起，假装"接受了 Range"
        file_size = os.path.getsize(FILE)
        self.send_header("Content-Range", f"bytes 0-{file_size - 1}/{file_size}")
        self.send_header("Content-Length", str(file_size))
        self.end_headers()
        # 整文件吐给客户端
        with open(FILE, "rb") as f:
            while True:
                chunk = f.read(64 * 1024)
                if not chunk:
                    break
                try:
                    self.wfile.write(chunk)
                except BrokenPipeError:
                    return
    def log_message(self, fmt, *args):
        print(f"[server] {self.address_string()} - {fmt % args}")

if __name__ == "__main__":
    server = HTTPServer(("127.0.0.1", PORT), AntiRangeHandler)
    print(f"[server] anti-Range server listening on http://127.0.0.1:{PORT}/")
    print(f"[server] file: {FILE} ({os.path.getsize(FILE)} bytes)")
    print(f"[server] GET / -> 206 + Content-Range: bytes 0-{os.path.getsize(FILE) - 1}/{os.path.getsize(FILE)}")
    print(f"[server] Ctrl-C to stop")
    server.serve_forever()
