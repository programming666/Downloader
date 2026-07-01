#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QNetworkProxy>

QT_BEGIN_NAMESPACE
namespace Ui { class SettingsDialog; }
QT_END_NAMESPACE

/**
 * @brief SettingsDialog类用于显示应用程序的设置对话框。
 * 允许用户配置网络代理、默认下载路径、默认线程数和界面主题。
 */
class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数。
     * @param parent 父QWidget。
     */
    explicit SettingsDialog(QWidget *parent = nullptr);
    ~SettingsDialog();

private slots:
    /**
     * @brief 处理“选择默认下载路径”按钮点击事件。
     */
    void on_browseButton_clicked();

    /**
     * @brief 处理代理类型选择框的改变事件。
     * @param index 选中的索引。
     */
    void on_proxyTypeComboBox_currentIndexChanged(int index);

    /**
     * @brief 处理“应用”按钮点击事件，保存设置。
     */
    void on_applyButton_clicked();

    /**
     * @brief 处理“取消”按钮点击事件，关闭对话框。
     */
    void on_cancelButton_clicked();

private:
    Ui::SettingsDialog *ui; ///< UI界面指针。

    /**
     * @brief 加载当前设置到UI控件。
     */
    void loadSettingsToUi();

    /**
     * @brief 从UI控件保存设置。
     */
    void saveSettingsFromUi();
};

#endif // SETTINGSDIALOG_H
