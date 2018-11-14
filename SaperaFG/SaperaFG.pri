
win32 {
    SaperaDir = $$getenv(SaperaDir)
    INCLUDEPATH += $${SaperaDir}/Include
    LIBS += $${SaperaDir}/Lib/Win64/SapClassicBasic.lib $${SaperaDir}/Lib/Win64/coreapi.lib
    !isEmpty(SaperaDir) {
        message("SaperaDir = $${SaperaDir}")
        SOURCES += \
            SaperaFG/FPGA.cpp \
            SaperaFG/PagedRingBuffer.cpp \
            SaperaFG/SaperaFG.cpp \
            SaperaFG/SpikeGLHandlerThread.cpp \
            SaperaFG/Thread.cpp

        HEADERS += \
            SaperaFG/SaperaFG.h
    } else {
        message("Sapera not found. If you wish to build with Sapera enabled, make sure SaperaDir is defined in the environment.")
    }
} else {
    message("Sapera not available for this platform. Excluding Sapera-dependent Framegrabber components from build.")
}

