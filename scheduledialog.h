#ifndef SCHEDULEDIALOG_H
#define SCHEDULEDIALOG_H

#include <QDialog>
#include <QDateTime>

namespace Ui {
class ScheduleDialog;
}

/**
 * @brief 定时下载设置对话框
 * 
 * 提供定时下载功能的设置界面，支持：
 * 1. 立即开始
 * 2. 指定时间开始
 * 3. 延迟开始
 * 4. 重复下载设置
 */
class ScheduleDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @brief 定时类型枚举
     */
    enum ScheduleType {
        Immediate,    ///< 立即开始
        SpecificTime, ///< 指定时间
        Delayed       ///< 延迟开始
    };

    explicit ScheduleDialog(QWidget *parent = nullptr);
    ~ScheduleDialog();

    /**
     * @brief 设置任务信息
     * @param fileName 文件名
     * @param url 下载URL
     */
    void setTaskInfo(const QString& fileName, const QString& url);

    /**
     * @brief 获取定时类型
     * @return ScheduleType 定时类型
     */
    ScheduleType scheduleType() const;

    /**
     * @brief 获取开始时间
     * @return QDateTime 开始时间
     */
    QDateTime startTime() const;

    /**
     * @brief 获取延迟时间（分钟）
     * @return int 延迟时间（分钟）
     */
    int delayMinutes() const;

    /**
     * @brief 是否重复下载
     * @return bool 是否重复
     */
    bool isRepeat() const;

    /**
     * @brief 获取重复间隔（小时）
     * @return int 重复间隔（小时）
     */
    int repeatInterval() const;

    /**
     * @brief 获取下载URL
     * @return QString URL字符串
     */
    QString url() const;

    /**
     * @brief 获取保存路径
     * @return QString 保存路径
     */
    QString savePath() const;

private slots:
    /**
     * @brief 定时类型改变时的处理
     * @param index 选择的索引
     */
    void onScheduleTypeChanged(int index);

    /**
     * @brief 重复设置改变时的处理
     * @param checked 是否选中
     */
    void onRepeatChanged(bool checked);

    /**
     * @brief 处理浏览按钮点击事件
     */
    void onBrowseButtonClicked();

private:
    Ui::ScheduleDialog *ui; ///< UI指针
};

#endif // SCHEDULEDIALOG_H