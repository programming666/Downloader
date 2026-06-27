#include "historymanager.h"
#include "logger.h"
#include <QCoreApplication>
#include <QStandardPaths>
#include <QDir>

// DownloadRecord的JSON转换方法实现
QJsonObject DownloadRecord::toJson() const
{
    QJsonObject obj;
    obj["id"] = id;
    obj["url"] = url;
    obj["filePath"] = filePath;
    obj["fileSize"] = QString::number(fileSize);
    obj["startTime"] = startTime.toString(Qt::ISODate);
    obj["finishTime"] = finishTime.toString(Qt::ISODate);
    obj["status"] = status;
    obj["fileName"] = fileName;
    obj["threadCount"] = threadCount;
    return obj;
}

DownloadRecord DownloadRecord::fromJson(const QJsonObject& json)
{
    DownloadRecord record;
    // 兼容历史数据：若不存在 id 字段，默认 0（将被 loadHistory 重新分配）
    record.id = json.contains("id") ? json["id"].toInt() : 0;
    record.url = json["url"].toString();
    record.filePath = json["filePath"].toString();
    record.fileSize = json["fileSize"].toString().toLongLong();
    record.startTime = QDateTime::fromString(json["startTime"].toString(), Qt::ISODate);
    record.finishTime = QDateTime::fromString(json["finishTime"].toString(), Qt::ISODate);
    record.status = json["status"].toString();
    record.fileName = json["fileName"].toString();
    // 兼容历史数据：旧记录没有 threadCount 字段，默认为 0（显示为"未知"）
    record.threadCount = json.contains("threadCount") ? json["threadCount"].toInt() : 0;

    // 校验关键字段：时间字段在解析失败时给出无效值，避免下游访问异常。
    if (!record.startTime.isValid()) {
        record.startTime = QDateTime();
    }
    if (!record.finishTime.isValid()) {
        record.finishTime = QDateTime();
    }
    return record;
}

/**
 * @brief 历史记录管理器构造函数
 * @param parent 父对象指针
 * 
 * 初始化历史记录管理器，创建并初始化JSON文件存储
 * JSON文件用于存储下载任务的历史记录，包括URL、文件路径、大小、时间等信息
 * 如果JSON文件初始化失败，会记录错误日志但不中断程序运行
 */
HistoryManager::HistoryManager(QObject *parent)
    : QObject(parent)
    , m_nextId(0)
{
    LOGD("开始构造HistoryManager");
    LOGD("调用initJsonFile()");
    if (!initJsonFile()) {
        LOGD("历史记录JSON文件初始化失败!");
    } else {
        LOGD("历史记录JSON文件初始化成功");
    }
    LOGD("HistoryManager构造完成");
}

HistoryManager::~HistoryManager()
{
    // 自动保存历史记录到JSON文件
    saveHistory();
}

HistoryManager& HistoryManager::instance()
{
    LOGD("获取HistoryManager单例实例");
    static HistoryManager instance;
    LOGD("返回HistoryManager实例");
    return instance;
}

/**
 * @brief 初始化JSON文件路径并加载历史记录
 * @return true 初始化成功，false 初始化失败
 * 
 * 创建JSON文件路径并加载历史记录
 * JSON文件存储在用户的APPDATA目录中：%APPDATA%\Programming666\Downloader\history.json
 * 
 * 初始化过程：
 * 1. 确保应用程序名称设置正确
 * 2. 获取APPDATA目录路径
 * 3. 创建目录（如不存在）
 * 4. 设置JSON文件路径
 * 5. 加载历史记录
 */
