cmake_minimum_required(VERSION 3.16)
project(ServoErrorAnalyzer VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt5 5.12 REQUIRED COMPONENTS Widgets)

add_executable(ServoErrorAnalyzer
    main.cpp
    main_window.h main_window.cpp
    data_loader.h data_loader.cpp
    chart_view.h chart_view.cpp
    direction_dialog.h direction_dialog.cpp
)

target_link_libraries(ServoErrorAnalyzer PRIVATE Qt5::Widgets)
