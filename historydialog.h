#ifndef HISTORYDIALOG_H
#define HISTORYDIALOG_H

#include <QDialog>
#include <QTableWidgetItem>
#include <QClipboard>
#include <QApplication>
#include <QDate>
#include "historymanager.h"

namespace Ui {
class HistoryDialog;
}

class HistoryDialog : public QDialog
{
    Q_OBJECT

public:
    explicit HistoryDialog(QWidget *parent = nullptr);
    ~HistoryDialog();

private slots:
    void onRefreshClicked();
    void onClearClicked();
    void onDeleteClicked();
    void onItemSelectionChanged();
    void onSearchTextChanged(const QString &text);
    void onDateFilterClicked();
    void onClearDateFilterClicked();
    void onDateFromChanged(const QDate &date);
    void onDateToChanged(const QDate &date);

protected:
    /**
     * @brief 接 QEvent::LanguageChange：当前应用翻译器变化时 Qt 会派发该事件；
     * 调用 ui->retranslateUi(this) 让对话框文案跟随语言切换更新；同时按需
     * 重排行内 cell widget 的 tooltip。
     */
    void changeEvent(QEvent* event) override;

private:
    void loadHistory();
    void updateTable();
    void showContextMenu(const QPoint &pos);
    void applyFilter();
    bool confirmDelete(const DownloadRecord &record);

    Ui::HistoryDialog *ui;
    HistoryManager &m_historyManager;
    QList<DownloadRecord> m_allRecords;
    QList<DownloadRecord> m_filteredRecords;
    bool m_dateFilterActive = false;
};

#endif // HISTORYDIALOG_H