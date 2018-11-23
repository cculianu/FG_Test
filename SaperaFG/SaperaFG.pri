
win32 {
    SaperaDir = $$getenv(SaperaDir)
    !isEmpty(SaperaDir) {
        message("Sapera Found ---> SaperaDir = $${SaperaDir}")
        QMAKE_INCDIR += $${SaperaDir}/Include $${SaperaDir}/Classes/Basic
        LIBS += $${SaperaDir}/Lib/Win64/SapClassBasic.lib psapi.lib User32.lib
        DEFINES += HAVE_SAPERA _CRT_SECURE_NO_WARNINGS
        SOURCES += \
            SaperaFG/FPGA.cpp \
            SaperaFG/PagedRingBuffer.cpp \
            SaperaFG/SaperaFG.cpp \
            SaperaFG/SpikeGLHandlerThread.cpp \
            SaperaFG/Thread.cpp

        HEADERS += \
                SaperaFG/SaperaFG.h \
                SaperaFG/CommonIncludes.h\
                SaperaFG/Globals.h \
                SaperaFG/SaperaFG.h \
                SaperaFG/XtCmd.h \
                SaperaFG/FPGA.h \
                SaperaFG/PagedRingBuffer.h \
                SaperaFG/SpikeGLHandlerThread.h \
                SaperaFG/Thread_Win32_Only.h \
                SaperaFG/Thread.h
    } else {
        message("Sapera not found. If you wish to build with Sapera enabled, make sure SaperaDir is defined in the environment.")
    }
} else {
    message("Sapera not available for this platform. Excluding Sapera-dependent Framegrabber components from build.")
}
