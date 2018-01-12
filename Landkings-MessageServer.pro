TEMPLATE = app
CONFIG += console
CONFIG += c++1z
CONFIG -= app_bundle
CONFIG -= qt

QMAKE_CXXFLAGS_WARN_ON += -Wno-unused-parameter -Wno-maybe-uninitialized -Wno-pointer-arith

QMAKE_CXXFLAGS += -DUSE_ASIO

INCLUDEPATH += $$PWD/include $$PWD/lib

SOURCES += main.cpp $$PWD/src/*.cpp

HEADERS += $$PWD/include/*.hpp $$PWD/include/*.h

LIBS += -L/usr/lib/x86_64-linux-gnu \
-luWS -lssl -lpthread -lz  -lboost_program_options -lboost_system
