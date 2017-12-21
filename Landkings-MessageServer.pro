TEMPLATE = app
CONFIG += console c++11
CONFIG -= app_bundle
CONFIG -= qt

QMAKE_CXXFLAGS_WARN_ON += -Wno-unused-parameter -Wno-maybe-uninitialized -Wno-pointer-arith

QMAKE_CXXFLAGS += -DUSE_ASIO

INCLUDEPATH += $$PWD/include $$PWD/lib

SOURCES += main.cpp $$PWD/src/*.cpp

HEADERS += $$PWD/include/*.hpp $$PWD/include/*.h

LIBS += -luWS -lssl -lpthread -lboost_system -lz -lboost_program_options
