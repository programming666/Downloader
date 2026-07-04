document.addEventListener('DOMContentLoaded', function() {
  const portInput = document.getElementById('port');
  const saveButton = document.getElementById('save');
  const statusDiv = document.getElementById('status');

  // 加载保存的端口号
  chrome.storage.local.get(['downloaderPort'], function(result) {
    if (result.downloaderPort) {
      portInput.value = result.downloaderPort;
    } else {
      portInput.placeholder = '8080'; // 默认端口作为占位符
    }
  });

  // 保存端口号
  saveButton.addEventListener('click', function() {
    let port = portInput.value.trim();

    // 如果没有输入端口，使用默认值8080
    if (!port) {
      port = '8080';
    }

    // 验证端口号：必须为整数（仅 ASCII 数字），且范围 1-65535
    // 之前 isNaN/port < 1 对非数字字符串会进行词法比较（"9" > "100" 之类），
    // 非 ASCII 数字（'８' 等）也会被静默接受。
    if (!isAsciiIntegerString(port)) {
      showStatus('请输入有效的端口号 (1-65535)', 'error');
      return;
    }
    const portNum = Number(port);
    if (!Number.isInteger(portNum) || portNum < 1 || portNum > 65535) {
      showStatus('请输入有效的端口号 (1-65535)', 'error');
      return;
    }

    // 先保存端口（不再强制要求测试成功才能保存），
    // 再异步测试连接并把结果以通知形式告知用户。
    chrome.storage.local.set({downloaderPort: port}, function() {
      if (chrome.runtime.lastError) {
        showStatus(`保存失败: ${chrome.runtime.lastError.message}`, 'error');
        return;
      }
      showStatus(`设置已保存，端口: ${port}，正在测试连接...`, 'info');

      testConnection(port)
        .then(() => {
          showStatus(`设置已保存，端口: ${port}，连接测试成功`, 'success');
          setTimeout(() => {
            statusDiv.style.display = 'none';
          }, 3000);
        })
        .catch((error) => {
          // 即便连接失败，端口也已经保存下来。
          showStatus(`设置已保存，端口: ${port}，但连接测试失败: ${error.message}`, 'error');
        });
    });
  });

  // 测试与Downloader的连接
  function testConnection(port) {
    return new Promise((resolve, reject) => {
      // 向background脚本发送测试连接请求
      chrome.runtime.sendMessage({action: 'testConnection'}, function(response) {
        // 检查 chrome.runtime.lastError：可能来自 background 脚本不存在、
        // 通道关闭、或被另一端忽略 sendResponse；之前的代码只在
        // response.success 为 false 时把它当业务错误处理。
        if (chrome.runtime.lastError) {
          reject(new Error(chrome.runtime.lastError.message || '无法连接到background脚本'));
          return;
        }
        if (!response) {
          reject(new Error('未收到background脚本的响应'));
          return;
        }
        if (response.success) {
          resolve(response.message);
        } else {
          reject(new Error(response.error || response.message || '未知错误'));
        }
      });
    });
  }

  function showStatus(message, type) {
    statusDiv.textContent = message;
    statusDiv.className = 'status ' + type;
    statusDiv.style.display = 'block';
  }

  // 严格 ASCII 整数串：仅允许 0-9，且长度至少 1。
  // 拒绝空串、负号、小数点、指数符号、非 ASCII 数字字符。
  function isAsciiIntegerString(s) {
    return typeof s === 'string' && /^[0-9]+$/.test(s);
  }
});