#ifndef SAPERA_FG_H
#define SAPERA_FG_H
#include "WorkerThread.h"
#include <functional>
#include <string>
#include <vector>
#include "CommonIncludes.h"

class XtCmdQueue;
class XtCmdQueueOut;
class XtCmdQueueIn;
class PagedScanWriter;
struct XtCmd;

class SaperaFG : public WorkerThread {
    Q_OBJECT
public:
    SaperaFG(const std::string & configFileName = "");
    ~SaperaFG();

public slots:
    void listServerResources(); ///< will set up a probeHardware() and emit serverResource() for each piece of hardware found sometime later. Returns immediately.
    void selectServerResource(int serverIndex, int resourceIndex); ///< returns immediately. selects the given camera for the next startAcq call.

signals:
    void fps(double);
    void clockSignals(bool pixClk1, bool pixClk2, bool pixClk3, bool hsync, bool vsync);
    void serverResource(QString serverName, QString resourceName, int serverIndex, int resourceIndex, int serverType, bool accessible);

private:
    static double getTime();

    inline void metaPtrInc() { if (++metaIdx >= metaMaxIdx) metaIdx = 0; }
    inline unsigned int & metaPtrCur() {
        static unsigned int dummy = 0;
        if (metaPtr && metaIdx < metaMaxIdx) return metaPtr[metaIdx];
        return dummy;
    }

    static int PSWDbgFunc(const char *fmt, ...);
    static int PSWErrFunc(const char *fmt, ...);

    static void acqCallback(SapXferCallbackInfo *info);
    void acqCallback();
    static void startFrameCallback(SapAcqCallbackInfo *info);
    void startFrameCallback();
    static void sapAcqCallback(SapAcqCallbackInfo *p);
    void sapAcqCallback(quint64 eventType);
    static void sapStatusCallback(SapManCallbackInfo *p);
    void sapStatusCallback(quint64 eventType, const std::string &errorMessage);

    void freeSapHandles();

    void resetHardware(int serverIndex, int timeout_ms = 3500 /* default 3.5 sec reset timeout */);
    bool setupAndStartAcq();
    void handleXtCommand(XtCmd *xt);
    void publishSignalStatus();
    void probeHardware();

    void gotCmd(XtCmdQueue *q);
    void translateOutCmd(XtCmdQueue *q);

private:
    SapAcquisition *acq = nullptr;
    SapBuffer      *buffers = nullptr;
    SapTransfer    *xfer = nullptr;
    std::string configFilename = "J_2000+_Electrode_8tap_8bit.ccf";
    int serverIndex = -1, resourceIndex = -1;

    XtCmdQueueOut    *xtOut = nullptr;
    XtCmdQueueIn  *xtIn = nullptr;
    UINT_PTR timerId = 0;
    bool gotFirstXferCallback = false, gotFirstStartFrameCallback = false;
    int bpp, pitch, width=0, height=0;

    int desiredWidth = 144;
    int desiredHeight = 32;
    unsigned long long frameNum = 0;

    std::string shmName = "COMES_FROM_XtCmd";
    unsigned shmSize = 0, shmPageSize = 0;
    unsigned shmMetaSize = 0;
    std::vector<char> metaBuffer; unsigned int *metaPtr = nullptr; int metaIdx = 0, metaMaxIdx = 0;
    std::vector<int> chanMapping;

    void *sharedMemory = nullptr;
    PagedScanWriter *writer = nullptr;
    unsigned nChansPerScan = 0;
    bool extraAI = false;

    HANDLE hShm = nullptr;
};
#endif
