#ifndef HISTORYDIALOG_H
#define HISTORYDIALOG_H

#include <QDialog>
#include <QTableWidgetItem>
#include <QClipboard>
#include <QApplication>
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

private:
    void loadHistory();
    void updateTable();
    void showContextMenu(const QPoint &pos);
    
    Ui::HistoryDialog *ui;
    HistoryManager &m_historyManager;
    QList<DownloadRecord> m_allRecords;
    QList<DownloadRecord> m_filteredRecords;
};

#endif // HISTORYDIALOG_H