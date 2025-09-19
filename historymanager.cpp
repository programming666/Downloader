#include "historymanager.h"
#include "logger.h"
#include <QCoreApplication>
#include <QStandardPaths>
#include <QDir>

// 初始化数据库常量
const QString HistoryManager::DB_CONNECTION_NAME = "DownloadHistoryConnection";
const QString HistoryManager::DB_FILE_NAME = "download_history.db";
const QString HistoryManager::TABLE_NAME = "DownloadRecords";
const QString HistoryManager::COL_URL = "Url";
const QString HistoryManager::COL_FILE_PATH = "FilePath";
const QString HistoryManager::COL_FILE_SIZE = "FileSize";
const QString HistoryManager::COL_START_TIME = "StartTime";
const QString HistoryManager::COL_FINISH_TIME = "FinishTime";
const QString HistoryManager::COL_STATUS = "Status";
const QString HistoryManager::COL_FILE_NAME = "FileName";

/**
 * @brief 历史记录管理器构造函数
 * @param parent 父对象指针
 * 
 * 初始化历史记录管理器，创建并初始化SQLite数据库连接
 * 数据库用于存储下载任务的历史记录，包括URL、文件路径、大小、时间等信息
 * 如果数据库初始化失败，会记录错误日志但不中断程序运行
 */
HistoryManager::HistoryManager(QObject *parent)
    : QObject(parent)
{
    LOGD("开始构造HistoryManager");
    LOGD("调用initDatabase()");
    if (!initDatabase()) {
        LOGD("历史记录数据库初始化失败!");
    } else {
        LOGD("历史记录数据库初始化成功");
    }
    LOGD("HistoryManager构造完成");
}

HistoryManager::~HistoryManager()
{
    if (m_db.isOpen()) {
        m_db.close();
        QSqlDatabase::removeDatabase(DB_CONNECTION_NAME);
    }
}

HistoryManager& HistoryManager::instance()
{
    LOGD("获取HistoryManager单例实例");
    static HistoryManager instance;
    LOGD("返回HistoryManager实例");
    return instance;
}

/**
 * @brief 初始化历史记录数据库
 * @return true 初始化成功，false 初始化失败
 * 
 * 创建SQLite数据库文件和表结构，用于存储下载历史记录
 * 数据库文件存储在用户的应用程序数据目录中
 * 表结构包含：URL、文件路径、文件大小、开始时间、结束时间、状态、文件名
 * 
 * 初始化过程：
 * 1. 确保应用程序名称设置正确（用于QStandardPaths）
 * 2. 获取应用程序数据目录路径
 * 3. 创建目录（如不存在）
 * 4. 创建SQLite数据库连接
 * 5. 创建历史记录表（如不存在）
 */
