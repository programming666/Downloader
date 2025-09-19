/********************************************************************************
** Form generated from reading UI file 'mainwindow.ui'
**
** Created by: Qt User Interface Compiler version 6.10.0
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_MAINWINDOW_H
#define UI_MAINWINDOW_H

#include <QtCore/QVariant>
#include <QtGui/QAction>
#include <QtGui/QIcon>
#include <QtWidgets/QApplication>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QToolBar>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_MainWindow
{
public:
    QAction *actionNewTask;
    QAction *actionStartAll;
    QAction *actionPauseAll;
    QAction *actionCancelSelected;
    QAction *actionDeleteSelected;
    QAction *actionPauseSelected;
    QAction *actionResumeSelected;
    QAction *actionSettings;
    QAction *actionAbout;
    QAction *actionExit;
    QAction *actionChinese;
    QAction *actionEnglish;
    QWidget *centralwidget;
    QVBoxLayout *verticalLayout;
    QTableWidget *tableWidget;
    QMenuBar *menubar;
    QMenu *menuFile;
    QMenu *menuTask;
    QMenu *menuSettings;
    QMenu *menuLanguage;
    QMenu *menuHelp;
    QStatusBar *statusbar;
    QToolBar *toolBar;

    void setupUi(QMainWindow *MainWindow)
    {
        if (MainWindow->objectName().isEmpty())
            MainWindow->setObjectName("MainWindow");
        MainWindow->resize(900, 600);
        QIcon icon;
        icon.addFile(QString::fromUtf8("icon.ico"), QSize(), QIcon::Mode::Normal, QIcon::State::Off);
        MainWindow->setWindowIcon(icon);
        actionNewTask = new QAction(MainWindow);
        actionNewTask->setObjectName("actionNewTask");
        QIcon icon1;
        icon1.addFile(QString::fromUtf8(":/icons/new.png"), QSize(), QIcon::Mode::Normal, QIcon::State::Off);
        actionNewTask->setIcon(icon1);
        actionStartAll = new QAction(MainWindow);
        actionStartAll->setObjectName("actionStartAll");
        QIcon icon2;
        icon2.addFile(QString::fromUtf8(":/icons/start.png"), QSize(), QIcon::Mode::Normal, QIcon::State::Off);
        actionStartAll->setIcon(icon2);
        actionPauseAll = new QAction(MainWindow);
        actionPauseAll->setObjectName("actionPauseAll");
        QIcon icon3;
        icon3.addFile(QString::fromUtf8(":/icons/pause.png"), QSize(), QIcon::Mode::Normal, QIcon::State::Off);
        actionPauseAll->setIcon(icon3);
        actionCancelSelected = new QAction(MainWindow);
        actionCancelSelected->setObjectName("actionCancelSelected");
        QIcon icon4;
        icon4.addFile(QString::fromUtf8(":/icons/cancel.png"), QSize(), QIcon::Mode::Normal, QIcon::State::Off);
        actionCancelSelected->setIcon(icon4);
        actionDeleteSelected = new QAction(MainWindow);
        actionDeleteSelected->setObjectName("actionDeleteSelected");
        QIcon icon5;
        icon5.addFile(QString::fromUtf8(":/icons/delete.png"), QSize(), QIcon::Mode::Normal, QIcon::State::Off);
        actionDeleteSelected->setIcon(icon5);
        actionPauseSelected = new QAction(MainWindow);
        actionPauseSelected->setObjectName("actionPauseSelected");
        actionPauseSelected->setIcon(icon3);
        actionResumeSelected = new QAction(MainWindow);
        actionResumeSelected->setObjectName("actionResumeSelected");
        actionResumeSelected->setIcon(icon2);
        actionSettings = new QAction(MainWindow);
        actionSettings->setObjectName("actionSettings");
        QIcon icon6;
        icon6.addFile(QString::fromUtf8(":/icons/settings.png"), QSize(), QIcon::Mode::Normal, QIcon::State::Off);
        actionSettings->setIcon(icon6);
        actionAbout = new QAction(MainWindow);
        actionAbout->setObjectName("actionAbout");
        QIcon icon7;
        icon7.addFile(QString::fromUtf8(":/icons/about.png"), QSize(), QIcon::Mode::Normal, QIcon::State::Off);
        actionAbout->setIcon(icon7);
        actionExit = new QAction(MainWindow);
        actionExit->setObjectName("actionExit");
        actionChinese = new QAction(MainWindow);
        actionChinese->setObjectName("actionChinese");
        actionEnglish = new QAction(MainWindow);
        actionEnglish->setObjectName("actionEnglish");
        centralwidget = new QWidget(MainWindow);
        centralwidget->setObjectName("centralwidget");
        verticalLayout = new QVBoxLayout(centralwidget);
        verticalLayout->setObjectName("verticalLayout");
        tableWidget = new QTableWidget(centralwidget);
        tableWidget->setObjectName("tableWidget");

        verticalLayout->addWidget(tableWidget);

        MainWindow->setCentralWidget(centralwidget);
        menubar = new QMenuBar(MainWindow);
        menubar->setObjectName("menubar");
        menubar->setGeometry(QRect(0, 0, 900, 23));
        menuFile = new QMenu(menubar);
        menuFile->setObjectName("menuFile");
        menuTask = new QMenu(menubar);
        menuTask->setObjectName("menuTask");
        menuSettings = new QMenu(menubar);
        menuSettings->setObjectName("menuSettings");
        menuLanguage = new QMenu(menubar);
        menuLanguage->setObjectName("menuLanguage");
        menuHelp = new QMenu(menubar);
        menuHelp->setObjectName("menuHelp");
        MainWindow->setMenuBar(menubar);
        statusbar = new QStatusBar(MainWindow);
        statusbar->setObjectName("statusbar");
        MainWindow->setStatusBar(statusbar);
        toolBar = new QToolBar(MainWindow);
        toolBar->setObjectName("toolBar");
        MainWindow->addToolBar(Qt::ToolBarArea::TopToolBarArea, toolBar);

        menubar->addAction(menuFile->menuAction());
        menubar->addAction(menuTask->menuAction());
        menubar->addAction(menuSettings->menuAction());
        menubar->addAction(menuLanguage->menuAction());
        menubar->addAction(menuHelp->menuAction());
        menuFile->addAction(actionNewTask);
        menuFile->addSeparator();
        menuFile->addAction(actionExit);
        menuTask->addAction(actionStartAll);
        menuTask->addAction(actionPauseAll);
        menuTask->addSeparator();
        menuTask->addAction(actionPauseSelected);
        menuTask->addAction(actionResumeSelected);
        menuTask->addSeparator();
        menuTask->addAction(actionCancelSelected);
        menuTask->addAction(actionDeleteSelected);
        menuSettings->addAction(actionSettings);
        menuLanguage->addAction(actionChinese);
        menuLanguage->addAction(actionEnglish);
        menuHelp->addAction(actionAbout);
        toolBar->addAction(actionNewTask);
        toolBar->addAction(actionStartAll);
        toolBar->addAction(actionPauseAll);
        toolBar->addAction(actionPauseSelected);
        toolBar->addAction(actionResumeSelected);
        toolBar->addAction(actionCancelSelected);
        toolBar->addAction(actionDeleteSelected);
        toolBar->addSeparator();
        toolBar->addAction(actionSettings);
        toolBar->addAction(actionAbout);

        retranslateUi(MainWindow);
        QObject::connect(actionExit, &QAction::triggered, MainWindow, qOverload<>(&QMainWindow::close));

        QMetaObject::connectSlotsByName(MainWindow);
    } // setupUi

    void retranslateUi(QMainWindow *MainWindow)
    {
        MainWindow->setWindowTitle(QCoreApplication::translate("MainWindow", "\345\244\232\347\272\277\347\250\213\344\270\213\350\275\275\345\231\250", nullptr));
        actionNewTask->setText(QCoreApplication::translate("MainWindow", "\346\226\260\345\273\272\344\273\273\345\212\241", nullptr));
#if QT_CONFIG(tooltip)
        actionNewTask->setToolTip(QCoreApplication::translate("MainWindow", "\346\226\260\345\273\272\344\270\213\350\275\275\344\273\273\345\212\241", nullptr));
#endif // QT_CONFIG(tooltip)
        actionStartAll->setText(QCoreApplication::translate("MainWindow", "\345\205\250\351\203\250\345\274\200\345\247\213", nullptr));
#if QT_CONFIG(tooltip)
        actionStartAll->setToolTip(QCoreApplication::translate("MainWindow", "\345\274\200\345\247\213\346\211\200\346\234\211\344\270\213\350\275\275\344\273\273\345\212\241", nullptr));
#endif // QT_CONFIG(tooltip)
        actionPauseAll->setText(QCoreApplication::translate("MainWindow", "\345\205\250\351\203\250\346\232\202\345\201\234", nullptr));
#if QT_CONFIG(tooltip)
        actionPauseAll->setToolTip(QCoreApplication::translate("MainWindow", "\346\232\202\345\201\234\346\211\200\346\234\211\344\270\213\350\275\275\344\273\273\345\212\241", nullptr));
#endif // QT_CONFIG(tooltip)
        actionCancelSelected->setText(QCoreApplication::translate("MainWindow", "\345\217\226\346\266\210\351\200\211\344\270\255", nullptr));
#if QT_CONFIG(tooltip)
        actionCancelSelected->setToolTip(QCoreApplication::translate("MainWindow", "\345\217\226\346\266\210\351\200\211\344\270\255\347\232\204\344\270\213\350\275\275\344\273\273\345\212\241", nullptr));
#endif // QT_CONFIG(tooltip)
        actionDeleteSelected->setText(QCoreApplication::translate("MainWindow", "\345\210\240\351\231\244\351\200\211\344\270\255", nullptr));
#if QT_CONFIG(tooltip)
        actionDeleteSelected->setToolTip(QCoreApplication::translate("MainWindow", "\345\210\240\351\231\244\351\200\211\344\270\255\347\232\204\344\270\213\350\275\275\344\273\273\345\212\241", nullptr));
#endif // QT_CONFIG(tooltip)
        actionPauseSelected->setText(QCoreApplication::translate("MainWindow", "\346\232\202\345\201\234\351\200\211\344\270\255", nullptr));
#if QT_CONFIG(tooltip)
        actionPauseSelected->setToolTip(QCoreApplication::translate("MainWindow", "\346\232\202\345\201\234\351\200\211\344\270\255\347\232\204\344\270\213\350\275\275\344\273\273\345\212\241", nullptr));
#endif // QT_CONFIG(tooltip)
        actionResumeSelected->setText(QCoreApplication::translate("MainWindow", "\347\273\247\347\273\255\351\200\211\344\270\255", nullptr));
#if QT_CONFIG(tooltip)
        actionResumeSelected->setToolTip(QCoreApplication::translate("MainWindow", "\347\273\247\347\273\255\351\200\211\344\270\255\347\232\204\344\270\213\350\275\275\344\273\273\345\212\241", nullptr));
#endif // QT_CONFIG(tooltip)
        actionSettings->setText(QCoreApplication::translate("MainWindow", "\350\256\276\347\275\256", nullptr));
#if QT_CONFIG(tooltip)
        actionSettings->setToolTip(QCoreApplication::translate("MainWindow", "\345\272\224\347\224\250\347\250\213\345\272\217\350\256\276\347\275\256", nullptr));
#endif // QT_CONFIG(tooltip)
        actionAbout->setText(QCoreApplication::translate("MainWindow", "\345\205\263\344\272\216", nullptr));
#if QT_CONFIG(tooltip)
        actionAbout->setToolTip(QCoreApplication::translate("MainWindow", "\345\205\263\344\272\216\346\234\254\347\250\213\345\272\217", nullptr));
#endif // QT_CONFIG(tooltip)
        actionExit->setText(QCoreApplication::translate("MainWindow", "\351\200\200\345\207\272", nullptr));
#if QT_CONFIG(tooltip)
        actionExit->setToolTip(QCoreApplication::translate("MainWindow", "\351\200\200\345\207\272\345\272\224\347\224\250\347\250\213\345\272\217", nullptr));
#endif // QT_CONFIG(tooltip)
        actionChinese->setText(QCoreApplication::translate("MainWindow", "\344\270\255\346\226\207", nullptr));
#if QT_CONFIG(tooltip)
        actionChinese->setToolTip(QCoreApplication::translate("MainWindow", "\345\210\207\346\215\242\345\210\260\344\270\255\346\226\207\347\225\214\351\235\242", nullptr));
#endif // QT_CONFIG(tooltip)
        actionEnglish->setText(QCoreApplication::translate("MainWindow", "English", nullptr));
#if QT_CONFIG(tooltip)
        actionEnglish->setToolTip(QCoreApplication::translate("MainWindow", "Switch to English interface", nullptr));
#endif // QT_CONFIG(tooltip)
        menuFile->setTitle(QCoreApplication::translate("MainWindow", "\346\226\207\344\273\266", nullptr));
        menuTask->setTitle(QCoreApplication::translate("MainWindow", "\344\273\273\345\212\241", nullptr));
        menuSettings->setTitle(QCoreApplication::translate("MainWindow", "\350\256\276\347\275\256", nullptr));
        menuLanguage->setTitle(QCoreApplication::translate("MainWindow", "\350\257\255\350\250\200", nullptr));
        menuHelp->setTitle(QCoreApplication::translate("MainWindow", "\345\270\256\345\212\251", nullptr));
        toolBar->setWindowTitle(QCoreApplication::translate("MainWindow", "toolBar", nullptr));
    } // retranslateUi

};

namespace Ui {
    class MainWindow: public Ui_MainWindow {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_MAINWINDOW_H
