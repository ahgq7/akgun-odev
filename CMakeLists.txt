cmake_minimum_required(VERSION 3.16)
project(DicomViewer VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(CONDA_ENV "C:/Users/ahgq7/.conda/envs/dicom-build")
set(CMAKE_PREFIX_PATH
    "${CONDA_ENV}/Library/lib/cmake/vtk-9.3"
    "${CONDA_ENV}/Library/lib/cmake/Qt6"
    "C:/Users/ahgq7/gdcm-install/lib/gdcm-3.0"
    ${CMAKE_PREFIX_PATH}
)

find_package(VTK 9.0 REQUIRED COMPONENTS
    CommonCore
    CommonDataModel
    CommonColor
    IOImage
    ImagingCore
    ImagingColor
    RenderingCore
    RenderingAnnotation
    RenderingOpenGL2
    InteractionStyle
    InteractionWidgets
    InteractionImage
    GUISupportQt
    RenderingQt
)

if(NOT VTK_FOUND)
    message(FATAL_ERROR "VTK bulunamadı. Lütfen CMAKE_PREFIX_PATH ayarını kontrol edin.")
endif()
message(STATUS "VTK bulundu: ${VTK_VERSION}")

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    OpenGL
    OpenGLWidgets
    Network
)
message(STATUS "Qt6 bulundu: ${Qt6_VERSION}")

find_package(GDCM REQUIRED)
message(STATUS "GDCM bulundu: ${GDCM_MAJOR_VERSION}.${GDCM_MINOR_VERSION}.${GDCM_BUILD_VERSION}")

# GDCM include ve link dizinlerini ayarla
set(GDCM_INCLUDE_DIR "C:/Users/ahgq7/gdcm-install/include/gdcm-3.0")
set(GDCM_LINK_DIR    "C:/Users/ahgq7/gdcm-install/lib")
set(GDCM_LIBS gdcmMSFF gdcmDSED gdcmDICT gdcmIOD gdcmCommon)

set(SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/DicomImageViewer.cpp
    src/MeasurementWidget.cpp
    src/TciaBrowser.cpp
    src/ThumbnailStrip.cpp
)

set(HEADERS
    src/MainWindow.h
    src/DicomImageViewer.h
    src/MeasurementWidget.h
    src/ThumbnailStrip.h
)

add_executable(DicomViewer WIN32 ${SOURCES} ${HEADERS})

target_include_directories(DicomViewer PRIVATE src ${GDCM_INCLUDE_DIR})

target_link_libraries(DicomViewer
    PRIVATE
    ${VTK_LIBRARIES}
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    Qt6::Network
)

target_link_directories(DicomViewer PRIVATE ${GDCM_LINK_DIR})
target_link_libraries(DicomViewer PRIVATE ${GDCM_LIBS})

vtk_module_autoinit(
    TARGETS DicomViewer
    MODULES ${VTK_LIBRARIES}
)


message(STATUS "Proje yapılandırıldı. Build: cmake --build . --parallel")
