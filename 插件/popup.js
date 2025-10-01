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
    
    // 验证端口号
    if (isNaN(port) || port < 1 || port > 65535) {
      showStatus('请输入有效的端口号 (1-65535)', 'error');
      return;
    }
    
    // 测试连接
    showStatus('正在测试连接...', 'info');
    testConnection(port)
      .then(() => {
        // 保存端口号
        chrome.storage.local.set({downloaderPort: port}, function() {
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
  
  // 测试与Downloader的连接
  function testConnection(port) {
    return new Promise((resolve, reject) => {
      // 向background脚本发送测试连接请求
      chrome.runtime.sendMessage({action: 'testConnection'}, function(response) {
        if (chrome.runtime.lastError) {
          reject(new Error('无法连接到background脚本'));
        } else if (response && response.success) {
          resolve(response.message);
        } else {
          reject(new Error(response ? response.message : '未知错误'));
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