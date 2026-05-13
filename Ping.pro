TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
CONFIG -= qt

SOURCES += \
        guid.cpp \
        main.cpp \
        socket.cpp

win32:LIBS += -lws2_32 -lole32

HEADERS += \
    guid.h \
    socket.h
