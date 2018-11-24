#ifndef SAPERA_FG_H
#define SAPERA_FG_H
#include "WorkerThread.h"
#include <functional>
#include <string>
#include <vector>
#include <Windows.h>

class SapAcquisition;
class SapBuffer;
class SapTransfer;
class SapXferCallbackInfo;
class SapAcqCallbackInfo;
class SapManCallbackInfo;
class FPGA;
class SpikeGLOutThread;
class SpikeGLInputThread;
class PagedScanWriter;
struct XtCmd;


class SaperaFG : public WorkerThread {
    Q_OBJECT
public:
    SaperaFG(const std::string & configFileName = "");
    ~SaperaFG();

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
    void handleSpikeGLCommand(XtCmd *xt);
    void tellSpikeGLAboutSignalStatus();
    void probeHardware();

private:
    static SaperaFG *instance;
    SapAcquisition *acq = nullptr;
    SapBuffer      *buffers = nullptr;
    SapTransfer    *xfer = nullptr;
    std::string configFilename = "J_2000+_Electrode_8tap_8bit.ccf";
    int serverIndex = -1, resourceIndex = -1;

    SpikeGLOutThread    *spikeGL = nullptr;
    SpikeGLInputThread  *spikeGLIn = nullptr;
    UINT_PTR timerId = 0;
    bool gotFirstXferCallback = false, gotFirstStartFrameCallback = false;
    int bpp, pitch, width=0, height=0;

    FPGA *fpga = nullptr;

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
