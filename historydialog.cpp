#include "historydialog.h"
#include "ui_historydialog.h"
#include <QMessageBox>
#include <QMenu>
#include <QDebug>
#include "logger.h"

HistoryDialog::HistoryDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::HistoryDialog),
    m_historyManager(HistoryManager::instance())
{
    ui->setupUi(this);
    
    // 设置表格属性
    ui->tableWidget->setColumnCount(6);
    ui->tableWidget->setHorizontalHeaderLabels({
        tr("文件名"), tr("URL"), tr("文件大小"), tr("完成时间"), tr("状态"), tr("线程数")
    });
    ui->tableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->tableWidget->setAlternatingRowColors(true);
    ui->tableWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    
    // 连接信号槽
    connect(ui->refreshButton, &QPushButton::clicked, this, &HistoryDialog::onRefreshClicked);
    connect(ui->clearButton, &QPushButton::clicked, this, &HistoryDialog::onClearClicked);
    connect(ui->deleteButton, &QPushButton::clicked, this, &HistoryDialog::onDeleteClicked);
    connect(ui->searchLineEdit, &QLineEdit::textChanged, this, &HistoryDialog::onSearchTextChanged);
    connect(ui->tableWidget, &QTableWidget::itemSelectionChanged, this, &HistoryDialog::onItemSelectionChanged);
    connect(ui->tableWidget, &QTableWidget::customContextMenuRequested, this, &HistoryDialog::showContextMenu);
    
    // 初始加载历史记录
    loadHistory();
    
    setWindowTitle(tr("下载历史记录"));
    resize(800, 600);
}

HistoryDialog::~HistoryDialog()
{
    delete ui;
}

void HistoryDialog::loadHistory()
{
    LOGD("开始加载历史记录");
    m_allRecords = m_historyManager.getHistory();
    m_filteredRecords = m_allRecords;
    updateTable();
    LOGD(QString("历史记录加载完成，共 %1 条记录").arg(m_allRecords.size()));
}

