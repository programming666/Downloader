#ifndef HISTORYMANAGER_H
#define HISTORYMANAGER_H

#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>
#include <QDebug>
#include <QStandardPaths>
#include <QDir>

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
};

/**
 * @brief HistoryManager类用于管理下载历史记录。
 * 这是一个单例类，负责与SQLite数据库交互，实现下载记录的添加、查询和清空。
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
     * @brief 添加一条下载记录到数据库。
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
     * @brief 清空所有下载历史记录。
     * @return 如果清空成功则返回true，否则返回false。
     */
    bool clearHistory();

private:
    /**
     * @brief 私有构造函数，确保单例模式。
     * 初始化数据库连接并创建表。
     * @param parent 父QObject。
     */
    explicit HistoryManager(QObject *parent = nullptr);
    ~HistoryManager();

    QSqlDatabase m_db; ///< SQLite数据库实例。

    /**
     * @brief 初始化数据库连接并创建历史记录表。
     * @return 如果初始化成功则返回true，否则返回false。
     */
    bool initDatabase();

    // 数据库表名和字段名常量
    static const QString DB_CONNECTION_NAME;
    static const QString DB_FILE_NAME;
    static const QString TABLE_NAME;
    static const QString COL_URL;
    static const QString COL_FILE_PATH;
    static const QString COL_FILE_SIZE;
    static const QString COL_START_TIME;
    static const QString COL_FINISH_TIME;
    static const QString COL_STATUS;
    static const QString COL_FILE_NAME;
};

#endif // HISTORYMANAGER_H