bool HistoryManager::initDatabase()
{
    qDebug() << "[HistoryManager::initDatabase] 开始初始化数据库";
    
    // 确保应用程序名称已设置，以便QStandardPaths正确工作
    qDebug() << "[HistoryManager::initDatabase] 检查应用程序名称设置";
    if (QCoreApplication::applicationName().isEmpty()) {
        qDebug() << "[HistoryManager::initDatabase] 应用程序名称为空，设置为'Downloader'";
        QCoreApplication::setApplicationName("Downloader");
    } else {
        qDebug() << "[HistoryManager::initDatabase] 应用程序名称已设置:" << QCoreApplication::applicationName();
    }
    
    if (QCoreApplication::organizationName().isEmpty()) {
        qDebug() << "[HistoryManager::initDatabase] 组织名称为空，设置为'YourOrganization'";
        QCoreApplication::setOrganizationName("YourOrganization");
    } else {
        qDebug() << "[HistoryManager::initDatabase] 组织名称已设置:" << QCoreApplication::organizationName();
    }

    // 获取应用程序数据目录
    qDebug() << "[HistoryManager::initDatabase] 获取应用程序数据目录";
    QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    qDebug() << "[HistoryManager::initDatabase] 数据目录路径:" << dataPath;
    
    if (dataPath.isEmpty()) {
        qWarning() << "[HistoryManager::initDatabase] 无法找到AppDataLocation，回退到当前目录";
        dataPath = QDir::currentPath();
        qDebug() << "[HistoryManager::initDatabase] 使用当前目录作为数据目录:" << dataPath;
    }

    qDebug() << "[HistoryManager::initDatabase] 检查数据目录是否存在";
    QDir dataDir(dataPath);
    if (!dataDir.exists()) {
        qDebug() << "[HistoryManager::initDatabase] 数据目录不存在，创建目录";
        bool created = dataDir.mkpath("."); // 创建目录
        qDebug() << "[HistoryManager::initDatabase] 目录创建结果:" << (created ? "成功" : "失败");
        if (!created) {
            qCritical() << "[HistoryManager::initDatabase] 无法创建数据目录:" << dataPath;
            return false;
        }
    } else {
        qDebug() << "[HistoryManager::initDatabase] 数据目录已存在";
    }

    QString dbPath = dataPath + QDir::separator() + DB_FILE_NAME;
    qDebug() << "[HistoryManager::initDatabase] 数据库文件完整路径:" << dbPath;

    qDebug() << "[HistoryManager::initDatabase] 添加SQLite数据库连接";
    m_db = QSqlDatabase::addDatabase("QSQLITE", DB_CONNECTION_NAME);
    qDebug() << "[HistoryManager::initDatabase] 设置数据库文件名";
    m_db.setDatabaseName(dbPath);

    qDebug() << "[HistoryManager::initDatabase] 尝试打开数据库连接";
    if (!m_db.open()) {
        qCritical() << "[HistoryManager::initDatabase] 数据库连接打开失败:" << m_db.lastError().text();
        qCritical() << "[HistoryManager::initDatabase] 数据库路径:" << dbPath;
        return false;
    }
    qDebug() << "[HistoryManager::initDatabase] 数据库连接打开成功";

    qDebug() << "[HistoryManager::initDatabase] 创建QSqlQuery对象";
    QSqlQuery query(m_db);
    qDebug() << "[HistoryManager::initDatabase] 构建创建表的SQL语句";
    QString createTableSql = QString(
        "CREATE TABLE IF NOT EXISTS %1 ("
        "Id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "%2 TEXT NOT NULL, "
        "%3 TEXT NOT NULL, "
        "%4 INTEGER NOT NULL, "
        "%5 TEXT NOT NULL, "
        "%6 TEXT NOT NULL, "
        "%7 TEXT NOT NULL, "
        "%8 TEXT NOT NULL"
        ")"
    ).arg(TABLE_NAME, COL_URL, COL_FILE_PATH, COL_FILE_SIZE, COL_START_TIME, COL_FINISH_TIME, COL_STATUS, COL_FILE_NAME);
    
    qDebug() << "[HistoryManager::initDatabase] 创建表SQL:" << createTableSql;

    qDebug() << "[HistoryManager::initDatabase] 执行创建表SQL";
    if (!query.exec(createTableSql)) {
        qCritical() << "[HistoryManager::initDatabase] 创建表失败:" << query.lastError().text();
        qCritical() << "[HistoryManager::initDatabase] 失败的SQL:" << createTableSql;
        return false;
    }
    qDebug() << "[HistoryManager::initDatabase] 表创建成功";

    qDebug() << "[HistoryManager::initDatabase] 数据库初始化完成，路径:" << dbPath;
    return true;
}

/**
 * @brief 添加下载历史记录
 * @param record 下载记录结构体，包含完整的下载信息
 * @return true 添加成功，false 添加失败
 * 
 * 将下载任务的详细信息记录到数据库中，包括：
 * - URL：下载链接
 * - 文件路径：本地保存路径
 * - 文件大小：文件总大小（字节）
 * - 开始时间：下载开始时间
 * - 结束时间：下载完成时间
 * - 状态：下载状态（完成/失败/取消等）
 * - 文件名：下载文件的名称
 * 
 * 使用参数化SQL语句防止SQL注入攻击
 */
