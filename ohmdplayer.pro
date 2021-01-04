QT       += core gui widgets x11extras
INCLUDEPATH += /usr/include/openhmd

CONFIG += c++11

DEFINES += QT_DEPRECATED_WARNINGS

LIBS += -lopenhmd -lmpv -lXrandr

SOURCES += \
    main.cpp \
    ohmdhandler.cpp \
    widget.cpp

HEADERS += \
    ohmdhandler.h \
    widget.h

RESOURCES += \
    shaders.qrc
