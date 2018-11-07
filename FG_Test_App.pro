#-------------------------------------------------
#
# Project created by QtCreator 2018-11-01T17:06:02
#
#-------------------------------------------------

QT       += core gui serialport widgets

TARGET = FG_Test
TEMPLATE = app

# The following define makes your compiler emit warnings if you use
# any feature of Qt which has been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if you use deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

CONFIG += c++1z  # C++17

SOURCES += \
    main.cpp \
    App.cpp \
    MainWindow.cpp \
    Util.cpp \
    Settings.cpp \
    DebugWindow.cpp \
    UARTBox.cpp \
    GLVideoWidget.cpp \
    WorkerThread.cpp \
    FakeFrameGenerator.cpp \
    SerialPortWorker.cpp \
    Prefs.cpp \
    Frame.cpp \
    Recorder.cpp

HEADERS += \
    App.h \
    MainWindow.h \
    Util.h \
    Settings.h \
    Version.h \
    DebugWindow.h \
    UARTBox.h \
    GLVideoWidget.h \
    WorkerThread.h \
    FakeFrameGenerator.h \
    SerialPortWorker.h \
    Prefs.h \
    Frame.h \
    Recorder.h

FORMS += \
    MainWindow.ui \
    DebugWindow.ui \
    UARTBox.ui \
    Prefs.ui

RESOURCES += \
    Resources.qrc \
    qdarkstyle/style.qrc

macx {
    # Add mac-specific libs, etc, here
    #LIBS += -framework CoreServices
    LIBS += -L $$OUT_PWD/QuaZip/quazip -lquazip -lz
}

win32 {
    # Add windows-specific stuff here
    QMAKE_CXXFLAGS += /std:c++17
    CONFIG += windows
    LIBS += opengl32.lib $$PWD/QuaZip/winzlib/lib/zlib.lib
    CONFIG(debug, debug|release) {
        LIBS += $$OUT_PWD/QuaZip/quaip/debug/quazip.lib
    }
    CONFIG(release, debug|release) {
        LIBS += $$OUT_PWD/QuaZip/quaip/release/quazip.lib
    }
}

INCLUDEPATH += QuaZip

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

