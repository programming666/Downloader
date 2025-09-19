cmake_minimum_required(VERSION 3.19)
project(Downloader LANGUAGES CXX)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Network Sql Gui)
qt_standard_project_setup()
qt_add_executable(Downloader
    WIN32
    MACOSX_BUNDLE
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    downloadmanager.cpp
    downloadmanager.h
    downloadtask.cpp
    downloadtask.h
    httpworker.cpp
    httpworker.h
    historymanager.cpp
    historymanager.h
    settingsmanager.cpp
    settingsmanager.h
    systemtray.cpp
    systemtray.h
    localserver.cpp
    localserver.h
    newtaskdialog.cpp
    newtaskdialog.h
    newtaskdialog.ui
    settingsdialog.cpp
    settingsdialog.h
    settingsdialog.ui
    icon.qrc
)
target_link_libraries(Downloader
    PRIVATE
        Qt6::Core
        Qt6::Widgets
        Qt6::Network
        Qt6::Sql
        Qt6::Gui
)
include(GNUInstallDirs)
install(TARGETS Downloader
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
qt_generate_deploy_app_script(
    TARGET Downloader
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
