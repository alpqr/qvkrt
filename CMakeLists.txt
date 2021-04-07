cmake_minimum_required(VERSION 3.14)
project(qvkrt LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 COMPONENTS Core)
find_package(Qt6 COMPONENTS Gui)
find_package(Qt6 COMPONENTS Qml)
find_package(Qt6 COMPONENTS Quick)

qt6_add_executable(qvkrt
    main.cpp
    vktexitem.cpp vktexitem.h
    rt.cpp rt.h
)
target_link_libraries(qvkrt PUBLIC
    Qt::Core
    Qt::Gui
    Qt::GuiPrivate
    Qt::Qml
    Qt::Quick
)

set(qvkrt_resource_files
    "main.qml"
    "raygen.rgen.spv"
    "miss.rmiss.spv"
    "closesthit.rchit.spv"
)

qt6_add_resources(qvkrt "qvkrt"
    PREFIX
        "/"
    FILES
        ${qvkrt_resource_files}
)

set_target_properties(qvkrt PROPERTIES
    QT_QML_MODULE_VERSION 1.0
    QT_QML_MODULE_URI VkRt
)

qt6_qml_type_registration(qvkrt)
