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

ScheduleManager* ScheduleManager::m_instance = nullptr;

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
    saveScheduledTasks();
}

ScheduleManager* ScheduleManager::instance()
{
    if (!m_instance) {
        m_instance = new ScheduleManager();
    }
    return m_instance;
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
    // 更新下一次执行时间
    ScheduledTask& mutableTask = m_tasks[task.id];
    mutableTask.scheduledTime = task.scheduledTime.addSecs(task.repeatInterval * 3600);
}