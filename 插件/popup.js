document.addEventListener('DOMContentLoaded', function() {
  const portInput = document.getElementById('port');
  const saveButton = document.getElementById('save');
  const statusDiv = document.getElementById('status');

  const STORAGE_KEY_PORT = 'downloaderPort';

  // 严格端口校验：
  // 1) 转 Number；
  // 2) 必须是 Integer（拒绝 NaN / Infinity / 字符串中夹杂字符）；
  // 3) 转回字符串必须等于输入（拒绝本地化数字 / 全角数字 / 前后空格等）；
  // 4) 范围 1..65535。
  function normalizePort(raw) {
    if (typeof raw !== 'string') return null;
    const trimmed = raw.trim();
    if (!trimmed) return null;
    const n = Number(trimmed);
    if (!Number.isInteger(n)) return null;
    if (String(n) !== trimmed) return null; // 防非 ASCII / 全角数字
    if (n < 1 || n > 65535) return null;
    return String(n);
  }

  // 加载保存的端口号
  chrome.storage.local.get([STORAGE_KEY_PORT], function(result) {
    if (chrome.runtime.lastError) {
      console.warn('storage.local.get failed:', chrome.runtime.lastError.message);
    }
    const saved = normalizePort(result && result[STORAGE_KEY_PORT]);
    if (saved) {
      portInput.value = saved;
    } else {
      portInput.placeholder = '8080'; // 默认端口作为占位符
    }
  });

  // 保存端口号
  saveButton.addEventListener('click', function() {
    const port = normalizePort(portInput.value);

    // 验证端口号
    if (!port) {
      showStatus('请输入有效的端口号 (1-65535, 仅 ASCII 数字)', 'error');
      return;
    }

    // 测试连接
    showStatus('正在测试连接...', 'info');
    testConnection(port)
      .then(() => {
        // 保存端口号
        // 检查 chrome.runtime.lastError 防止静默写失败。
        chrome.storage.local.set({ [STORAGE_KEY_PORT]: port }, function() {
          if (chrome.runtime.lastError) {
            showStatus(`保存失败: ${chrome.runtime.lastError.message}`, 'error');
            return;
          }
          showStatus(`设置已保存，端口: ${port}，连接测试成功`, 'success');

          // 隐藏状态消息
          setTimeout(() => {
            statusDiv.style.display = 'none';
          }, 3000);
        });
      })
      .catch((error) => {
        showStatus(`连接测试失败: ${error.message}`, 'error');
      });
  });

  // 测试与 Downloader 的连接
  function testConnection(port) {
    return new Promise((resolve, reject) => {
      // 向 background 脚本发送测试连接请求
      chrome.runtime.sendMessage({action: 'testConnection', port: port}, function(response) {
        if (chrome.runtime.lastError) {
          reject(new Error('无法连接到 background 脚本: ' + chrome.runtime.lastError.message));
        } else if (response && response.success) {
          resolve(response.message);
        } else {
          reject(new Error(response && response.message ? response.message : '未知错误'));
        }
      });
    });
  }

  function showStatus(message, type) {
    statusDiv.textContent = message;
    statusDiv.className = 'status ' + type;
    statusDiv.style.display = 'block';
  }
});
