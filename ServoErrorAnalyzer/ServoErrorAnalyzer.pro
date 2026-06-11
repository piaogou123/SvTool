QT       += core gui widgets
TEMPLATE  = app
CONFIG   += c++14

TARGET    = ServoErrorAnalyzer

# 源码为 UTF-8(含中文字面量);MSVC 需显式指定,MinGW/GCC 默认即 UTF-8
msvc {
    QMAKE_CXXFLAGS += /utf-8
}

SOURCES  += main.cpp \
            main_window.cpp \
            data_loader.cpp \
            chart_view.cpp \
            circle_analysis.cpp \
            direction_dialog.cpp \
            circularity_widget.cpp

HEADERS  += main_window.h \
            data_loader.h \
            chart_view.h \
            circle_analysis.h \
            direction_dialog.h \
            direction_defs.h \
            circularity_widget.h
