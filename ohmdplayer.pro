QT       += core gui widgets
INCLUDEPATH += /usr/include/openhmd

CONFIG += c++11

DEFINES += QT_DEPRECATED_WARNINGS

LIBS += -lopenhmd -lmpv

SOURCES += \
    main.cpp \
    ohmdhandler.cpp \
    widget.cpp

HEADERS += \
    ohmdhandler.h \
    widget.h

RESOURCES += \
    shaders.qrc
