#include "schedulemanager.h"
#include "downloadtask.h"
#include <QTimer>
#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDir>
#include <QDebug>
#include "logger.h"

QJsonObject ScheduledTask::toJson() const
{
    QJsonObject obj;
    obj["id"] = id;
    obj["fileName"] = fileName;
    obj["url"] = url;
    obj["savePath"] = savePath;
    // 始终使用 LocalTime 进行序列化，避免时区漂移
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
    // 解析时间后强制设为 LocalTime，与任务后续比较一致
    QDateTime parsed = QDateTime::fromString(json["scheduledTime"].toString(), Qt::ISODate);
    if (parsed.timeSpec() != Qt::LocalTime) {
        parsed.setTimeSpec(Qt::LocalTime);
    }
    task.scheduledTime = parsed;
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
    saveScheduledTasks();
}

ScheduleManager* ScheduleManager::instance()
{
    // Meyers singleton: 函数局部static，匹配 SettingsManager 的实现风格；
    // 旧版裸指针 m_instance 的生命周期/泄漏问题一并消除。
    static ScheduleManager instance;
    return &instance;
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
    QFile file("scheduled_tasks.json");
    if (file.open(QIODevice::WriteOnly)) {
        file.write(doc.toJson());
        file.close();
    }
}

void ScheduleManager::loadScheduledTasks()
{
    QFile file("scheduled_tasks.json");
    if (!file.exists()) {
        return;
    }
    
    if (file.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();
        
        if (doc.isArray()) {
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
}

void ScheduleManager::onTimerTimeout()
{
    checkScheduledTasks();
}

QDateTime ScheduleManager::computeNextFire(const ScheduledTask& task, const QDateTime& now)
{
    // 复制定理：基准时间必须是当前触发时间（或当前），并且必须显式使用 LocalTime，
    // 避免在跨DST边界时使用 UTC 或 LocalTime 混用造成的 off-by-one。
    if (!task.isRepeat || task.repeatInterval <= 0) {
        return task.scheduledTime;
    }
    // 转换为 LocalTime，确保加秒数后跨DST时由Qt自动按本地日历处理
    QDateTime base = task.scheduledTime;
    if (base.timeSpec() != Qt::LocalTime) {
        base.setTimeSpec(Qt::LocalTime);
    }
    QDateTime candidate = base.addSecs(task.repeatInterval * 3600);
    while (candidate <= now) {
        candidate = candidate.addSecs(task.repeatInterval * 3600);
    }
    return candidate;
}

void ScheduleManager::checkScheduledTasks()
{
    QDateTime now = QDateTime::currentDateTime();

    for (auto it = m_tasks.begin(); it != m_tasks.end(); ++it) {
        ScheduledTask& task = it.value();

        if (!task.isActive) {
            continue;
        }

        // 把当前 scheduledTime 也强制为 LocalTime 以保证比较一致
        if (task.scheduledTime.timeSpec() != Qt::LocalTime) {
            task.scheduledTime.setTimeSpec(Qt::LocalTime);
        }

        // 检查任务是否到期
        if (task.scheduledTime <= now) {
            // 触发任务
            emit scheduledTaskTriggered(task);

            // 处理重复任务
            if (task.isRepeat) {
                handleRepeatTask(task);
            } else {
                // 非重复任务，标记为完成
                task.isActive = false;
            }

            // 保存更改
            saveScheduledTasks();
        }
    }
}

void ScheduleManager::handleRepeatTask(const ScheduledTask& task)
{
    // 重新计算下一次执行时间。每次tick都基于"now"判断是否要跳到更后面，
    // 不假设tick间隔恰好等于 repeatInterval（避免长时间无响应后的延迟累积）。
    QDateTime now = QDateTime::currentDateTime();
    QDateTime nextFire = computeNextFire(task, now);

    ScheduledTask& mutableTask = m_tasks[task.id];
    mutableTask.scheduledTime = nextFire;
}