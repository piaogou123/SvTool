QT       += core gui widgets
TEMPLATE  = app
CONFIG   += c++17

TARGET    = ServoErrorAnalyzer

SOURCES  += main.cpp \
            main_window.cpp \
            data_loader.cpp \
            chart_view.cpp

HEADERS  += main_window.h \
            data_loader.h \
            chart_view.h
