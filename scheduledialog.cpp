#include "scheduledialog.h"
#include "ui_scheduledialog.h"
#include "settingsmanager.h"
#include <QDateTime>
#include <QTimer>
#include <QFileDialog>
#include <QDir>
#include <QUrl>
#include <QFileInfo>
#include <QMessageBox>
#include <QRegularExpression>
#include <QEvent>

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

    // setupUi 已通过 .ui 的 <connection> 把 buttonBox::accepted 绑定到本类的
    // accept()。我们用重写的 accept() 做校验；不要再 connect 一次以免重复触发。
    // （rejected 同理 —— auto-connection 仍然有效）

    // 初始化UI状态
    onScheduleTypeChanged(0);
    onRepeatChanged(false);
}

ScheduleDialog::~ScheduleDialog()
{
    delete ui;
}

void ScheduleDialog::changeEvent(QEvent* event)
{
    if (event && event->type() == QEvent::LanguageChange) {
        // 重新翻译 .ui 中的所有固定字符串。
        ui->retranslateUi(this);

        // comboScheduleType 的下拉条目在 .ui 里写死中文，retranslateUi 不会改
        // 它们，需要手动按当前索引重写。
        const int typeIdx = ui->comboScheduleType->currentIndex();
        if (ui->comboScheduleType->count() >= 3) {
            ui->comboScheduleType->setItemText(0, tr("立即开始"));
            ui->comboScheduleType->setItemText(1, tr("指定时间"));
            ui->comboScheduleType->setItemText(2, tr("延迟开始"));
            ui->comboScheduleType->setCurrentIndex(typeIdx);
        }

        // 同样的处理：延迟单位下拉（分钟/小时）。
        const int delayIdx = ui->comboDelayUnit->currentIndex();
        if (ui->comboDelayUnit->count() >= 2) {
            ui->comboDelayUnit->setItemText(0, tr("分钟"));
            ui->comboDelayUnit->setItemText(1, tr("小时"));
            ui->comboDelayUnit->setCurrentIndex(delayIdx);
        }

        // 重复间隔的单位标签是 label_6（"小时"），需要重新 setText。
        ui->label_6->setText(tr("小时"));
        // groupBox 标题、groupBox_2 标题在 retranslateUi 会刷新（来自 .ui 中的
        // <property name="title">），但若 .ui 没标 tr（标题里都已经是中文），仍
        // 可能不刷新。这里兜底再写一次。
        ui->groupBox->setTitle(tr("定时设置"));
        ui->groupBox_2->setTitle(tr("任务信息"));
    }
    QDialog::changeEvent(event);
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

bool ScheduleDialog::isValidCronExpression(const QString &expr)
{
    if (expr.trimmed().isEmpty()) {
        return false;
    }
    // 标准 cron：5 个字段，空格分隔
    const QString trimmed = expr.trimmed();
    static const QRegularExpression ws(QStringLiteral("\\s+"));
    const QStringList fields = trimmed.split(ws, Qt::SkipEmptyParts);
    if (fields.size() != 5) {
        return false;
    }
    static const int minValues[5] = {0, 0, 1, 1, 0};
    static const int maxValues[5] = {59, 23, 31, 12, 6};

    for (int i = 0; i < 5; ++i) {
        const QString f = fields[i];
        // 不允许空白字符
        if (f.contains(QChar::fromLatin1(' '))) {
            return false;
        }
        // 允许 * 或 a-b 或 */n 或 a-b/n 或数字 或列表
        // 用宽松正则逐项校验
        static const QRegularExpression fieldRegex(
            QStringLiteral("^\\*(\\/([0-9]+))?$|^([0-9]+)(-([0-9]+))?(\\/([0-9]+))?$|^([0-9]+)(,([0-9]+)(-([0-9]+))?(\\/([0-9]+))?)+$"));
        if (!fieldRegex.match(f).hasMatch()) {
            return false;
        }
        // 解析各数字项并确保在范围内（仅对纯数字/范围做精确校验）
        const QStringList parts = f.split(QLatin1Char(','));
        for (const QString &part : parts) {
            QString body = part;
            int step = 1;
            // */n
            if (body.startsWith(QLatin1String("*/"))) {
                bool ok = false;
                step = body.mid(2).toInt(&ok);
                if (!ok || step <= 0) return false;
                // 范围用 field 范围
                if (step < 1 || step > maxValues[i]) return false;
                continue;
            }
            int slashIdx = body.indexOf(QLatin1Char('/'));
            if (slashIdx > 0) {
                bool ok = false;
                step = body.mid(slashIdx + 1).toInt(&ok);
                if (!ok || step <= 0) return false;
                body = body.left(slashIdx);
            }
            if (body == QLatin1String("*")) {
                continue;
            }
            int dashIdx = body.indexOf(QLatin1Char('-'));
            int lo, hi;
            if (dashIdx > 0) {
                lo = body.left(dashIdx).toInt();
                hi = body.mid(dashIdx + 1).toInt();
            } else {
                lo = body.toInt();
                hi = lo;
            }
            if (lo < minValues[i] || hi > maxValues[i] || lo > hi) {
                return false;
            }
        }
    }
    return true;
}

bool ScheduleDialog::validateSchedule() const
{
    // 1) URL 必填且必须以 http:// 或 https:// 开头
    const QString url = ui->lineEditUrl->text().trimmed();
    if (url.isEmpty()) {
        QMessageBox::warning(const_cast<ScheduleDialog*>(this), tr("Invalid Input"),
                             tr("URL cannot be empty."));
        return false;
    }
    static const QRegularExpression urlRegex(QStringLiteral("^https?://.+"));
    if (!urlRegex.match(url).hasMatch()) {
        QMessageBox::warning(const_cast<ScheduleDialog*>(this), tr("Invalid Input"),
                             tr("URL must start with http:// or https://"));
        return false;
    }
    QUrl qurl(url);
    if (!qurl.isValid() ||
        (qurl.scheme() != QLatin1String("http") && qurl.scheme() != QLatin1String("https"))) {
        QMessageBox::warning(const_cast<ScheduleDialog*>(this), tr("Invalid Input"),
                             tr("URL is invalid or uses an unsupported scheme."));
        return false;
    }

    // 2) 保存路径必填
    const QString savePath = ui->lineEditSavePath->text().trimmed();
    if (savePath.isEmpty()) {
        QMessageBox::warning(const_cast<ScheduleDialog*>(this), tr("Invalid Input"),
                             tr("Save path cannot be empty."));
        return false;
    }

    // 3) 定时类型相关校验
    const int typeIndex = ui->comboScheduleType->currentIndex();
    if (typeIndex == 1) {
        // 指定时间：必须晚于当前时间
        const QDateTime dt = ui->dateTimeEdit->dateTime();
        if (!dt.isValid() || dt <= QDateTime::currentDateTime()) {
            QMessageBox::warning(const_cast<ScheduleDialog*>(this), tr("Invalid Input"),
                                 tr("Scheduled start time must be in the future."));
            return false;
        }
    } else if (typeIndex == 2) {
        // 延迟开始：必须 > 0
        if (ui->spinDelay->value() <= 0) {
            QMessageBox::warning(const_cast<ScheduleDialog*>(this), tr("Invalid Input"),
                                 tr("Delay must be greater than zero."));
            return false;
        }
    }

    // 4) 重复时验证重复间隔
    if (ui->checkRepeat->isChecked()) {
        if (ui->spinRepeatInterval->value() <= 0) {
            QMessageBox::warning(const_cast<ScheduleDialog*>(this), tr("Invalid Input"),
                                 tr("Repeat interval must be greater than zero."));
            return false;
        }
        // 校验：若文本控件未来扩展为 cron 表达式字段，亦可在此调用 isValidCronExpression
        // 当前 UI 中没有 cron 字段，因此保留函数供未来扩展使用。
    }

    return true;
}

void ScheduleDialog::accept()
{
    if (validateSchedule()) {
        QDialog::accept();
    }
}