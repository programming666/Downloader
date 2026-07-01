cmake_minimum_required(VERSION 3.19)
project(Downloader LANGUAGES CXX)

# Qt installation directory.
# Override by setting CMAKE_PREFIX_PATH or Qt6_DIR before the first project()
# invocation, e.g. `cmake -DCMAKE_PREFIX_PATH=/path/to/Qt/6.10.0/mingw_64 ..`.
# Default is the developer-machine install used by the original author.
if(NOT DEFINED Qt6_DIR AND NOT DEFINED CMAKE_PREFIX_PATH)
    set(Qt6_DIR "D:/Qt/6.10.0/mingw_64/lib/cmake/Qt6")
endif()

if(DEFINED Qt6_DIR)
    message(STATUS "Qt6 path override (Qt6_DIR): ${Qt6_DIR}")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Consolidated find_package: query Qt6 once with all components we need.
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Network Gui Concurrent)

# Qt6::HttpServer is optional; gracefully fall back to the bundled QTcpServer impl.
find_package(Qt6 COMPONENTS HttpServer QUIET)
if(TARGET Qt6::HttpServer)
    message(STATUS "Found Qt6 HttpServer module - enabling QHttpServer backend")
    add_definitions(-DQT_HTTPSERVER_LIB)
else()
    message(STATUS "Qt6 HttpServer module not found - falling back to QTcpServer implementation")
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
)

# Auto-generate translation .qm files from the .ts sources at build time.
# The generated .qm files are loaded at runtime as Qt resources (prefix /i18n/),
# so icon.qrc no longer needs to list them explicitly.
qt_add_translations(Downloader TS_FILES translations/zh_CN.ts translations/en_US.ts)

target_compile_features(Downloader PRIVATE cxx_std_17)

target_link_libraries(Downloader
    PRIVATE
        Qt6::Core
        Qt6::Widgets
        Qt6::Network
        Qt6::Gui
        Qt6::Concurrent
)

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
