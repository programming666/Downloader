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

    /**
     * @brief 重写 accept，在关闭前验证输入
     */
    void accept() override;

protected:
    /**
     * @brief 接 QEvent::LanguageChange：当前应用翻译器变化时 Qt 会派发该事件；
     * 在这里调用 ui->retranslateUi(this) 让对话框文案跟随语言切换更新。
     */
    void changeEvent(QEvent* event) override;

private:
    Ui::ScheduleDialog *ui; ///< UI指针

    /**
     * @brief 验证当前 UI 输入，返回 true 表示通过
     */
    bool validateSchedule() const;

    /**
     * @brief 简单 cron 表达式校验：5 字段（分 时 日 月 周），每个字段范围合法。
     * 支持 *、数字、a-b、a-b/n、步进（形如 star-slash-n，即 * 后跟 /n）、列表 a,b,c。
     * @return 合法返回 true
     */
    static bool isValidCronExpression(const QString &expr);
};

#endif // SCHEDULEDIALOG_H