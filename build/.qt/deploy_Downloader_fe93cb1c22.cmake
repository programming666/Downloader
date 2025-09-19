include("D:/QT6Project/Downloader/build/.qt/QtDeploySupport.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/Downloader-plugins.cmake" OPTIONAL)
set(__QT_DEPLOY_I18N_CATALOGS "qtbase")

qt6_deploy_runtime_dependencies(
    EXECUTABLE "D:/QT6Project/Downloader/build/Downloader.exe"
    GENERATE_QT_CONF
)