bool HistoryManager::initJsonFile()
{
    LOGD("[HistoryManager::initJsonFile] 开始初始化JSON文件");
    
    // 确保应用程序名称已设置
    LOGD("[HistoryManager::initJsonFile] 检查应用程序名称设置");
    if (QCoreApplication::applicationName().isEmpty()) {
        LOGD("[HistoryManager::initJsonFile] 应用程序名称为空，设置为'Downloader'");
        QCoreApplication::setApplicationName("Downloader");
    } else {
        LOGD(QString("[HistoryManager::initJsonFile] 应用程序名称已设置:%1").arg(QCoreApplication::applicationName()));
    }
    
    if (QCoreApplication::organizationName().isEmpty()) {
        LOGD("[HistoryManager::initJsonFile] 组织名称为空，设置为'Programming666'");
        QCoreApplication::setOrganizationName("Programming666");
    } else {
        LOGD(QString("[HistoryManager::initJsonFile] 组织名称已设置:%1").arg(QCoreApplication::organizationName()));
    }

    // 获取APPDATA目录
    LOGD("[HistoryManager::initJsonFile] 获取APPDATA目录");
    QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (appDataPath.isEmpty()) {
        LOGD("[HistoryManager::initJsonFile] 无法找到AppDataLocation，回退到当前目录");
        appDataPath = QDir::currentPath();
    }
    
    // QStandardPaths::AppDataLocation 已经返回完整路径：C:\Users\username\AppData\Roaming\Programming666\Downloader
    QString downloaderPath = appDataPath;
    LOGD(QString("[HistoryManager::initJsonFile] Downloader目录路径:%1").arg(downloaderPath));

    LOGD("[HistoryManager::initJsonFile] 检查Downloader目录是否存在");
    QDir downloaderDir(downloaderPath);
    if (!downloaderDir.exists()) {
        LOGD("[HistoryManager::initJsonFile] Downloader目录不存在，创建目录");
        bool created = downloaderDir.mkpath(".");
        LOGD(QString("[HistoryManager::initJsonFile] 目录创建结果:%1").arg(created ? "成功" : "失败"));
        if (!created) {
            LOGD(QString("[HistoryManager::initJsonFile] 无法创建Downloader目录:%1").arg(downloaderPath));
            return false;
        }
    } else {
        LOGD("[HistoryManager::initJsonFile] Downloader目录已存在");
    }

    // 设置JSON文件路径
    m_historyFilePath = downloaderPath + QDir::separator() + "history.json";
    LOGD(QString("[HistoryManager::initJsonFile] JSON文件完整路径:%1").arg(m_historyFilePath));

    // 加载历史记录
    LOGD("[HistoryManager::initJsonFile] 加载历史记录");
    if (!loadHistory()) {
        LOGD("[HistoryManager::initJsonFile] 历史记录加载失败，将使用空列表");
        m_records.clear();
    }

    LOGD("[HistoryManager::initJsonFile] JSON文件初始化完成");
    return true;
}

/**
 * @brief 从JSON文件加载历史记录
 * @return true 加载成功，false 加载失败
 *
 * 如果JSON文件存在但格式错误，会清空文件并返回空列表
 */
bool HistoryManager::loadHistory()
{
    LOGD("[HistoryManager::loadHistory] 开始加载历史记录");

    QMutexLocker locker(&m_mutex);
    return loadHistoryUnlocked();
}

/**
 * @brief 不加锁版的加载历史记录。
 * 调用方必须已经持有 m_mutex。
 */
