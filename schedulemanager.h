#ifndef SCHEDULEMANAGER_H
#define SCHEDULEMANAGER_H

#include <QObject>
#include <QTimer>
#include <QDateTime>
#include <QMap>
#include <QJsonObject>

class DownloadTask;

/**
 * @brief 定时任务项
 */
struct ScheduledTask {
    int id;                    ///< 任务ID
    QString fileName;          ///< 文件名
    QString url;              ///< 下载URL
    QString savePath;         ///< 保存路径
    QDateTime scheduledTime;  ///< 计划时间
    bool isRepeat;            ///< 是否重复
    int repeatInterval;       ///< 重复间隔（小时）
    bool isActive;            ///< 是否激活
    QString type;             ///< 任务类型标识，用于区分定时下载任务
    
    /**
     * @brief 转换为JSON对象
     * @return QJsonObject JSON对象
     */
    QJsonObject toJson() const;
    
    /**
     * @brief 从JSON对象创建
     * @param json JSON对象
     * @return ScheduledTask 定时任务
     */
    static ScheduledTask fromJson(const QJsonObject& json);
};

/**
 * @brief 定时任务管理器
 * 
 * 负责管理所有定时下载任务，包括：
 * 1. 添加/删除定时任务
 * 2. 定时检查任务执行时间
 * 3. 持久化存储定时任务
 * 4. 重复任务处理
 */
class ScheduleManager : public QObject
{
    Q_OBJECT

public:
    static ScheduleManager* instance();
    
    /**
     * @brief 添加定时任务
     * @param task 定时任务
     * @return int 任务ID
     */
    int addScheduledTask(const ScheduledTask& task);
    
    /**
     * @brief 删除定时任务
     * @param taskId 任务ID
     */
    void removeScheduledTask(int taskId);
    
    /**
     * @brief 获取所有定时任务
     * @return QList<ScheduledTask> 任务列表
     */
    QList<ScheduledTask> getAllScheduledTasks() const;
    
    /**
     * @brief 激活/停用定时任务
     * @param taskId 任务ID
     * @param active 是否激活
     */
    void setTaskActive(int taskId, bool active);
    
    /**
     * @brief 保存定时任务到文件
     */
    void saveScheduledTasks();
    
    /**
     * @brief 从文件加载定时任务
     */
    void loadScheduledTasks();

signals:
    /**
     * @brief 定时任务触发信号
     * @param task 触发的定时任务
     */
    void scheduledTaskTriggered(const ScheduledTask& task);
    
    /**
     * @brief 定时任务列表改变信号
     */
    void scheduledTasksChanged();

private slots:
    /**
     * @brief 定时器超时处理
     */
    void onTimerTimeout();

private:
    explicit ScheduleManager(QObject *parent = nullptr);
    ~ScheduleManager();
    
    static ScheduleManager* m_instance; ///< 单例实例
    QTimer* m_timer;                   ///< 定时器
    QMap<int, ScheduledTask> m_tasks;  ///< 定时任务映射
    int m_nextTaskId;                  ///< 下一个任务ID
    
    /**
     * @brief 检查并执行到期的定时任务
     */
    void checkScheduledTasks();
    
    /**
     * @brief 处理重复任务
     * @param task 定时任务
     */
    void handleRepeatTask(const ScheduledTask& task);
};

#endif // SCHEDULEMANAGER_H