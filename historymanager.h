#ifndef HISTORYMANAGER_H
#define HISTORYMANAGER_H

#include <QObject>
#include <QDateTime>
#include <QDebug>
#include <QStandardPaths>
#include <QDir>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>

/**
 * @brief DownloadRecord结构体用于存储单个下载任务的历史记录信息。
 */
struct DownloadRecord {
    QString url;            ///< 下载文件的URL
    QString filePath;       ///< 文件保存的本地路径
    qint64 fileSize;        ///< 文件总大小（字节）
    QDateTime startTime;    ///< 任务开始时间
    QDateTime finishTime;   ///< 任务完成时间
    QString status;         ///< 任务状态（例如："Completed", "Cancelled", "Failed"）
    QString fileName;       ///< 文件名
    
    /**
     * @brief 转换为JSON对象
     * @return QJsonObject JSON对象
     */
    QJsonObject toJson() const;
    
    /**
     * @brief 从JSON对象创建
     * @param json JSON对象
     * @return DownloadRecord 下载记录
     */
    static DownloadRecord fromJson(const QJsonObject& json);
};

/**
 * @brief HistoryManager类用于管理下载历史记录。
 * 这是一个单例类，负责与JSON文件交互，实现下载记录的添加、查询、删除和清空。
 */
class HistoryManager : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 获取HistoryManager的单例实例。
     * @return HistoryManager的唯一实例。
     */
    static HistoryManager& instance();

    // 禁用拷贝构造函数和赋值运算符
    HistoryManager(const HistoryManager&) = delete;
    HistoryManager& operator=(const HistoryManager&) = delete;

    /**
     * @brief 添加一条下载记录到JSON文件。
     * @param record 要添加的DownloadRecord结构体。
     * @return 如果添加成功则返回true，否则返回false。
     */
    bool addRecord(const DownloadRecord& record);

    /**
     * @brief 获取所有下载历史记录。
     * @return 包含所有DownloadRecord的QList。
     */
    QList<DownloadRecord> getHistory() const;

    /**
     * @brief 删除单条历史记录。
     * @param index 要删除的记录索引
     * @return 如果删除成功则返回true，否则返回false。
     */
    bool deleteRecord(int index);

    /**
     * @brief 清空所有下载历史记录。
     * @return 如果清空成功则返回true，否则返回false。
     */
    bool clearHistory();

private:
    /**
     * @brief 私有构造函数，确保单例模式。
     * 初始化JSON文件路径并加载历史记录。
     * @param parent 父QObject。
     */
    explicit HistoryManager(QObject *parent = nullptr);
    ~HistoryManager();

    QString m_historyFilePath; ///< JSON文件路径
    QList<DownloadRecord> m_records; ///< 内存中的历史记录列表

    /**
     * @brief 初始化JSON文件路径并加载历史记录。
     * @return 如果初始化成功则返回true，否则返回false。
     */
    bool initJsonFile();

    /**
     * @brief 从JSON文件加载历史记录。
     * @return 如果加载成功则返回true，否则返回false。
     */
    bool loadHistory();

    /**
     * @brief 保存历史记录到JSON文件。
     * @return 如果保存成功则返回true，否则返回false。
     */
    bool saveHistory();
};

#endif // HISTORYMANAGER_H
