#include "historymanager.h"
#include <QCoreApplication>
#include <QDebug>
#include <iostream>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    
    std::cout << "测试历史记录管理器..." << std::endl;
    
    // 获取HistoryManager实例
    HistoryManager &historyManager = HistoryManager::instance();
    
    // 测试获取历史记录
    QList<DownloadRecord> records = historyManager.getHistory();
    std::cout << "找到 " << records.size() << " 条历史记录" << std::endl;
    
    // 打印每条记录
    for (int i = 0; i < records.size(); ++i) {
        const DownloadRecord &record = records[i];
        std::cout << "记录 " << (i+1) << ":" << std::endl;
        std::cout << "  文件名: " << record.fileName.toStdString() << std::endl;
        std::cout << "  URL: " << record.url.toStdString() << std::endl;
        std::cout << "  文件路径: " << record.filePath.toStdString() << std::endl;
        std::cout << "  文件大小: " << record.fileSize << " 字节" << std::endl;
        std::cout << "  状态: " << record.status.toStdString() << std::endl;
        std::cout << "  开始时间: " << record.startTime.toString().toStdString() << std::endl;
        std::cout << "  完成时间: " << record.finishTime.toString().toStdString() << std::endl;
        std::cout << std::endl;
    }
    
    std::cout << "测试完成！" << std::endl;
    return 0;
}