bool HistoryManager::loadHistoryUnlocked()
{
    LOGD("[HistoryManager::loadHistoryUnlocked] 开始加载历史记录（不加锁）");

    QFile file(m_historyFilePath);
    if (!file.exists()) {
        LOGD("[HistoryManager::loadHistoryUnlocked] JSON文件不存在，将创建新文件");
        m_records.clear();
        m_nextId = 0;
        return true;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        LOGD(QString("[HistoryManager::loadHistoryUnlocked] 无法打开JSON文件:%1").arg(file.errorString()));
        // 无法打开文件时，清空文件内容
        LOGD("[HistoryManager::loadHistoryUnlocked] 清空JSON文件内容");
        file.close();
        m_records.clear();
        m_nextId = 0;
        saveHistoryUnlocked(); // 创建空的JSON文件
        return true;
    }

    QByteArray data = file.readAll();
    file.close();

    // 如果文件为空，返回空列表
    if (data.isEmpty()) {
        LOGD("[HistoryManager::loadHistoryUnlocked] JSON文件为空");
        m_records.clear();
        m_nextId = 0;
        return true;
    }

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull() || !doc.isArray()) {
        LOGD("[HistoryManager::loadHistoryUnlocked] JSON文件格式错误或不是数组，将清空文件");
        // 格式错误时，清空文件并返回空列表
        m_records.clear();
        m_nextId = 0;
        saveHistoryUnlocked(); // 创建空的JSON文件
        return true;
    }

    QJsonArray array = doc.array();
    m_records.clear();
    m_nextId = 0;

    for (const QJsonValue& value : array) {
        if (value.isObject()) {
            try {
                DownloadRecord record = DownloadRecord::fromJson(value.toObject());
                // 兼容老数据：旧记录没有 id 字段，按加载顺序补齐主键
                if (record.id <= 0) {
                    record.id = ++m_nextId;
                } else if (record.id > m_nextId) {
                    m_nextId = record.id;
                }
                m_records.append(record);
            } catch (...) {
                LOGD("[HistoryManager::loadHistoryUnlocked] 解析记录失败，跳过该记录");
                continue;
            }
        }
    }

    LOGD(QString("[HistoryManager::loadHistoryUnlocked] 历史记录加载成功，共%1条记录").arg(m_records.size()));
    return true;
}

/**
 * @brief 保存历史记录到JSON文件
 * @return true 保存成功，false 保存失败
 */
bool HistoryManager::saveHistory()
{
    LOGD("[HistoryManager::saveHistory] 开始保存历史记录");

    QMutexLocker locker(&m_mutex);
    return saveHistoryUnlocked();
}

/**
 * @brief 不加锁版的保存历史记录。
 * 调用方必须已经持有 m_mutex。
 */
bool HistoryManager::saveHistoryUnlocked()
{
    LOGD("[HistoryManager::saveHistoryUnlocked] 开始保存历史记录（不加锁）");

    QJsonArray array;
    for (const DownloadRecord& record : m_records) {
        array.append(record.toJson());
    }

    QJsonDocument doc(array);
    QFile file(m_historyFilePath);

    if (!file.open(QIODevice::WriteOnly)) {
        LOGD(QString("[HistoryManager::saveHistory] 无法打开JSON文件:%1").arg(file.errorString()));
        return false;
    }

    file.write(doc.toJson());
    file.close();

    LOGD(QString("[HistoryManager::saveHistory] 历史记录保存成功，共%1条记录").arg(m_records.size()));
    return true;
}

/**
 * @brief 添加下载历史记录
 * @param record 下载记录结构体，包含完整的下载信息
 * @return true 添加成功，false 添加失败
 * 
 * 将下载任务的详细信息记录到JSON文件中，包括：
 * - URL：下载链接
 * - 文件路径：本地保存路径
 * - 文件大小：文件总大小（字节）
 * - 开始时间：下载开始时间
 * - 结束时间：下载完成时间
 * - 状态：下载状态（完成/失败/取消等）
 * - 文件名：下载文件的名称
 */
