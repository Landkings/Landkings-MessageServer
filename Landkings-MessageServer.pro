TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
CONFIG -= qt

QMAKE_CXXFLAGS_WARN_ON += -Wno-unused-parameter -Wno-maybe-uninitialized -Wno-pointer-arith

QMAKE_CXXFLAGS += -DUSE_ASIO

SOURCES += main.cpp \
    MS-Callbacks.cpp \
    MS-Functions.cpp \
    MS-Core.cpp

HEADERS += \
    MS.hpp

LIBS += -luWS -lssl -lpthread -lboost_system -lz -lboost_program_options
