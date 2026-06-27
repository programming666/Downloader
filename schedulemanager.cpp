#include "schedulemanager.h"
#include "downloadtask.h"
#include <QTimer>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QDir>
#include <QDebug>

QJsonObject ScheduledTask::toJson() const
{
    QJsonObject obj;
    obj["id"] = id;
    obj["fileName"] = fileName;
    obj["url"] = url;
    obj["savePath"] = savePath;
    obj["scheduledTime"] = scheduledTime.toString(Qt::ISODate);
    obj["isRepeat"] = isRepeat;
    obj["repeatInterval"] = repeatInterval;
    obj["isActive"] = isActive;
    obj["type"] = type.isEmpty() ? "scheduled_download" : type; // 默认类型为scheduled_download
    return obj;
}

ScheduledTask ScheduledTask::fromJson(const QJsonObject& json)
{
    ScheduledTask task;
    task.id = json["id"].toInt();
    task.fileName = json["fileName"].toString();
    task.url = json["url"].toString();
    task.savePath = json["savePath"].toString();
    task.scheduledTime = QDateTime::fromString(json["scheduledTime"].toString(), Qt::ISODate);
    task.isRepeat = json["isRepeat"].toBool();
    task.repeatInterval = json["repeatInterval"].toInt();
    task.isActive = json["isActive"].toBool();
    task.type = json.contains("type") ? json["type"].toString() : "scheduled_download"; // 兼容旧版本数据
    return task;
}

ScheduleManager::ScheduleManager(QObject *parent) : QObject(parent)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(30000); // 30秒检查一次
    connect(m_timer, &QTimer::timeout, this, &ScheduleManager::onTimerTimeout);
    m_timer->start();
    
    m_nextTaskId = 1;
    
    // 加载保存的定时任务
    loadScheduledTasks();
}

ScheduleManager::~ScheduleManager()
{
    // 停止定时器避免重复触发
    if (m_timer) {
        m_timer->stop();
    }
    saveScheduledTasks();
}

ScheduleManager* ScheduleManager::instance()
{
    // 使用函数内 static 变量实现 Meyer's 单例：线程安全（C++11 起保证），
    // 避免裸 new 导致的内存泄漏与析构顺序问题。
    static ScheduleManager inst;
    return &inst;
}

int ScheduleManager::addScheduledTask(const ScheduledTask& task)
{
    ScheduledTask newTask = task;
    newTask.id = m_nextTaskId++;
    m_tasks[newTask.id] = newTask;
    
    emit scheduledTasksChanged();
    saveScheduledTasks();
    
    return newTask.id;
}

void ScheduleManager::removeScheduledTask(int taskId)
{
    if (m_tasks.contains(taskId)) {
        m_tasks.remove(taskId);
        emit scheduledTasksChanged();
        saveScheduledTasks();
    }
}

QList<ScheduledTask> ScheduleManager::getAllScheduledTasks() const
{
    return m_tasks.values();
}

void ScheduleManager::setTaskActive(int taskId, bool active)
{
    if (m_tasks.contains(taskId)) {
        m_tasks[taskId].isActive = active;
        emit scheduledTasksChanged();
        saveScheduledTasks();
    }
}

void ScheduleManager::saveScheduledTasks()
{
    QJsonArray tasksArray;
    for (const auto& task : m_tasks) {
        tasksArray.append(task.toJson());
    }

    QJsonDocument doc(tasksArray);

    // 使用 AppDataLocation 作为存储根目录，避免相对路径随工作目录漂移
    QString filePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                       + QDir::separator() + "scheduled_tasks.json";
    QDir().mkpath(QFileInfo(filePath).absolutePath());

    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(doc.toJson());
        file.close();
    }
}

void ScheduleManager::loadScheduledTasks()
{
    QString filePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                       + QDir::separator() + "scheduled_tasks.json";
    QDir().mkpath(QFileInfo(filePath).absolutePath());

    QFile file(filePath);
    if (!file.exists()) {
        return;
    }

    if (file.open(QIODevice::ReadOnly)) {
        QByteArray raw = file.readAll();
        file.close();

        QJsonParseError err{};
        QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
        if (err.error != QJsonParseError::NoError || !doc.isArray()) {
            // 解析失败时清空任务并保留原文件待人工排查，避免清空导致数据丢失
            qWarning() << "[ScheduleManager::loadScheduledTasks] JSON parse failed:"
                       << err.errorString();
            m_tasks.clear();
            m_nextTaskId = 1;
            return;
        }

        m_tasks.clear();
        m_nextTaskId = 1;
        QJsonArray array = doc.array();
        for (const auto& value : array) {
            if (value.isObject()) {
                ScheduledTask task = ScheduledTask::fromJson(value.toObject());
                m_tasks[task.id] = task;
                if (task.id >= m_nextTaskId) {
                    m_nextTaskId = task.id + 1;
                }
            }
        }
    }
}

void ScheduleManager::onTimerTimeout()
{
    checkScheduledTasks();
}

void ScheduleManager::checkScheduledTasks()
{
    QDateTime now = QDateTime::currentDateTime();

    for (auto it = m_tasks.begin(); it != m_tasks.end(); ++it) {
        ScheduledTask& task = it.value();

        if (!task.isActive) {
            continue;
        }

        // 检查任务是否到期
        if (task.scheduledTime <= now) {
            // 触发任务
            emit scheduledTaskTriggered(task);

            // 处理重复任务：下一次触发时间由 handleRepeatTask 写入
            if (task.isRepeat) {
                handleRepeatTask(task);
            } else {
                // 非重复任务：触发后直接从列表删除，避免重启后残留无效任务
                int doneId = task.id;
                it = m_tasks.erase(it);
                --it; // erase 返回下一迭代器，先回退避免 ++it 时跳过
                Q_UNUSED(doneId);
            }

            // 保存更改
            saveScheduledTasks();
        }
    }
}

void ScheduleManager::handleRepeatTask(const ScheduledTask& task)
{
    // 更新下一次执行时间：以当前时间为基准，而不是原计划时间。
    // 避免每次重复都基于最初计划时间累加，导致实际触发时间持续向后漂移
    // （例如程序在任务到期一段时间后才被唤醒 / 检查定时器时已经过去较久）。
    ScheduledTask& mutableTask = m_tasks[task.id];
    mutableTask.scheduledTime = QDateTime::currentDateTime().addSecs(task.repeatInterval * 3600);
}