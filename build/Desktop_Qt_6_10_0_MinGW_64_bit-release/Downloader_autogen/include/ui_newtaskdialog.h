/********************************************************************************
** Form generated from reading UI file 'newtaskdialog.ui'
**
** Created by: Qt User Interface Compiler version 6.10.0
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_NEWTASKDIALOG_H
#define UI_NEWTASKDIALOG_H

#include <QtCore/QVariant>
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QVBoxLayout>

QT_BEGIN_NAMESPACE

class Ui_NewTaskDialog
{
public:
    QVBoxLayout *verticalLayout;
    QFormLayout *formLayout;
    QLabel *urlLabel;
    QLineEdit *urlLineEdit;
    QLabel *savePathLabel;
    QHBoxLayout *horizontalLayout;
    QLineEdit *savePathLineEdit;
    QPushButton *browseButton;
    QLabel *threadCountLabel;
    QSpinBox *threadCountSpinBox;
    QSpacerItem *verticalSpacer;
    QDialogButtonBox *buttonBox;

    void setupUi(QDialog *NewTaskDialog)
    {
        if (NewTaskDialog->objectName().isEmpty())
            NewTaskDialog->setObjectName("NewTaskDialog");
        NewTaskDialog->resize(400, 200);
        verticalLayout = new QVBoxLayout(NewTaskDialog);
        verticalLayout->setObjectName("verticalLayout");
        formLayout = new QFormLayout();
        formLayout->setObjectName("formLayout");
        urlLabel = new QLabel(NewTaskDialog);
        urlLabel->setObjectName("urlLabel");

        formLayout->setWidget(0, QFormLayout::ItemRole::LabelRole, urlLabel);

        urlLineEdit = new QLineEdit(NewTaskDialog);
        urlLineEdit->setObjectName("urlLineEdit");

        formLayout->setWidget(0, QFormLayout::ItemRole::FieldRole, urlLineEdit);

        savePathLabel = new QLabel(NewTaskDialog);
        savePathLabel->setObjectName("savePathLabel");

        formLayout->setWidget(1, QFormLayout::ItemRole::LabelRole, savePathLabel);

        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setObjectName("horizontalLayout");
        savePathLineEdit = new QLineEdit(NewTaskDialog);
        savePathLineEdit->setObjectName("savePathLineEdit");

        horizontalLayout->addWidget(savePathLineEdit);

        browseButton = new QPushButton(NewTaskDialog);
        browseButton->setObjectName("browseButton");

        horizontalLayout->addWidget(browseButton);


        formLayout->setLayout(1, QFormLayout::ItemRole::FieldRole, horizontalLayout);

        threadCountLabel = new QLabel(NewTaskDialog);
        threadCountLabel->setObjectName("threadCountLabel");

        formLayout->setWidget(2, QFormLayout::ItemRole::LabelRole, threadCountLabel);

        threadCountSpinBox = new QSpinBox(NewTaskDialog);
        threadCountSpinBox->setObjectName("threadCountSpinBox");

        formLayout->setWidget(2, QFormLayout::ItemRole::FieldRole, threadCountSpinBox);


        verticalLayout->addLayout(formLayout);

        verticalSpacer = new QSpacerItem(20, 40, QSizePolicy::Policy::Minimum, QSizePolicy::Policy::Expanding);

        verticalLayout->addItem(verticalSpacer);

        buttonBox = new QDialogButtonBox(NewTaskDialog);
        buttonBox->setObjectName("buttonBox");
        buttonBox->setOrientation(Qt::Horizontal);
        buttonBox->setStandardButtons(QDialogButtonBox::Cancel|QDialogButtonBox::Ok);

        verticalLayout->addWidget(buttonBox);


        retranslateUi(NewTaskDialog);
        QObject::connect(buttonBox, &QDialogButtonBox::accepted, NewTaskDialog, qOverload<>(&QDialog::accept));
        QObject::connect(buttonBox, &QDialogButtonBox::rejected, NewTaskDialog, qOverload<>(&QDialog::reject));

        QMetaObject::connectSlotsByName(NewTaskDialog);
    } // setupUi

    void retranslateUi(QDialog *NewTaskDialog)
    {
        NewTaskDialog->setWindowTitle(QCoreApplication::translate("NewTaskDialog", "Dialog", nullptr));
        urlLabel->setText(QCoreApplication::translate("NewTaskDialog", "URL:", nullptr));
        savePathLabel->setText(QCoreApplication::translate("NewTaskDialog", "\344\277\235\345\255\230\350\267\257\345\276\204:", nullptr));
        browseButton->setText(QCoreApplication::translate("NewTaskDialog", "\346\265\217\350\247\210...", nullptr));
        threadCountLabel->setText(QCoreApplication::translate("NewTaskDialog", "\347\272\277\347\250\213\346\225\260:", nullptr));
    } // retranslateUi

};

namespace Ui {
    class NewTaskDialog: public Ui_NewTaskDialog {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_NEWTASKDIALOG_H
