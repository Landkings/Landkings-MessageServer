TEMPLATE = app
CONFIG += console c++11
CONFIG -= app_bundle
CONFIG -= qt

QMAKE_CXXFLAGS_WARN_ON += -Wno-unused-parameter

QMAKE_CXXFLAGS += -DUSE_ASIO

SOURCES += main.cpp \
    WsServer.cpp

HEADERS += \
    WsServer.h

LIBS += -luWS -lssl -lpthread -lboost_system -lz -lboost_program_options
