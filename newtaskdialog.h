#ifndef NEWTASKDIALOG_H
#define NEWTASKDIALOG_H

#include <QDialog>
#include <QUrl>

QT_BEGIN_NAMESPACE
namespace Ui { class NewTaskDialog; }
QT_END_NAMESPACE

/**
 * @brief NewTaskDialog类用于显示一个对话框，允许用户输入新的下载任务信息。
 * 包括下载URL、保存路径和下载线程数。
 */
class NewTaskDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数。
     * @param parent 父QWidget。
     */
    explicit NewTaskDialog(QWidget *parent = nullptr);
    ~NewTaskDialog();

    /**
     * @brief 获取用户输入的下载URL。
     * @return URL字符串。
     */
    QString url() const;

    /**
     * @brief 获取用户选择的保存路径。
     * @return 保存路径字符串。
     */
    QString savePath() const;

    /**
     * @brief 获取用户设置的下载线程数。
     * @return 线程数。
     */
    int threadCount() const;

private slots:
    /**
     * @brief 处理"选择保存路径"按钮点击事件。
     */
    void on_browseButton_clicked();
    
    /**
     * @brief 重写accept方法，添加输入验证逻辑。
     */
    void accept() override;

private:
    /**
     * @brief 应用当前主题样式。
     */
    void applyTheme();

    Ui::NewTaskDialog *ui; ///< UI界面指针。
};

#endif // NEWTASKDIALOG_H
