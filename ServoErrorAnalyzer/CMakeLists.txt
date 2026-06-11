cmake_minimum_required(VERSION 3.16)
project(ServoErrorAnalyzer VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# 源码为 UTF-8(含中文字面量);MSVC 需显式指定
if(MSVC)
    add_compile_options(/utf-8)
endif()

find_package(Qt5 5.12 REQUIRED COMPONENTS Widgets)

add_executable(ServoErrorAnalyzer
    main.cpp
    main_window.h main_window.cpp
    data_loader.h data_loader.cpp
    chart_view.h chart_view.cpp
    circle_analysis.h circle_analysis.cpp
    servo_params.h servo_params.cpp
    diagnosis.h diagnosis.cpp
    diagnosis_dialog.h diagnosis_dialog.cpp
    direction_defs.h
    direction_dialog.h direction_dialog.cpp
    circularity_widget.h circularity_widget.cpp
)

target_link_libraries(ServoErrorAnalyzer PRIVATE Qt5::Widgets)
