// 下载器插件后台脚本
// 负责拦截浏览器下载并发送到本地应用

// 配置
const DEFAULT_PORT = 8080;
const LOCAL_SERVER_URL = 'http://localhost';

// 端口配置管理
let currentPort = DEFAULT_PORT;

// 从存储中加载端口配置
chrome.storage.local.get(['port'], (result) => {
    if (result.port) {
        currentPort = result.port;
    }
    console.log('Background script loaded with port:', currentPort);
});

// 监听端口配置变化
chrome.storage.onChanged.addListener((changes, namespace) => {
    if (namespace === 'local' && changes.port) {
        currentPort = changes.port.newValue;
        console.log('Port updated to:', currentPort);
    }
});

// 用于跟踪已处理的下载项，避免重复处理
const processedDownloads = new Set();

// 清理已完成的下载记录
function cleanupProcessedDownloads(downloadId) {
    setTimeout(() => {
        processedDownloads.delete(downloadId);
        console.log('Cleaned up download record:', downloadId);
    }, 60000); // 1分钟后清理
}

// 发送下载请求到本地应用
async function sendDownloadToLocalApp(downloadItem, suggest) {
    const downloadId = downloadItem.id;
    
    // 检查是否已经处理过这个下载
    if (processedDownloads.has(downloadId)) {
        console.log('Download already processed, skipping:', downloadId);
        return;
    }
    
    // 标记为已处理
    processedDownloads.add(downloadId);
    
    try {
        console.log('Processing download:', downloadItem.url);
        console.log('Filename:', downloadItem.filename);
        console.log('File size:', downloadItem.totalBytes);
        
        // 构造请求数据 - 不发送保存路径，让下载器使用默认路径
        const requestData = {
            url: downloadItem.url,
            filename: downloadItem.filename || '',
            fileSize: downloadItem.totalBytes || 0,
            mimeType: downloadItem.mime || 'application/octet-stream'
        };
        
        // 发送到本地服务器
        const response = await fetch(`${LOCAL_SERVER_URL}:${currentPort}/download`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
            },
            body: JSON.stringify(requestData)
        });
        
        if (response.ok) {
            console.log('Download request sent successfully');
            
            // 立即暂停下载（阻止浏览器默认下载）
            try {
                await chrome.downloads.pause(downloadId);
                await chrome.downloads.cancel(downloadId);
                console.log('Browser download cancelled successfully');
            } catch (error) {
                console.log('Error cancelling browser download:', error);
            }
            
            // 显示通知
            chrome.notifications.create({
                type: 'basic',
                iconUrl: 'icon.png',
                title: '下载已拦截',
                message: `文件 "${downloadItem.filename}" 已发送到下载器`
            });
            
            // 清理记录
            cleanupProcessedDownloads(downloadId);
            
        } else {
            console.error('Failed to send download request:', response.status, response.statusText);
            // 如果发送失败，允许浏览器继续下载
            processedDownloads.delete(downloadId);
        }
        
    } catch (error) {
        console.error('Error sending download request:', error);
        // 如果发送失败，允许浏览器继续下载
        processedDownloads.delete(downloadId);
    }
}

// 监听下载创建事件（更早的拦截点）
chrome.downloads.onCreated.addListener((downloadItem) => {
    console.log('Download created:', downloadItem.url);
    
    // 立即暂停下载
    chrome.downloads.pause(downloadItem.id).catch(error => {
        console.log('Failed to pause download:', error);
    });
    
    // 发送到本地应用
    sendDownloadToLocalApp(downloadItem);
});

// 监听下载状态变化事件
chrome.downloads.onChanged.addListener((downloadDelta) => {
    // 如果下载被暂停，检查是否需要恢复或取消
    if (downloadDelta.state && downloadDelta.state.current === 'interrupted') {
        console.log('Download interrupted:', downloadDelta.id);
        
        // 如果这是我们处理的下载，保持暂停状态
        if (processedDownloads.has(downloadDelta.id)) {
            chrome.downloads.pause(downloadDelta.id).catch(error => {
                console.log('Failed to keep download paused:', error);
            });
        }
    }
});

// 监听来自popup或content script的消息
chrome.runtime.onMessage.addListener((request, sender, sendResponse) => {
    console.log('Received message:', request);
    
    if (request.action === 'testConnection') {
        // 测试与本地服务器的连接
        testConnection()
            .then(result => sendResponse(result))
            .catch(error => sendResponse({ success: false, error: error.message }));
        return true; // 保持消息通道开放以进行异步响应
    }
    
    if (request.action === 'getStatus') {
        sendResponse({
            port: currentPort,
            processedCount: processedDownloads.size
        });
    }
});

// 测试与本地服务器的连接
async function testConnection() {
    try {
        const response = await fetch(`${LOCAL_SERVER_URL}:${currentPort}/status`, {
            method: 'GET',
            timeout: 5000
        });
        
        if (response.ok) {
            return { success: true, message: '连接成功' };
        } else {
            return { success: false, error: `HTTP ${response.status}` };
        }
    } catch (error) {
        return { success: false, error: error.message };
    }
}

console.log('Background script initialized');