bool HistoryManager::addRecord(const DownloadRecord& record)
{
    qDebug() << "[HistoryManager::addRecord] 开始添加历史记录";
    qDebug() << "[HistoryManager::addRecord] 记录详情:";
    qDebug() << "[HistoryManager::addRecord]   URL:" << record.url;
    qDebug() << "[HistoryManager::addRecord]   文件路径:" << record.filePath;
    qDebug() << "[HistoryManager::addRecord]   文件大小:" << record.fileSize;
    qDebug() << "[HistoryManager::addRecord]   开始时间:" << record.startTime.toString();
    qDebug() << "[HistoryManager::addRecord]   结束时间:" << record.finishTime.toString();
    qDebug() << "[HistoryManager::addRecord]   状态:" << record.status;
    qDebug() << "[HistoryManager::addRecord]   文件名:" << record.fileName;
    
    qDebug() << "[HistoryManager::addRecord] 检查数据库连接状态";
    if (!m_db.isOpen()) {
        qCritical() << "[HistoryManager::addRecord] 数据库未打开，无法添加记录";
        return false;
    }
    qDebug() << "[HistoryManager::addRecord] 数据库连接正常";

    qDebug() << "[HistoryManager::addRecord] 创建QSqlQuery对象";
    QSqlQuery query(m_db);
    qDebug() << "[HistoryManager::addRecord] QSqlQuery对象创建完成";
    
    QString sql = QString(
        "INSERT INTO %1 (%2, %3, %4, %5, %6, %7, %8) "
        "VALUES (:url, :filePath, :fileSize, :startTime, :finishTime, :status, :fileName)"
    ).arg(TABLE_NAME, COL_URL, COL_FILE_PATH, COL_FILE_SIZE, COL_START_TIME, COL_FINISH_TIME, COL_STATUS, COL_FILE_NAME);
    qDebug() << "[HistoryManager::addRecord] 准备SQL语句:" << sql;
    
    qDebug() << "[HistoryManager::addRecord] 调用query.prepare()";
    if (!query.prepare(sql)) {
        qCritical() << "[HistoryManager::addRecord] SQL语句准备失败:" << query.lastError().text();
        return false;
    }
    qDebug() << "[HistoryManager::addRecord] SQL语句准备成功";

    qDebug() << "[HistoryManager::addRecord] 开始绑定参数值";
    query.bindValue(":url", record.url);
    qDebug() << "[HistoryManager::addRecord] 绑定URL完成:" << record.url;
    
    query.bindValue(":filePath", record.filePath);
    qDebug() << "[HistoryManager::addRecord] 绑定文件路径完成:" << record.filePath;
    
    query.bindValue(":fileSize", record.fileSize);
    qDebug() << "[HistoryManager::addRecord] 绑定文件大小完成:" << record.fileSize;
    
    QString startTimeStr = record.startTime.toString(Qt::ISODate);
    query.bindValue(":startTime", startTimeStr);
    qDebug() << "[HistoryManager::addRecord] 绑定开始时间完成:" << startTimeStr;
    
    QString finishTimeStr = record.finishTime.toString(Qt::ISODate);
    query.bindValue(":finishTime", finishTimeStr);
    qDebug() << "[HistoryManager::addRecord] 绑定结束时间完成:" << finishTimeStr;
    
    query.bindValue(":status", record.status);
    qDebug() << "[HistoryManager::addRecord] 绑定状态完成:" << record.status;
    
    query.bindValue(":fileName", record.fileName);
    qDebug() << "[HistoryManager::addRecord] 绑定文件名完成:" << record.fileName;
    
    qDebug() << "[HistoryManager::addRecord] 所有参数绑定完成，准备执行SQL";

    qDebug() << "[HistoryManager::addRecord] 调用query.exec()";
    if (!query.exec()) {
        qCritical() << "[HistoryManager::addRecord] SQL执行失败:" << query.lastError().text();
        qCritical() << "[HistoryManager::addRecord] 失败的SQL:" << query.executedQuery();
        return false;
    }
    qDebug() << "[HistoryManager::addRecord] SQL执行成功";
    qDebug() << "[HistoryManager::addRecord] 记录添加成功，URL:" << record.url;
    return true;
}

QList<DownloadRecord> HistoryManager::getHistory() const
{
    QList<DownloadRecord> records;
    if (!m_db.isOpen()) {
        qCritical() << "Database is not open. Cannot retrieve history.";
        return records;
    }

    QSqlQuery query(m_db);
    if (!query.exec(QString("SELECT %1, %2, %3, %4, %5, %6, %7 FROM %8").arg(
        COL_URL, COL_FILE_PATH, COL_FILE_SIZE, COL_START_TIME, COL_FINISH_TIME, COL_STATUS, COL_FILE_NAME, TABLE_NAME))) {
        qCritical() << "Error retrieving history:" << query.lastError().text();
        return records;
    }

    while (query.next()) {
        DownloadRecord record;
        record.url = query.value(COL_URL).toString();
        record.filePath = query.value(COL_FILE_PATH).toString();
        record.fileSize = query.value(COL_FILE_SIZE).toLongLong();
        record.startTime = QDateTime::fromString(query.value(COL_START_TIME).toString(), Qt::ISODate);
        record.finishTime = QDateTime::fromString(query.value(COL_FINISH_TIME).toString(), Qt::ISODate);
        record.status = query.value(COL_STATUS).toString();
        record.fileName = query.value(COL_FILE_NAME).toString();
        records.append(record);
    }
    return records;
}

bool HistoryManager::clearHistory()
{
    if (!m_db.isOpen()) {
        qCritical() << "Database is not open. Cannot clear history.";
        return false;
    }

    QSqlQuery query(m_db);
    if (!query.exec(QString("DELETE FROM %1").arg(TABLE_NAME))) {
        qCritical() << "Error clearing history:" << query.lastError().text();
        return false;
    }
    qDebug() << "Download history cleared successfully.";
    return true;
}
