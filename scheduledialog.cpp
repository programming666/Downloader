#include "scheduledialog.h"
#include "ui_scheduledialog.h"
#include "settingsmanager.h"
#include <QDateTime>
#include <QTimer>
#include <QFileDialog>
#include <QDir>
#include <QUrl>
#include <QFileInfo>

ScheduleDialog::ScheduleDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::ScheduleDialog)
{
    ui->setupUi(this);
    
    // 设置当前时间为默认时间
    ui->dateTimeEdit->setDateTime(QDateTime::currentDateTime().addSecs(300)); // 默认5分钟后
    
    // 设置默认保存路径，与新建任务共享相同的默认目录
    QString defaultPath = SettingsManager::instance().loadDefaultDownloadPath();
    ui->lineEditSavePath->setText(defaultPath);
    
    // 连接信号槽
    connect(ui->comboScheduleType, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ScheduleDialog::onScheduleTypeChanged);
    connect(ui->checkRepeat, &QCheckBox::toggled,
            this, &ScheduleDialog::onRepeatChanged);
    connect(ui->btnBrowse, &QPushButton::clicked,
            this, &ScheduleDialog::onBrowseButtonClicked);
    
    // 初始化UI状态
    onScheduleTypeChanged(0);
    onRepeatChanged(false);
}

ScheduleDialog::~ScheduleDialog()
{
    delete ui;
}

void ScheduleDialog::setTaskInfo(const QString& fileName, const QString& url)
{
    // 设置URL输入框的默认值
    ui->lineEditUrl->setText(url);
    
    // 如果提供了文件名，可以设置保存路径的默认文件名
    if (!fileName.isEmpty()) {
        QString defaultPath = QDir::homePath() + "/" + fileName;
        ui->lineEditSavePath->setText(defaultPath);
    }
}

ScheduleDialog::ScheduleType ScheduleDialog::scheduleType() const
{
    return static_cast<ScheduleType>(ui->comboScheduleType->currentIndex());
}

QDateTime ScheduleDialog::startTime() const
{
    return ui->dateTimeEdit->dateTime();
}

int ScheduleDialog::delayMinutes() const
{
    int delay = ui->spinDelay->value();
    if (ui->comboDelayUnit->currentIndex() == 1) { // 小时
        delay *= 60;
    }
    return delay;
}

bool ScheduleDialog::isRepeat() const
{
    return ui->checkRepeat->isChecked();
}

int ScheduleDialog::repeatInterval() const
{
    return ui->spinRepeatInterval->value();
}

void ScheduleDialog::onScheduleTypeChanged(int index)
{
    // 根据定时类型显示/隐藏相关控件
    bool showDateTime = (index == 1); // 指定时间
    bool showDelay = (index == 2);    // 延迟开始
    
    ui->dateTimeEdit->setVisible(showDateTime);
    ui->label_2->setVisible(showDateTime);
    
    ui->spinDelay->setVisible(showDelay);
    ui->comboDelayUnit->setVisible(showDelay);
    ui->label_3->setVisible(showDelay);
}

void ScheduleDialog::onRepeatChanged(bool checked)
{
    // 根据是否重复显示/隐藏重复间隔设置
    ui->spinRepeatInterval->setVisible(checked);
    ui->label_5->setVisible(checked);
    ui->label_6->setVisible(checked);
}

QString ScheduleDialog::url() const
{
    // 从URL输入框获取URL
    return ui->lineEditUrl->text();
}

QString ScheduleDialog::savePath() const
{
    // 从保存路径输入框获取路径
    return ui->lineEditSavePath->text();
}

void ScheduleDialog::onBrowseButtonClicked()
{
    // 获取当前保存路径作为默认目录
    QString currentPath = ui->lineEditSavePath->text();
    QString defaultDir = QDir::homePath();
    
    if (!currentPath.isEmpty()) {
        QFileInfo fileInfo(currentPath);
        if (fileInfo.exists() && fileInfo.isDir()) {
            defaultDir = currentPath;
        } else if (fileInfo.dir().exists()) {
            defaultDir = fileInfo.dir().absolutePath();
        }
    }
    
    // 打开目录选择对话框，让用户选择保存目录
    QString saveDir = QFileDialog::getExistingDirectory(this, 
                                                        tr("选择保存目录"), 
                                                        defaultDir);
    
    if (!saveDir.isEmpty()) {
        // 获取URL中的文件名
        QString url = ui->lineEditUrl->text();
        QString fileName;
        
        if (!url.isEmpty()) {
            // 从URL中提取文件名
            QUrl qurl(url);
            QString path = qurl.path();
            if (!path.isEmpty()) {
                fileName = QFileInfo(path).fileName();
                if (fileName.isEmpty()) {
                    // 如果URL路径中没有文件名，使用默认文件名
                    fileName = "downloaded_file";
                }
            } else {
                // 如果URL无效，使用默认文件名
                fileName = "downloaded_file";
            }
        } else {
            // 如果URL为空，使用默认文件名
            fileName = "downloaded_file";
        }
        
        // 构建完整的保存路径：目录 + 文件名
        QString savePath = saveDir + "/" + fileName;
        ui->lineEditSavePath->setText(savePath);
    }
}