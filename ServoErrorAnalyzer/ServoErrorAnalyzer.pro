QT       += core gui widgets
TEMPLATE  = app
CONFIG   += c++11

TARGET    = ServoErrorAnalyzer

SOURCES  += main.cpp \
            main_window.cpp \
            data_loader.cpp \
            chart_view.cpp \
            direction_dialog.cpp

HEADERS  += main_window.h \
            data_loader.h \
            chart_view.h \
            direction_dialog.h