bool HistoryManager::addRecord(const DownloadRecord& record)
{
    LOGD("[HistoryManager::addRecord] 开始添加历史记录");
    LOGD(QString("[HistoryManager::addRecord] 记录详情:"));
    LOGD(QString("[HistoryManager::addRecord]   URL:%1").arg(record.url));
    LOGD(QString("[HistoryManager::addRecord]   文件路径:%1").arg(record.filePath));
    LOGD(QString("[HistoryManager::addRecord]   文件大小:%1").arg(record.fileSize));
    LOGD(QString("[HistoryManager::addRecord]   开始时间:%1").arg(record.startTime.toString()));
    LOGD(QString("[HistoryManager::addRecord]   结束时间:%1").arg(record.finishTime.toString()));
    LOGD(QString("[HistoryManager::addRecord]   状态:%1").arg(record.status));
    LOGD(QString("[HistoryManager::addRecord]   文件名:%1").arg(record.fileName));
    LOGD(QString("[HistoryManager::addRecord]   线程数:%1").arg(record.threadCount));

    // 写入磁盘前先复制一份待写入的记录，避免锁外读取被并发修改
    DownloadRecord newRecord = record;
    int oldCount = 0;
    {
        QMutexLocker locker(&m_mutex);
        // 分配主键 id（按调用顺序自增）
        if (newRecord.id <= 0) {
            newRecord.id = ++m_nextId;
        } else if (newRecord.id > m_nextId) {
            m_nextId = newRecord.id;
        }

        oldCount = m_records.size();
        m_records.append(newRecord);

        // 保存到JSON文件（必须用 Unlocked 版本，避免 QMutex 不可重入导致死锁）
        if (!saveHistoryUnlocked()) {
            LOGD("[HistoryManager::addRecord] 保存历史记录失败");
            // 回滚：使用 oldCount 精确回退，并回收主键（仅当回退的是本次插入的 id 时）
            const int extra = m_records.size() - oldCount;
            if (extra > 0) {
                m_records.remove(oldCount, extra);
            }
            if (newRecord.id == m_nextId) {
                --m_nextId;
            }
            return false;
        }
    }

    LOGD(QString("[HistoryManager::addRecord] 记录添加成功，URL:%1").arg(newRecord.url));
    return true;
}

QList<DownloadRecord> HistoryManager::getHistory() const
{
    QMutexLocker locker(&m_mutex);
    LOGD(QString("[HistoryManager::getHistory] 返回历史记录，共%1条记录").arg(m_records.size()));
    return m_records;
}

/**
 * @brief 删除单条历史记录
 * @param index 要删除的记录索引
 * @return true 删除成功，false 删除失败
 */
bool HistoryManager::deleteRecord(int index)
{
    LOGD(QString("[HistoryManager::deleteRecord] 开始删除历史记录，索引:%1").arg(index));

    {
        QMutexLocker locker(&m_mutex);
        if (index < 0 || index >= m_records.size()) {
            LOGD(QString("[HistoryManager::deleteRecord] 索引超出范围:%1").arg(index));
            return false;
        }

        // 先备份待删除的记录，保存失败时回滚
        DownloadRecord removedItem = m_records.at(index);
        m_records.removeAt(index);

        // 保存到JSON文件（必须用 Unlocked 版本，避免 QMutex 不可重入导致死锁）
        if (!saveHistoryUnlocked()) {
            LOGD("[HistoryManager::deleteRecord] 保存历史记录失败，回滚内存");
            // 回滚：将备份项插回原位置
            if (index >= m_records.size()) {
                m_records.append(removedItem);
            } else {
                m_records.insert(index, removedItem);
            }
            return false;
        }
    }

    LOGD("[HistoryManager::deleteRecord] 历史记录删除成功");
    return true;
}

int HistoryManager::findIndexById(int id) const
{
    if (id <= 0) {
        return -1;
    }
    QMutexLocker locker(&m_mutex);
    for (int i = 0; i < m_records.size(); ++i) {
        if (m_records[i].id == id) {
            return i;
        }
    }
    return -1;
}

bool HistoryManager::deleteRecordById(int id)
{
    int index = findIndexById(id);
    if (index < 0) {
        LOGD(QString("[HistoryManager::deleteRecordById] 未找到 id=%1 的记录").arg(id));
        return false;
    }
    return deleteRecord(index);
}

bool HistoryManager::clearHistory()
{
    LOGD("[HistoryManager::clearHistory] 开始清空历史记录");

    {
        QMutexLocker locker(&m_mutex);
        // 清空内存列表
        m_records.clear();
        m_nextId = 0;

        // 保存到JSON文件（必须用 Unlocked 版本，避免 QMutex 不可重入导致死锁）
        if (!saveHistoryUnlocked()) {
            LOGD("[HistoryManager::clearHistory] 保存历史记录失败");
            return false;
        }
    }

    LOGD("[HistoryManager::clearHistory] 历史记录清空成功");
    return true;
}
