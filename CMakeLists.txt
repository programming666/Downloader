cmake_minimum_required(VERSION 3.19)
# 设置Qt路径（根据实际安装路径修改）
set(Qt6_DIR "D:/Qt/6.10.0/mingw_64/lib/cmake/Qt6")
project(Downloader LANGUAGES CXX)
# 打印调试信息
message(STATUS "Qt6路径: ${Qt6_DIR}")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Network Gui Concurrent)
# 检查Qt6是否支持HttpServer模块
find_package(Qt6 COMPONENTS HttpServer REQUIRED)
if(TARGET Qt6::HttpServer)
    message(STATUS "找到Qt6 HttpServer模块")
    # 定义宏以启用QHttpServer
    add_definitions(-DQT_HTTPSERVER_LIB)
else()
    message(STATUS "未找到Qt6 HttpServer模块，将使用QTcpServer实现HTTP服务器")
endif()
if(TARGET Qt6::Concurrent)
    message(STATUS "找到Qt6 Concurrent模块")
else()
    message(FATAL_ERROR "未找到Qt6 Concurrent模块")
endif()
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
    httpserver.cpp
    httpserver.h
    newtaskdialog.cpp
    newtaskdialog.h
    newtaskdialog.ui
    settingsdialog.cpp
    settingsdialog.h
    settingsdialog.ui
    scheduledialog.cpp
    scheduledialog.h
    scheduledialog.ui
    schedulemanager.cpp
    schedulemanager.h
    historydialog.cpp
    historydialog.h
    historydialog.ui
    icon.qrc
)
target_link_libraries(Downloader
    PRIVATE
        Qt6::Core
        Qt6::Widgets
        Qt6::Network
        Qt6::Gui
        Qt6::Concurrent
)
# 如果HttpServer模块可用，则链接它
if(TARGET Qt6::HttpServer)
    target_link_libraries(Downloader PRIVATE Qt6::HttpServer)
endif()
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