void HistoryDialog::updateTable()
{
    ui->tableWidget->setRowCount(m_filteredRecords.size());
    
    for (int i = 0; i < m_filteredRecords.size(); ++i) {
        const DownloadRecord &record = m_filteredRecords[i];
        
        // 文件名
        QTableWidgetItem *fileNameItem = new QTableWidgetItem(record.fileName);
        fileNameItem->setData(Qt::UserRole, i); // 存储原始索引
        ui->tableWidget->setItem(i, 0, fileNameItem);
        
        // URL
        QTableWidgetItem *urlItem = new QTableWidgetItem(record.url);
        urlItem->setToolTip(record.url); // 鼠标悬停显示完整URL
        ui->tableWidget->setItem(i, 1, urlItem);
        
        // 文件大小
        QString sizeText;
        if (record.fileSize >= 1024 * 1024 * 1024) {
            sizeText = QString("%1 GB").arg(record.fileSize / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
        } else if (record.fileSize >= 1024 * 1024) {
            sizeText = QString("%1 MB").arg(record.fileSize / (1024.0 * 1024.0), 0, 'f', 2);
        } else if (record.fileSize >= 1024) {
            sizeText = QString("%1 KB").arg(record.fileSize / 1024.0, 0, 'f', 2);
        } else {
            sizeText = QString("%1 B").arg(record.fileSize);
        }
        ui->tableWidget->setItem(i, 2, new QTableWidgetItem(sizeText));
        
        // 完成时间
        ui->tableWidget->setItem(i, 3, new QTableWidgetItem(record.finishTime.toString("yyyy-MM-dd hh:mm:ss")));
        
        // 状态
        QString statusText;
        if (record.status == "Completed") {
            statusText = tr("已完成");
        } else if (record.status == "Failed") {
            statusText = tr("失败");
        } else if (record.status == "Cancelled") {
            statusText = tr("已取消");
        } else {
            statusText = tr("未知");
        }
        QTableWidgetItem *statusItem = new QTableWidgetItem(statusText);
        if (record.status == "Failed") {
            statusItem->setForeground(Qt::red);
        } else if (record.status == "Completed") {
            statusItem->setForeground(Qt::darkGreen);
        }
        ui->tableWidget->setItem(i, 4, statusItem);
        
        // 线程数 - 使用默认值1，因为DownloadRecord中没有这个字段
        ui->tableWidget->setItem(i, 5, new QTableWidgetItem(QString::number(1)));
    }
    
    // 调整列宽
    ui->tableWidget->resizeColumnsToContents();
    ui->tableWidget->horizontalHeader()->setStretchLastSection(true);
    
    // 更新状态标签
    ui->statusLabel->setText(tr("共 %1 条记录").arg(m_filteredRecords.size()));
}

void HistoryDialog::onRefreshClicked()
{
    LOGD("刷新历史记录");
    loadHistory();
}

void HistoryDialog::onClearClicked()
{
    int ret = QMessageBox::question(this, tr("确认清空"), 
                                   tr("确定要清空所有历史记录吗？此操作不可恢复。"),
                                   QMessageBox::Yes | QMessageBox::No,
                                   QMessageBox::No);
    
    if (ret == QMessageBox::Yes) {
        LOGD("用户确认清空历史记录");
        if (m_historyManager.clearHistory()) {
            LOGD("历史记录清空成功");
            loadHistory();
            QMessageBox::information(this, tr("成功"), tr("历史记录已清空"));
        } else {
            LOGD("历史记录清空失败");
            QMessageBox::warning(this, tr("错误"), tr("清空历史记录失败"));
        }
    }
}

void HistoryDialog::onDeleteClicked()
{
    int currentRow = ui->tableWidget->currentRow();
    if (currentRow < 0 || currentRow >= m_filteredRecords.size()) {
        return;
    }
    
    const DownloadRecord &record = m_filteredRecords[currentRow];
    
    int ret = QMessageBox::question(this, tr("确认删除"), 
                                   tr("确定要删除选中的记录吗？\n文件名：%1").arg(record.fileName),
                                   QMessageBox::Yes | QMessageBox::No,
                                   QMessageBox::No);
    
    if (ret == QMessageBox::Yes) {
        LOGD(QString("用户确认删除记录：%1").arg(record.fileName));
        // 找到在全部记录中的索引
        int originalIndex = -1;
        for (int i = 0; i < m_allRecords.size(); ++i) {
            if (m_allRecords[i].url == record.url && 
                m_allRecords[i].fileName == record.fileName &&
                m_allRecords[i].startTime == record.startTime) {
                originalIndex = i;
                break;
            }
        }
        
        if (originalIndex >= 0 && m_historyManager.deleteRecord(originalIndex)) {
            LOGD("记录删除成功");
            loadHistory();
            QMessageBox::information(this, tr("成功"), tr("记录已删除"));
        } else {
            LOGD("记录删除失败");
            QMessageBox::warning(this, tr("错误"), tr("删除记录失败"));
        }
    }
}

void HistoryDialog::onItemSelectionChanged()
{
    bool hasSelection = ui->tableWidget->currentRow() >= 0;
    ui->deleteButton->setEnabled(hasSelection);
}

void HistoryDialog::onSearchTextChanged(const QString &text)
{
    QString searchText = text.trimmed().toLower();
    
    if (searchText.isEmpty()) {
        m_filteredRecords = m_allRecords;
    } else {
        m_filteredRecords.clear();
        for (const DownloadRecord &record : m_allRecords) {
            if (record.fileName.toLower().contains(searchText) ||
                record.url.toLower().contains(searchText)) {
                m_filteredRecords.append(record);
            }
        }
    }
    
    updateTable();
}

void HistoryDialog::showContextMenu(const QPoint &pos)
{
    QTableWidgetItem *item = ui->tableWidget->itemAt(pos);
    if (!item) {
        return;
    }
    
    QMenu contextMenu(this);
    QAction *openFileAction = contextMenu.addAction(tr("打开文件"));
    QAction *openFolderAction = contextMenu.addAction(tr("打开所在文件夹"));
    QAction *copyUrlAction = contextMenu.addAction(tr("复制URL"));
    QAction *deleteAction = contextMenu.addAction(tr("删除记录"));
    
    QAction *selectedAction = contextMenu.exec(ui->tableWidget->mapToGlobal(pos));
    
    if (selectedAction) {
        int row = item->row();
        if (row >= 0 && row < m_filteredRecords.size()) {
            const DownloadRecord &record = m_filteredRecords[row];
            
            if (selectedAction == openFileAction) {
                // TODO: 实现打开文件功能
                QMessageBox::information(this, tr("提示"), tr("打开文件功能暂未实现"));
            } else if (selectedAction == openFolderAction) {
                // TODO: 实现打开文件夹功能
                QMessageBox::information(this, tr("提示"), tr("打开文件夹功能暂未实现"));
            } else if (selectedAction == copyUrlAction) {
                QApplication::clipboard()->setText(record.url);
                QMessageBox::information(this, tr("成功"), tr("URL已复制到剪贴板"));
            } else if (selectedAction == deleteAction) {
                onDeleteClicked();
            }
        }
    }
}