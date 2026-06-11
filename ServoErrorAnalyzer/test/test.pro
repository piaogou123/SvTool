QT       += core gui widgets
TEMPLATE  = app
CONFIG   += c++14 console
TARGET    = test_analysis

INCLUDEPATH += ..

SOURCES  += test_analysis.cpp \
            ../data_loader.cpp \
            ../chart_view.cpp \
            ../circle_analysis.cpp \
            ../servo_params.cpp \
            ../diagnosis.cpp \
            ../diagnosis_dialog.cpp \
            ../circularity_widget.cpp

HEADERS  += ../data_loader.h \
            ../direction_defs.h \
            ../chart_view.h \
            ../circle_analysis.h \
            ../servo_params.h \
            ../diagnosis.h \
            ../diagnosis_dialog.h \
            ../circularity_widget.h
