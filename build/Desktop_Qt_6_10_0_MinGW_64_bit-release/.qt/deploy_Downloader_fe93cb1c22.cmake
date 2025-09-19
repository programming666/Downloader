include("D:/QT6Project/Downloader/build/Desktop_Qt_6_10_0_MinGW_64_bit-release/.qt/QtDeploySupport.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/Downloader-plugins.cmake" OPTIONAL)
set(__QT_DEPLOY_I18N_CATALOGS "qtbase")

qt6_deploy_runtime_dependencies(
    EXECUTABLE "D:/QT6Project/Downloader/build/Desktop_Qt_6_10_0_MinGW_64_bit-release/Downloader.exe"
    GENERATE_QT_CONF
)
