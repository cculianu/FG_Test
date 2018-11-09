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
    Recorder.cpp \
    FFmpegEncoder.cpp

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
    Recorder.h \
    FFmpegEncoder.h

FORMS += \
    MainWindow.ui \
    DebugWindow.ui \
    UARTBox.ui \
    Prefs.ui

RESOURCES += \
    Resources.qrc \
    qdarkstyle/style.qrc

win32|macx {
    !contains(QT_ARCH, x86_64) {
        error("Only 64-bit builds are supported at this time. Please reconfigure your project in Qt Creator using a 64-bit Kit.")
    }
} else {
    error("Only Windows and macOS builds are supported at this time.")
}

macx {
    # Add mac-specific libs, etc, here
    #LIBS += -framework CoreServices
    LIBS += -L$$OUT_PWD/QuaZip/quazip -lquazip -lz

    # FFmpeg
    INCLUDEPATH += $$PWD/FFmpeg/osx/include
    fflib.dir = $$PWD/FFmpeg/osx/bin
    fflib.flags = -L$$fflib.dir
    fflib.all_files = $$files($$fflib.dir/*.dylib)
    fflib.cpydest = $$OUT_PWD/$${TARGET}.app/Contents/MacOS
    for(f, fflib.all_files) {
        bn = $$basename(f) # bn is of the form libXXX.dylib
        fflib.libs += $$bn  # save libXXX.dylib filename just in case we need it
        fflib.destlibs += $${fflib.cpydest}/$${bn}
        tmp = $$replace(bn, .dylib, ) # libXXX.dylib -> libXXX
        tmp = $$replace(tmp, lib, -l) # libXXX -> -lXXX
        fflib.flags += $$tmp # add -lXXX flags for below 'LIBS +='
    }
    # add the above-constructed -L/bla/bla -lXXX -lYYY to the LIBS for qmake...
    LIBS += $$fflib.flags
    # The following copies the .dylibs to the generated .app bundle
    cpy.target = not_a_real_file
    cpy.commands = mkdir -vp $$fflib.cpydest; cp -fpv $$fflib.all_files $$fflib.cpydest
    cpy.depends = cpy2
    cpy2.commands = @echo Copying .dylibs to .app bundle...
    QMAKE_EXTRA_TARGETS += cpy cpy2
    POST_TARGETDEPS += not_a_real_file
    QMAKE_CLEAN += $$fflib.destlibs
}

win32 {
    # Add windows-specific stuff here
    QMAKE_CXXFLAGS += /std:c++17
    CONFIG += windows
    LIBS += opengl32.lib $$PWD/QuaZip/winzlib/lib/zlib.lib
    CONFIG(debug, debug|release) {
        LIBS += $$OUT_PWD/QuaZip/quazip/debug/quazip.lib
    }
    CONFIG(release, debug|release) {
        LIBS += $$OUT_PWD/QuaZip/quazip/release/quazip.lib
    }
    INCLUDEPATH += $$PWD/QuaZip/winzlib/include
    INCLUDEPATH += $$PWD/FFmpeg/win/include

    # FFmpeg
    fflib.bindir = $$PWD/FFmpeg/win/bin
    fflib.libdir = $$PWD/FFmpeg/win/lib
    fflib.incdir = $$PWD/FFmpeg/win/include
    INCLUDEPATH += $$fflib.incdir
    LIBS += $$files($${fflib.libdir}/*.lib)
    CONFIG(debug, debug|release) {
        fflib.cpydest = $$OUT_PWD/debug
    }
    CONFIG(release, debug|release) {
        fflib.cpydest = $$OUT_PWD/release
    }
    # The following copies the DLLs to the same directory as the .EXE
    cpy.target = not_a_real_file
    cpy.commands = copy /Y $$shell_path($${fflib.bindir}/*.dll) $$shell_path($${fflib.cpydest})
    cpy.depends = cpy2
    cpy2.commands = @echo Copying .DLLs to same dir as .EXE...
    QMAKE_EXTRA_TARGETS += cpy cpy2
    POST_TARGETDEPS += not_a_real_file
    QMAKE_CLEAN += $$files($${fflib.cpydest}/*.dll)

}

INCLUDEPATH += $$PWD/QuaZip
DEFINES += QUAZIP_STATIC

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

