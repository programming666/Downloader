#!/usr/bin/env python3
"""
测试脚本：模拟浏览器插件发送下载请求，检查是否有重复响应

用法：
    python test_local_server.py [port]

默认端口 8080，可以通过命令行参数或环境变量 DOWNLOADER_PORT 覆盖。
"""
import sys
import os
import time
import json

# `requests` 是测试唯一外部依赖；用 try/except 包住，给出友好报错。
try:
    import requests
except ImportError:
    sys.stderr.write(
        "缺少依赖 requests。请运行：\n"
        "    pip install requests\n"
    )
    sys.exit(1)

DEFAULT_PORT = 8080


def get_port():
    """解析端口：CLI 参数 > 环境变量 > 默认值"""
    if len(sys.argv) > 1 and sys.argv[1]:
        try:
            return int(sys.argv[1])
        except ValueError:
            print(f"无效的端口参数: {sys.argv[1]}，使用默认 {DEFAULT_PORT}")
    env_port = os.environ.get("DOWNLOADER_PORT")
    if env_port:
        try:
            return int(env_port)
        except ValueError:
            print(f"无效的环境变量 DOWNLOADER_PORT={env_port}，使用默认 {DEFAULT_PORT}")
    return DEFAULT_PORT


PORT = get_port()


def test_download_request():
    """测试发送下载请求。
    返回 True 表示响应成功且 body 中的 status 字段为 success。"""
    url = f"http://localhost:{PORT}/download"
    data = {
        "url": "https://example.com/test-file.zip",
        "filename": "test-file.zip"
    }

    try:
        print(f"发送下载请求到 {url}")
        print(f"请求数据: {json.dumps(data, indent=2, ensure_ascii=False)}")

        # 使用 tuple timeout：(connect, read)；之前 5 既作 connect 又作 read，
        # 长尾网络下读超时阈值过短，会让测试误报失败。
        response = requests.post(url, json=data, timeout=(3, 10))

        print(f"响应状态码: {response.status_code}")
        print(f"响应内容: {response.text}")

        if response.status_code == 200:
            # 解析 body 中的 status 字段，确保真的是 "success"。
            try:
                body = response.json()
                server_status = body.get("status")
            except ValueError:
                server_status = None

            if server_status == "success":
                print("✅ 请求成功发送")
                return True
            else:
                print(f"❌ 服务器响应体 status 字段不为 success: {server_status!r}")
                return False
        else:
            print(f"❌ 请求失败: {response.status_code}")
            return False

    except requests.exceptions.ConnectionError:
        print(f"❌ 无法连接到服务器，请确保下载器正在运行并监听 {PORT} 端口")
        return False
    except requests.exceptions.Timeout:
        print("❌ 请求超时")
        return False
    except Exception as e:
        print(f"❌ 发生错误: {e}")
        return False


def test_status():
    """测试 /status 端点"""
    url = f"http://localhost:{PORT}/status"
    print(f"\n测试健康检查 {url}")
    try:
        # tuple timeout，避免把读超时当连接超时。
        r = requests.get(url, timeout=(3, 10))
        print(f"状态码: {r.status_code}, 响应: {r.text}")
        return r.status_code == 200
    except Exception as e:
        print(f"❌ /status 失败: {e}")
        return False


def test_multiple_requests():
    """测试发送多个请求，检查是否有重复响应"""
    print("\n" + "=" * 50)
    print("测试发送多个请求...")

    success_count = 0
    for i in range(3):
        print(f"\n第 {i + 1} 次请求:")
        if test_download_request():
            success_count += 1
        time.sleep(1)  # 等待1秒

    print(f"\n成功发送 {success_count}/3 个请求")
    return success_count


if __name__ == "__main__":
    print("测试下载器重复下载问题")
    print("=" * 50)
    print(f"使用端口: {PORT}")

    # 首先测试单个请求：使用返回值做 assert，让脚本在 CI 环境下能 fail 出来。
    first_ok = test_download_request()
    if not first_ok:
        print("第一次请求失败，跳过剩余测试")
        sys.exit(2)

    # 健康检查
    test_status()

    # 然后测试多个请求
    test_multiple_requests()

    print("\n测试完成。请检查下载器界面是否显示了重复的下载任务。")
