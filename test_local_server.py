#!/usr/bin/env python3
"""
测试脚本：模拟浏览器插件发送下载请求，检查是否有重复响应
"""
import requests
import json
import time
import sys

def test_download_request():
    """测试发送下载请求"""
    url = "http://localhost:8077/download"
    data = {
        "url": "https://example.com/test-file.zip",
        "filename": "test-file.zip"
    }
    
    try:
        print(f"发送下载请求到 {url}")
        print(f"请求数据: {json.dumps(data, indent=2)}")
        
        response = requests.post(url, json=data, timeout=5)
        
        print(f"响应状态码: {response.status_code}")
        print(f"响应内容: {response.text}")
        
        if response.status_code == 200:
            print("✅ 请求成功发送")
            return True
        else:
            print(f"❌ 请求失败: {response.status_code}")
            return False
            
    except requests.exceptions.ConnectionError:
        print("❌ 无法连接到服务器，请确保下载器正在运行并监听8080端口")
        return False
    except requests.exceptions.Timeout:
        print("❌ 请求超时")
        return False
    except Exception as e:
        print(f"❌ 发生错误: {e}")
        return False

def test_multiple_requests():
    """测试发送多个请求，检查是否有重复响应"""
    print("\n" + "="*50)
    print("测试发送多个请求...")
    
    success_count = 0
    for i in range(3):
        print(f"\n第 {i+1} 次请求:")
        if test_download_request():
            success_count += 1
        time.sleep(1)  # 等待1秒
    
    print(f"\n成功发送 {success_count}/3 个请求")
    return success_count

if __name__ == "__main__":
    print("测试下载器重复下载问题")
    print("="*50)
    
    # 首先测试单个请求
    test_download_request()
    
    # 然后测试多个请求
    test_multiple_requests()
    
    print("\n测试完成。请检查下载器界面是否显示了重复的下载任务。")