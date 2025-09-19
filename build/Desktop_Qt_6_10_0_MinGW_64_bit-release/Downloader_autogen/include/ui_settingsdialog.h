/********************************************************************************
** Form generated from reading UI file 'settingsdialog.ui'
**
** Created by: Qt User Interface Compiler version 6.10.0
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_SETTINGSDIALOG_H
#define UI_SETTINGSDIALOG_H

#include <QtCore/QVariant>
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_SettingsDialog
{
public:
    QVBoxLayout *verticalLayout;
    QTabWidget *tabWidget;
    QWidget *networkTab;
    QFormLayout *formLayout;
    QLabel *proxyTypeLabel;
    QComboBox *proxyTypeComboBox;
    QLabel *proxyHostLabel;
    QLineEdit *proxyHostLineEdit;
    QLabel *proxyPortLabel;
    QSpinBox *proxyPortSpinBox;
    QLabel *proxyUserLabel;
    QLineEdit *proxyUserLineEdit;
    QLabel *proxyPassLabel;
    QLineEdit *proxyPassLineEdit;
    QWidget *downloadTab;
    QFormLayout *formLayout_2;
    QLabel *defaultDownloadPathLabel;
    QHBoxLayout *horizontalLayout;
    QLineEdit *defaultDownloadPathLineEdit;
    QPushButton *browseButton;
    QLabel *defaultThreadsLabel;
    QSpinBox *defaultThreadsSpinBox;
    QWidget *uiTab;
    QFormLayout *formLayout_3;
    QLabel *themeLabel;
    QComboBox *themeComboBox;
    QWidget *localServerTab;
    QFormLayout *formLayout_4;
    QLabel *localListenPortLabel;
    QSpinBox *localListenPortSpinBox;
    QDialogButtonBox *buttonBox;

    void setupUi(QDialog *SettingsDialog)
    {
        if (SettingsDialog->objectName().isEmpty())
            SettingsDialog->setObjectName("SettingsDialog");
        SettingsDialog->resize(500, 400);
        verticalLayout = new QVBoxLayout(SettingsDialog);
        verticalLayout->setObjectName("verticalLayout");
        tabWidget = new QTabWidget(SettingsDialog);
        tabWidget->setObjectName("tabWidget");
        networkTab = new QWidget();
        networkTab->setObjectName("networkTab");
        formLayout = new QFormLayout(networkTab);
        formLayout->setObjectName("formLayout");
        proxyTypeLabel = new QLabel(networkTab);
        proxyTypeLabel->setObjectName("proxyTypeLabel");

        formLayout->setWidget(0, QFormLayout::ItemRole::LabelRole, proxyTypeLabel);

        proxyTypeComboBox = new QComboBox(networkTab);
        proxyTypeComboBox->addItem(QString());
        proxyTypeComboBox->addItem(QString());
        proxyTypeComboBox->addItem(QString());
        proxyTypeComboBox->addItem(QString());
        proxyTypeComboBox->setObjectName("proxyTypeComboBox");

        formLayout->setWidget(0, QFormLayout::ItemRole::FieldRole, proxyTypeComboBox);

        proxyHostLabel = new QLabel(networkTab);
        proxyHostLabel->setObjectName("proxyHostLabel");

        formLayout->setWidget(1, QFormLayout::ItemRole::LabelRole, proxyHostLabel);

        proxyHostLineEdit = new QLineEdit(networkTab);
        proxyHostLineEdit->setObjectName("proxyHostLineEdit");

        formLayout->setWidget(1, QFormLayout::ItemRole::FieldRole, proxyHostLineEdit);

        proxyPortLabel = new QLabel(networkTab);
        proxyPortLabel->setObjectName("proxyPortLabel");

        formLayout->setWidget(2, QFormLayout::ItemRole::LabelRole, proxyPortLabel);

        proxyPortSpinBox = new QSpinBox(networkTab);
        proxyPortSpinBox->setObjectName("proxyPortSpinBox");
        proxyPortSpinBox->setMaximum(65535);

        formLayout->setWidget(2, QFormLayout::ItemRole::FieldRole, proxyPortSpinBox);

        proxyUserLabel = new QLabel(networkTab);
        proxyUserLabel->setObjectName("proxyUserLabel");

        formLayout->setWidget(3, QFormLayout::ItemRole::LabelRole, proxyUserLabel);

        proxyUserLineEdit = new QLineEdit(networkTab);
        proxyUserLineEdit->setObjectName("proxyUserLineEdit");

        formLayout->setWidget(3, QFormLayout::ItemRole::FieldRole, proxyUserLineEdit);

        proxyPassLabel = new QLabel(networkTab);
        proxyPassLabel->setObjectName("proxyPassLabel");

        formLayout->setWidget(4, QFormLayout::ItemRole::LabelRole, proxyPassLabel);

        proxyPassLineEdit = new QLineEdit(networkTab);
        proxyPassLineEdit->setObjectName("proxyPassLineEdit");
        proxyPassLineEdit->setEchoMode(QLineEdit::EchoMode::Password);

        formLayout->setWidget(4, QFormLayout::ItemRole::FieldRole, proxyPassLineEdit);

        tabWidget->addTab(networkTab, QString());
        downloadTab = new QWidget();
        downloadTab->setObjectName("downloadTab");
        formLayout_2 = new QFormLayout(downloadTab);
        formLayout_2->setObjectName("formLayout_2");
        defaultDownloadPathLabel = new QLabel(downloadTab);
        defaultDownloadPathLabel->setObjectName("defaultDownloadPathLabel");

        formLayout_2->setWidget(0, QFormLayout::ItemRole::LabelRole, defaultDownloadPathLabel);

        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setObjectName("horizontalLayout");
        defaultDownloadPathLineEdit = new QLineEdit(downloadTab);
        defaultDownloadPathLineEdit->setObjectName("defaultDownloadPathLineEdit");

        horizontalLayout->addWidget(defaultDownloadPathLineEdit);

        browseButton = new QPushButton(downloadTab);
        browseButton->setObjectName("browseButton");

        horizontalLayout->addWidget(browseButton);


        formLayout_2->setLayout(0, QFormLayout::ItemRole::FieldRole, horizontalLayout);

        defaultThreadsLabel = new QLabel(downloadTab);
        defaultThreadsLabel->setObjectName("defaultThreadsLabel");

        formLayout_2->setWidget(1, QFormLayout::ItemRole::LabelRole, defaultThreadsLabel);

        defaultThreadsSpinBox = new QSpinBox(downloadTab);
        defaultThreadsSpinBox->setObjectName("defaultThreadsSpinBox");
        defaultThreadsSpinBox->setMinimum(1);
        defaultThreadsSpinBox->setMaximum(32);
        defaultThreadsSpinBox->setValue(5);

        formLayout_2->setWidget(1, QFormLayout::ItemRole::FieldRole, defaultThreadsSpinBox);

        tabWidget->addTab(downloadTab, QString());
        uiTab = new QWidget();
        uiTab->setObjectName("uiTab");
        formLayout_3 = new QFormLayout(uiTab);
        formLayout_3->setObjectName("formLayout_3");
        themeLabel = new QLabel(uiTab);
        themeLabel->setObjectName("themeLabel");

        formLayout_3->setWidget(0, QFormLayout::ItemRole::LabelRole, themeLabel);

        themeComboBox = new QComboBox(uiTab);
        themeComboBox->setObjectName("themeComboBox");

        formLayout_3->setWidget(0, QFormLayout::ItemRole::FieldRole, themeComboBox);

        tabWidget->addTab(uiTab, QString());
        localServerTab = new QWidget();
        localServerTab->setObjectName("localServerTab");
        formLayout_4 = new QFormLayout(localServerTab);
        formLayout_4->setObjectName("formLayout_4");
        localListenPortLabel = new QLabel(localServerTab);
        localListenPortLabel->setObjectName("localListenPortLabel");

        formLayout_4->setWidget(0, QFormLayout::ItemRole::LabelRole, localListenPortLabel);

        localListenPortSpinBox = new QSpinBox(localServerTab);
        localListenPortSpinBox->setObjectName("localListenPortSpinBox");
        localListenPortSpinBox->setMinimum(1024);
        localListenPortSpinBox->setMaximum(65535);
        localListenPortSpinBox->setValue(8080);

        formLayout_4->setWidget(0, QFormLayout::ItemRole::FieldRole, localListenPortSpinBox);

        tabWidget->addTab(localServerTab, QString());

        verticalLayout->addWidget(tabWidget);

        buttonBox = new QDialogButtonBox(SettingsDialog);
        buttonBox->setObjectName("buttonBox");
        buttonBox->setOrientation(Qt::Orientation::Horizontal);
        buttonBox->setStandardButtons(QDialogButtonBox::StandardButton::Apply|QDialogButtonBox::StandardButton::Cancel);

        verticalLayout->addWidget(buttonBox);


        retranslateUi(SettingsDialog);

        tabWidget->setCurrentIndex(0);


        QMetaObject::connectSlotsByName(SettingsDialog);
    } // setupUi

    void retranslateUi(QDialog *SettingsDialog)
    {
        SettingsDialog->setWindowTitle(QCoreApplication::translate("SettingsDialog", "\350\256\276\347\275\256", nullptr));
        proxyTypeLabel->setText(QCoreApplication::translate("SettingsDialog", "\344\273\243\347\220\206\347\261\273\345\236\213:", nullptr));
        proxyTypeComboBox->setItemText(0, QCoreApplication::translate("SettingsDialog", "\344\270\215\344\275\277\347\224\250\344\273\243\347\220\206", nullptr));
        proxyTypeComboBox->setItemText(1, QCoreApplication::translate("SettingsDialog", "HTTP\344\273\243\347\220\206", nullptr));
        proxyTypeComboBox->setItemText(2, QCoreApplication::translate("SettingsDialog", "SOCKS5\344\273\243\347\220\206", nullptr));
        proxyTypeComboBox->setItemText(3, QCoreApplication::translate("SettingsDialog", "\347\263\273\347\273\237\344\273\243\347\220\206", nullptr));

        proxyHostLabel->setText(QCoreApplication::translate("SettingsDialog", "\344\273\243\347\220\206\344\270\273\346\234\272:", nullptr));
        proxyPortLabel->setText(QCoreApplication::translate("SettingsDialog", "\344\273\243\347\220\206\347\253\257\345\217\243:", nullptr));
        proxyUserLabel->setText(QCoreApplication::translate("SettingsDialog", "\347\224\250\346\210\267\345\220\215:", nullptr));
        proxyPassLabel->setText(QCoreApplication::translate("SettingsDialog", "\345\257\206\347\240\201:", nullptr));
        tabWidget->setTabText(tabWidget->indexOf(networkTab), QCoreApplication::translate("SettingsDialog", "\347\275\221\347\273\234", nullptr));
        defaultDownloadPathLabel->setText(QCoreApplication::translate("SettingsDialog", "\351\273\230\350\256\244\344\270\213\350\275\275\350\267\257\345\276\204:", nullptr));
        browseButton->setText(QCoreApplication::translate("SettingsDialog", "\346\265\217\350\247\210...", nullptr));
        defaultThreadsLabel->setText(QCoreApplication::translate("SettingsDialog", "\351\273\230\350\256\244\347\272\277\347\250\213\346\225\260:", nullptr));
        tabWidget->setTabText(tabWidget->indexOf(downloadTab), QCoreApplication::translate("SettingsDialog", "\344\270\213\350\275\275", nullptr));
        themeLabel->setText(QCoreApplication::translate("SettingsDialog", "\344\270\273\351\242\230:", nullptr));
        tabWidget->setTabText(tabWidget->indexOf(uiTab), QCoreApplication::translate("SettingsDialog", "\347\225\214\351\235\242", nullptr));
        localListenPortLabel->setText(QCoreApplication::translate("SettingsDialog", "\347\233\221\345\220\254\347\253\257\345\217\243:", nullptr));
        tabWidget->setTabText(tabWidget->indexOf(localServerTab), QCoreApplication::translate("SettingsDialog", "\346\234\254\345\234\260\346\234\215\345\212\241", nullptr));
    } // retranslateUi

};

namespace Ui {
    class SettingsDialog: public Ui_SettingsDialog {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_SETTINGSDIALOG_H
