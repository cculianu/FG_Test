// FG_SpikeGL.cpp : Defines the entry point for the console application.
//
#include "SaperaFG.h"
#include "CommonIncludes.h"
#include "Globals.h"
#include "XtCmd.h"
#include "PagedRingBuffer.h"
#include "XtCmdQueue.h"
#include "Util.h"

#include <varargs.h>
#include <utility>

/*static*/ int SaperaFG::PSWDbgFunc(const char *fmt, ...)
{
    char buf[1024];
    va_list l;
    int ret = 0;
    va_start(l, fmt);
    ret = vsnprintf_s(buf, sizeof(buf), fmt, l);
    Debug("%s", buf);
    va_end(l);
    return ret;
}

/*static*/ int SaperaFG::PSWErrFunc(const char *fmt, ...)
{
    char buf[1024];
    va_list l;
    int ret = 0;
    va_start(l, fmt);
    ret = vsnprintf_s(buf, sizeof(buf), fmt, l);
    Error("%s", buf);
    va_end(l);
    return ret;
}

/*static*/ void SaperaFG::acqCallback(SapXferCallbackInfo *info)
{
    SaperaFG *g = reinterpret_cast<SaperaFG *>(info->GetContext());
    if (g) g->acqCallback();
}

void SaperaFG::acqCallback()
{
    if (isFinished()) return;
    if (!gotFirstXferCallback)
        (void)xtOut->pushConsoleDebug("acqCallback called at least once! Yay!"), gotFirstXferCallback = true;
    if (!buffers) {
        xtOut->pushConsoleDebug("INTERNAL ERROR... acqCallback called with 'buffers' pointer NULL!");
        return;
    }
    if (!width) {
        bpp = buffers->GetBytesPerPixel();		// bpp:		get number of bytes required to store a single image
        pitch = buffers->GetPitch();				// pitch:	get number of bytes between two consecutive lines of all the buffer resource
        width = buffers->GetWidth();				// width:	get the width (in pixel) of the image
        height = buffers->GetHeight();				// Height:	get the height of the image
    }
    int w = width, h = height;
    if (w < desiredWidth || h < desiredHeight) {
        char tmp[512];
        _snprintf_c(tmp, sizeof(tmp), "acqCallback got a frame of size %dx%d, but expected a frame of size %dx%d", w, h, desiredWidth, desiredHeight);
        xtOut->pushConsoleError(tmp);
        xfer->Abort();
        return;
    }
    if (w > desiredWidth) w = desiredWidth;
    if (h > desiredHeight) h = desiredHeight;
    size_t len = size_t(w*h);

    const size_t oneScanBytes = nChansPerScan * sizeof(short);

    if (!sharedMemory) {
        xtOut->pushConsoleError("INTERNAL ERROR.. not attached to shm, cannot send frames!");
        xfer->Abort();
        return;
    }
    if (!writer->scansPerPage()) {
        xtOut->pushConsoleError("INTERNAL ERROR.. shm page, cannot fit at least 1 scan! FIXME!");
        xfer->Abort();
        return;
    }

    size_t nScansInFrame = (len + (extraAI ? 4 : 0)) / oneScanBytes;

    if (!nScansInFrame) {
        xtOut->pushConsoleError("Frame must contain at least 1 full scan! FIXME!");
        xfer->Abort();
        return;
    }

    const short *pData = nullptr;

    buffers->GetAddress(const_cast<void **>(reinterpret_cast<const void **>(&pData)));			// Get image buffer start memory address.

    if (!pData) {
        xtOut->pushConsoleError("SapBuffers::GetAddress() returned a NULL pointer!");
        xfer->Abort();
        return;
    }
#define DUMP_FRAMES 0
#if     DUMP_FRAMES
    double t0write = getTime(); /// XXX
    static double tLastPrt = 0.; /// XXX
#endif

    if (pitch != w) {
        // WARNING:
        // THIS IS A HACK TO SUPPORT THE FPGA FRAMESaperaFG FORMAT AND IS VERY SENSITIVE TO THE EXACT
        // LAYOUT OF DATA IN THE FRAMESaperaFG!! EDIT THIS CODE IF THE LAYOUT CHANGES AND/OR WE NEED SOMETHING
        // MORE GENERIC!

        // pith != w, so write scan by taking each valid row of the frame... using our new writer->writePartial() method
        // note we only support basically frames where pitch is exactly 8 bytes larger than width, because these are from the FPGA
        // and we take the ENTIRE line (all the way up to pitch bytes) for data.  The first 8 bytes in the line is the timestamp
        // See emails form Jim Chen in March 2016
        if (pitch - w != 8) {
            xtOut->pushConsoleError("Unsupported frame format! We are expecting a frame where pitch is 8 bytes larger than width!");
            buffers->ReleaseAddress(const_cast<void *>(reinterpret_cast<const void *>(pData)));
            xfer->Abort();
            return;
        }
        if (nScansInFrame != 1) {
            xtOut->pushConsoleError("Unsupported frame format! We are expecting a frame where there is exactly one complete scan per frame!");
            buffers->ReleaseAddress(const_cast<void *>(reinterpret_cast<const void *>(pData)));
            xfer->Abort();
            return;
        }

        const char *pc = reinterpret_cast<const char *>(pData);
#if DUMP_FRAMES
        // HACK TESTING HACK
        static FILE *f = 0; static bool tryfdump = true;
        if ( tryfdump ) {
            if (!f) f = fopen("c:\\frame.bin", "wb");
            if (!f) {
                spikeGL->pushConsoleError("Could not open d:\\frame.bin");
                tryfdump = false;
            }
            else {
                size_t s = fwrite(pc, pitch*height, bpp, f);
                if (!s) {
                    spikeGL->pushConsoleError("Error writing frame to d:\\frmae.bin");
                }
                else if (t0write - tLastPrt > 1.0) {
                    spikeGL->pushConsoleDebug("Appended frame to d:\\frame.bin");
                    fflush(f);
                    tLastPrt = t0write;
                }
            }
        }
        // /HACK
#endif
        writer->writePartialBegin();
        for (int line = 0; line < h; ++line) {
            //            if (1) { // no swab
            bool lastIter = false;
            if ((lastIter = (line + 1 == h))) {
                metaPtrCur() = *reinterpret_cast<const unsigned int *>(pc + line*pitch);
                metaPtrInc();
            }
            if (!writer->writePartial(pc + /* <HACK> */ 8 /* </HACK> */ + (line*pitch), unsigned(w), metaPtr)) {
                xtOut->pushConsoleError("PagedScanWriter::writePartial() returned false!");
                buffers->ReleaseAddress(const_cast<void *>(reinterpret_cast<const void *>(pData)));
                writer->writePartialEnd();
                xfer->Abort();
                return;
            }
            if (extraAI && lastIter) {
                const unsigned short *ais_in = reinterpret_cast<const unsigned short *>(pc + 4 + line*pitch);
                short ais[2];
                ais[0] = static_cast<short>(int(ais_in[0]) - 32768); // convert unsigned to signed 16 bit
                ais[1] = static_cast<short>(int(ais_in[1]) - 32768); // ditto
                if (!writer->writePartial(reinterpret_cast<const char *>(ais), 4, metaPtr)) {
                    xtOut->pushConsoleError("PagedScanWriter::writePartial() for AI chans returned false!");
                    writer->writePartialEnd();
                    xfer->Abort();
                    return;
                }
            }
            /*           } else { // swab
                if (line + 1 == h) {
                    unsigned long long mts = *reinterpret_cast<const unsigned long long *>(pc + line*pitch);
                    mts = ((mts >> 56) & 0x00000000000000ff) | ((mts >> 40) & 0x000000000000ff00) | ((mts >> 24) & 0x0000000000ff0000) | ((mts >> 8) & 0x00000000ff000000) | ((mts << 8) & 0x000000ff00000000) | ((mts << 24) & 0x0000ff0000000000) | ((mts << 40) & 0x00ff000000000000) | ((mts << 56) & 0xff000000000000);
                    metaPtrCur() = mts;
                    metaPtrInc();
                }
                static char btmp[262144]; // testing hacky buffer
                for (int i = 0; i < w; i += 2) {
                    btmp[i] = pc[8 + (line*pitch) + i+1];
                    btmp[i+1] = pc[8 + (line*pitch) + i];
                }
                if (!writer->writePartial(btmp, w, metaPtr)) {
                    spikeGL->pushConsoleError("PagedScanWriter::writePartial() returned false!");
                    buffers->ReleaseAddress((void *)pData);
                    writer->writePartialEnd();
                    xfer->Abort();
                    return;
                }
            }
*/
        }

        if (!writer->writePartialEnd()) {
            xtOut->pushConsoleError("PagedScanWriter::writePartialEnd() returned false!");
            buffers->ReleaseAddress(const_cast<void *>(reinterpret_cast<const void *>(pData)));
            xfer->Abort();
            return;
        }
    } else {
        // NOTE: this case is calin's test code.. we fudge the metadata .. even though it makes no sense at all.. to make sure SpikeGL is reading it properly
        metaIdx = metaMaxIdx - 1;
        metaPtrCur() = static_cast<unsigned int>(frameNum);  // HACK!

        /*
        // test channel reordering here..
        short *scan = (short *)pData;
        while ( (scan - ((short *)pData))/nChansPerScan < (int)nScansInFrame) {
            for (int i = 0; i < (int)nChansPerScan; ++i)
                scan[i] = i + 1;
            scan += nChansPerScan;
        }
        // end testing..
        */

        if (!writer->write(pData, unsigned(nScansInFrame),metaPtr)) {
            xtOut->pushConsoleError("PagedScanWriter::write returned false!");
            buffers->ReleaseAddress(const_cast<void *>(reinterpret_cast<const void *>(pData))); // Need to release it to return it to the hardware!
            xfer->Abort();
            return;
        }
    }

    //double tfwrite = getTime(); /// XXX

    buffers->ReleaseAddress(const_cast<void *>(reinterpret_cast<const void *>(pData))); // Need to release it to return it to the hardware!


    /// FPS RATE CODE
    static double lastTS = 0., lastPrt = -1.0;
    static int ctr = 0;
    ++ctr;
    ++frameNum;
    //static double tWriteSum = 0.; /// XXX
    //tWriteSum += tfwrite - t0write; /// XXX

    double now = getTime();
    if (frameNum > 1 && now - lastPrt >= 1.0) {
        double rate = 1.0 / ((now - lastPrt)/ctr);
        /// xxx
        //char tmp[512];
        //_snprintf_c(tmp, sizeof(tmp), "DEBUG: frame %u - avg.twrite: %2.6f ms (avg of %d frames)", (unsigned)frameNum, tWriteSum/double(ctr) * 1e3, ctr);
        //spikeGL->pushConsoleDebug(tmp);
        //tWriteSum = 0.;
        /// /XXX
        lastPrt = now;
        ctr = 0;
        XtCmdFPS fps;
        fps.init(rate);
        xtOut->pushCmd(&fps);
    }
    lastTS = now;

}

/*static*/ void SaperaFG::startFrameCallback(SapAcqCallbackInfo *info)
{
    SaperaFG *g = reinterpret_cast<SaperaFG *>(info->GetContext());
    if (g) g->startFrameCallback();
}

void SaperaFG::startFrameCallback()
{
    if (isFinished()) return;
    if (!gotFirstStartFrameCallback)
        (void)xtOut->pushConsoleDebug("'startFrameCallback' called at least once! Yay!"), gotFirstStartFrameCallback = true;
}

void SaperaFG::freeSapHandles()
{
    if (xfer && *xfer) xfer->Destroy();
    if (buffers && *buffers) buffers->Destroy();
    if (acq && *acq) acq->Destroy();
    if (xfer) (void)delete xfer, xfer = nullptr;
    if (buffers) (void)delete buffers, buffers = nullptr;
    if (acq) (void)delete acq, acq = nullptr;
    bpp = pitch = width = height = 0;
    gotFirstStartFrameCallback = gotFirstXferCallback = false;
}

/*static*/ void SaperaFG::sapAcqCallback(SapAcqCallbackInfo *p)
{
    SaperaFG *g = p ? reinterpret_cast<SaperaFG *>(p->GetContext()) : nullptr;
    if (g) g->sapAcqCallback(p->GetEventType());
}

void SaperaFG::sapAcqCallback(quint64 t)
{
    if (isFinished()) return;
    if (xtOut) {
        char buf[2048];
        _snprintf_c(buf, sizeof(buf), "SAP ACQ CALLBACK CALLED WITH eventtype=%d", int(t));
        xtOut->pushConsoleDebug(buf);
    }
    std::string msg = "";
    //SapAcquisition::EventNoPixelClk|SapAcquisition::EventFrameLost|SapAcquisition::EventPixelClk|SapAcquisition::EventDataOverflow
    if (t & SapAcquisition::EventPixelClk) msg = msg + " Pixel Clock Resumed!";
    if (t & SapAcquisition::EventNoPixelClk) msg = msg + " No Pixel Clock!";
    if (t & SapAcquisition::EventFrameLost) msg = msg + " Frame Lost!";
    if (t & SapAcquisition::EventDataOverflow) msg = msg + " Data Overflow!";
    if (!msg.size()) return;
    if (xtOut) {
        xtOut->pushConsoleError(std::string("(SAP Acq Event) ") + msg);
    } else
        fprintf(stderr, "(SAP Acq Event) %s\n", msg.c_str());
}

/*static*/ void SaperaFG::sapStatusCallback(SapManCallbackInfo *p)
{
    SaperaFG *g = p ? reinterpret_cast<SaperaFG *>(p->GetContext()) : nullptr;
    if (g) {
        std::string errorMessage = "";
        quint64 t = 0ULL;
        if (p->GetErrorMessage() && *(p->GetErrorMessage())) {
            errorMessage = p->GetErrorMessage();
        }
        else {
            t = quint64(p->GetEventType());
        }
        g->sapStatusCallback(t, errorMessage);
    }
}

void SaperaFG::sapStatusCallback(quint64 t, const std::string &errorMessage)
{
    if (isFinished()) return;
    if (!errorMessage.empty()) {
        if (xtOut) {
            xtOut->pushConsoleDebug(std::string("(SAP Status) ") + errorMessage);
        } else {
            fprintf(stderr, "(SAP Status) %s\n", errorMessage.c_str());
        }
    }
    else {
        std::string msg = "";
        //SapAcquisition::EventNoPixelClk|SapAcquisition::EventFrameLost|SapAcquisition::EventPixelClk|SapAcquisition::EventDataOverflow
        if (t & SapAcquisition::EventPixelClk) msg = msg + " Pixel Clock Resumed!";
        if (t & SapAcquisition::EventNoPixelClk) msg = msg + " No Pixel Clock!";
        if (t & SapAcquisition::EventFrameLost) msg = msg + " Frame Lost!";
        if (t & SapAcquisition::EventDataOverflow) msg = msg + " Data Overflow!";
        if (!msg.size()) return;
        if (xtOut) {
            xtOut->pushConsoleError(std::string("(SAP Acq Event) ") + msg);
        }
        else
            fprintf(stderr, "(SAP Acq Event) %s\n", msg.c_str());
    }
}

// Resets server.. as per Jim Chen's recommendation 2/18/2016 email
void SaperaFG::resetHardware(int serverIndex, int timeout_ms  /* default 3.5 sec reset timeout */)
{
    char tmp[512];
    SapManager::SetResetTimeout(timeout_ms);
    if (!SapManager::ResetServer(serverIndex, 1)) {
        _snprintf_c(tmp, sizeof(tmp), "Reset hardware device %d: FAIL (Sapera Error: '%s')", serverIndex, SapManager::GetLastStatus());
        xtOut->pushConsoleWarning(tmp);
    } else {
        _snprintf_c(tmp, sizeof(tmp), "Reset hardware device %d: success", serverIndex);
        xtOut->pushConsoleMsg(tmp);
    }
}

bool SaperaFG::setupAndStartAcq()
{
    SapManager::SetDisplayStatusMode(SapManager::StatusCallback, sapStatusCallback, this); // so we get errors reported properly from SAP

    freeSapHandles();

    metaBuffer.resize(shmMetaSize,0);
    metaPtr = shmMetaSize ? reinterpret_cast<unsigned int *>(&metaBuffer[0]) : nullptr;
    metaIdx = 0; metaMaxIdx = shmMetaSize / sizeof(unsigned int);

    if (!sharedMemory) {
        char tmp[512];

        hShm = OpenFileMapping(
                    FILE_MAP_ALL_ACCESS,   // read/write access
                    FALSE,                 // do not inherit the name
                    tcharify(shmName));               // name of mapping object


        if (!hShm ) {
            _snprintf_c(tmp, sizeof(tmp), "Could not open shared memory (%d).", GetLastError());
            xtOut->pushConsoleError(tmp);
            return false;
        }

        sharedMemory = MapViewOfFile(
                    hShm,
                    FILE_MAP_ALL_ACCESS,  // read/write permission
                    0,
                    0,
                    shmSize);

        if (!sharedMemory) {
            _snprintf_c(tmp, sizeof(tmp), "Could not map shared memory (%d).", GetLastError());
            xtOut->pushConsoleError(tmp);
            CloseHandle(hShm); hShm = nullptr;
            return false;
        }
        if (writer) delete writer;
        writer = new PagedScanWriter(nChansPerScan, shmMetaSize, sharedMemory, shmSize, shmPageSize, chanMapping);
        writer->ErrFunc = &PSWErrFunc; writer->DbgFunc = &PSWDbgFunc;
        _snprintf_c(tmp, sizeof(tmp), "Connected to shared memory \"%s\" size: %u  pagesize: %u metadatasize: %u", shmName.c_str(), shmSize, shmPageSize, shmMetaSize);
        xtOut->pushConsoleDebug(tmp);
    }

    if (serverIndex < 0) serverIndex = 1;
    if (resourceIndex < 0) resourceIndex = 0;

    char acqServerName[128], acqResName[128];
    char tmp[512];

    SapLocation loc(serverIndex, resourceIndex);
    SapManager::GetServerName(serverIndex, acqServerName, sizeof(acqServerName));
    SapManager::GetResourceName(loc, SapManager::ResourceAcq, acqResName, sizeof(acqResName));
    _snprintf_c(tmp, sizeof(tmp), "Server name: %s   Resource name: %s  ConfigFile: %s", acqServerName, acqResName, configFilename.c_str());
    xtOut->pushConsoleDebug(tmp);

    if (SapManager::GetResourceCount(acqServerName, SapManager::ResourceAcq) > 0)
    {
        int nbufs = NUM_BUFFERS(); if (nbufs < 2) nbufs = 2;

        acq = new SapAcquisition(loc, configFilename.c_str(),
                                 static_cast<SapAcquisition::EventType>(SapAcquisition::EventNoPixelClk|SapAcquisition::EventFrameLost|SapAcquisition::EventPixelClk|SapAcquisition::EventDataOverflow/*|SapAcquisition::EventCameraBufferOverrun|SapAcquisition::EventCameraMissedTrigger|SapAcquisition::EventExternalTriggerIgnored|SapAcquisition::EventExtLineTriggerTooSlow|SapAcquisition::EventExternalTriggerTooSlow|SapAcquisition::EventLineTriggerTooFast|SapAcquisition::EventVerticalTimeout*/),
                                 sapAcqCallback, this);
        buffers = new SapBufferWithTrash(nbufs, acq);
        xfer = new SapAcqToBuf(acq, buffers, acqCallback, this);

        _snprintf_c(tmp, sizeof(tmp), "Will use buffer memory: %dMB in %d sapbufs", BUFFER_MEMORY_MB, nbufs);
        xtOut->pushConsoleDebug(tmp);

        // Create acquisition object
        if (acq && !*acq && !acq->Create()) {
            xtOut->pushConsoleError("Failed to Create() acquisition object");
            freeSapHandles();
            return false;
        }
    } else  {
        xtOut->pushConsoleError("GetResourceCount() returned <= 0");
        freeSapHandles();
        return false;
    }

    //register an acquisition callback
    if (acq)
        acq->RegisterCallback(SapAcquisition::EventStartOfFrame, startFrameCallback, this);

    // Create buffer object
    if (buffers && !*buffers && !buffers->Create()) {
        xtOut->pushConsoleError("Failed to Create() buffers object");
        freeSapHandles();
        return false;
    }

    // Create transfer object
    if (xfer && !*xfer && !xfer->Create()) {
        xtOut->pushConsoleError("Failed to Create() xfer object");
        freeSapHandles();
        return false;
    }


    // Start continous grab
    return !!(xfer->Grab());
}

void SaperaFG::handleXtCommand(XtCmd *xt)
{
    (void)xt;
    switch (xt->cmd) {
    case XtCmd_Test:
        xtOut->pushConsoleDebug("Got 'TEST' command, replying with this debug message!");
        break;
    case XtCmd_GrabFrames: {
        XtCmdGrabFrames *x = static_cast<XtCmdGrabFrames *>(xt);
        xtOut->pushConsoleDebug("Got 'GrabFrames' command");
        if (x->ccfFile[0]) configFilename = x->ccfFile;
        if (x->frameH > 0) desiredHeight = x->frameH;
        if (x->frameW > 0) desiredWidth = x->frameW;
        if (x->numChansPerScan > 0) nChansPerScan = unsigned(x->numChansPerScan);
        else {
            nChansPerScan = 1;
            xtOut->pushConsoleWarning("FIXME: nChansPerScan was not specified in XtCmd_GrabFrames!");
        }
        if (x->shmPageSize <= 0 || x->shmSize <= 0 || !x->shmName[0]) {
            xtOut->pushConsoleError("FIXME: shmPageSize,shmName,and shmSize need to be specified in XtCmd_GrabFrames!");
            break;
        }
        if (x->shmPageSize > x->shmSize) {
            xtOut->pushConsoleError("FIXME: shmPageSize cannot be > shmSize in XtCmd_GrabFrames!");
            break;
        }
        shmPageSize = unsigned(x->shmPageSize);
        shmSize = unsigned(x->shmSize);
        shmName = x->shmName;
        shmMetaSize = unsigned(x->shmMetaSize);
        chanMapping.clear();
        if (x->use_map) {
            chanMapping.resize(nChansPerScan);
            const int maxsize = sizeof(x->mapping) / sizeof(*x->mapping);
            if (chanMapping.size() > maxsize) chanMapping.resize(maxsize);
            memcpy(&chanMapping[0], x->mapping, chanMapping.size()*sizeof(int));
        }
        extraAI = x->use_extra_ai;
        if (!setupAndStartAcq())
            xtOut->pushConsoleWarning("Failed to start acquisition.");
        break;
    }
    case XtCmd_ServerResource: {
        XtCmdServerResource *x = static_cast<XtCmdServerResource *>(xt);
        xtOut->pushConsoleDebug("Got 'ServerResource' command");
        if (x->serverIndex < 0 || x->resourceIndex < 0) {
            probeHardware();
        } else {
            serverIndex = x->serverIndex;
            resourceIndex = x->resourceIndex;
            char buf[128];
            _snprintf_c(buf, sizeof(buf), "Setting serverIndex=%d resourceIndex=%d", serverIndex, resourceIndex);
            xtOut->pushConsoleDebug(buf);
        }
    }
        break;
    default: // ignore....?
        break;
    }
}

void SaperaFG::listServerResources() ///< will set up a probeHardware() and emit serverResource() for each piece of hardware found sometime later. Returns immediately.
{
    XtCmdServerResource xt;
    xt.init("", "", -1, -1, 0, false);
    xtIn->pushCmd(&xt); // will get handled in thread
}

void SaperaFG::selectServerResource(int serverIndex, int resourceIndex) ///< returns immediately. selects the given camera for the next startAcq call.
{
    XtCmdServerResource xt;
    xt.init("", "", serverIndex, resourceIndex, 0, false);
    xtIn->pushCmd(&xt); // will get handled in thread
}

void SaperaFG::publishSignalStatus()
{
    if (!acq) return;
    static double tLast = 0.;

    if (getTime() - tLast > 0.25) {
        BOOL PixelCLKSignal1, PixelCLKSignal2, PixelCLKSignal3, HSyncSignal, VSyncSignal;
        if (
                acq->GetSignalStatus(SapAcquisition::SignalPixelClk1Present, &PixelCLKSignal1)
                && acq->GetSignalStatus(SapAcquisition::SignalPixelClk2Present, &PixelCLKSignal2)
                && acq->GetSignalStatus(SapAcquisition::SignalPixelClk3Present, &PixelCLKSignal3)
                && acq->GetSignalStatus(SapAcquisition::SignalHSyncPresent, &HSyncSignal)
                && acq->GetSignalStatus(SapAcquisition::SignalVSyncPresent, &VSyncSignal)
                )
        {
            XtCmdClkSignals cmd;
            cmd.init(!!PixelCLKSignal1, !!PixelCLKSignal2, !!PixelCLKSignal3, !!HSyncSignal, !!VSyncSignal);
            xtOut->pushCmd(&cmd);
        }
        tLast = getTime();
    }
}

void SaperaFG::probeHardware()
{
    char buf[512];
    SapManager::SetDisplayStatusMode(SapManager::StatusCallback, sapStatusCallback, this); // so we get errors reported properly from SAP
    int nServers = SapManager::GetServerCount();
    _snprintf_c(buf, sizeof(buf), "ServerCount: %d", nServers);
    if (xtOut) xtOut->pushConsoleDebug(buf);
    else fprintf(stderr, "%s\n", buf);
    for (int i = 0; i < nServers; ++i) {
        resetHardware(i);
        int nRes;
        if ((nRes = SapManager::GetResourceCount(i, SapManager::ResourceAcq)) > 0) {
            char sname[64]; int type; BOOL accessible = SapManager::IsServerAccessible(i);
            if (!SapManager::GetServerName(i, sname)) continue;
            type = SapManager::GetServerType(i);
            for (int j = 0; j < nRes; ++j) {
                char rname[64];
                if (!SapManager::GetResourceName(i, SapManager::ResourceAcq, j, rname, sizeof(rname))) continue;
                _snprintf_c(buf, sizeof(buf), "#%d,%d \"%s\" - \"%s\", type %d accessible: %s",i,j,sname,rname,type,accessible ? "yes" : "no");
                if (xtOut) xtOut->pushConsoleDebug(buf);
                else fprintf(stderr, "%s\n", buf);
                if (xtOut) {
                    XtCmdServerResource r;
                    r.init(sname, rname, i, j, type, !!accessible);
                    xtOut->pushCmd(&r);
                }
            }
        }
    }
}

/*static*/ double SaperaFG::getTime()
{
    static int64_t freq = 0;
    static int64_t t0 = 0;
    int64_t ct;

    if (!freq) {
        QueryPerformanceFrequency(reinterpret_cast<LARGE_INTEGER *>(&freq));
    }
    QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER *>(&ct));   // reads the current time (in system units)
    if (!t0) {
        t0 = ct;
    }
    return double(ct - t0) / double(freq);
}

SaperaFG::SaperaFG(const std::string &ccf_in)
{
    thr.setObjectName("SaperaFG");

    if (!ccf_in.empty()) configFilename = ccf_in;

    // NB: it's vital these two objects get constructed before any other calls.. since other code assumes they are valid and may call these objects' methods
    xtOut = new XtCmdQueueOut(this); // these will "live" in the same thread as us
    xtIn = new XtCmdQueueIn(this);

    // communication from Outside World -> this class
    connect(xtIn, &XtCmdQueue::gotCmd, this, &SaperaFG::gotCmd);
    // communication from this class -> Outside World
    connect(xtOut, &XtCmdQueue::gotCmd, this, &SaperaFG::translateOutCmd);

    xtOut->pushConsoleMsg("SaperaFG slave started.");
}

SaperaFG::~SaperaFG()
{
    stop(); // required because we have dependent QObjects
    freeSapHandles();
    (void)delete xtOut, xtOut = nullptr;
    (void)delete xtIn, xtIn = nullptr;
    (void)delete writer, writer = nullptr;
    if (sharedMemory) {
        UnmapViewOfFile(sharedMemory);
        sharedMemory = nullptr;
    }
    if (hShm) (void)CloseHandle(hShm), hShm = nullptr;
}

void SaperaFG::gotCmd(XtCmdQueue *q)
{
    XtCmdQueue::Buffer buf;
    XtCmd *xt = nullptr;
    while ((xt = q->popCmd(buf))) {
        handleXtCommand(xt);
    }
    publishSignalStatus();
}

void SaperaFG::translateOutCmd(XtCmdQueue *q)
{
    XtCmdQueue::Buffer buf;
    XtCmd *xt = nullptr;
    while ((xt = q->popCmd(buf))) {
        switch(xt->cmd) {
        case XtCmd_ClkSignals: {
            auto x = static_cast<XtCmdClkSignals *>(xt);
            emit clockSignals(x->isPxClk1(), x->isPxClk2(), x->isPxClk3(), x->isHSync(), x->isVSync());
        }
            break;
        case XtCmd_FPS: {
            auto x = static_cast<XtCmdFPS *>(xt);
            emit fps(double(x->param));
        }
            break;
        case XtCmd_ServerResource: {
            auto x = static_cast<XtCmdServerResource *>(xt);
            emit serverResource(x->serverName, x->resourceName, x->serverIndex, x->resourceIndex, x->serverType, x->accessible);
        }
            break;
        case XtCmd_ConsoleMessage: {
            auto x = static_cast<XtCmdConsoleMsg *>(xt);
            const char * message = static_cast<char *>(&x->msg[0]);
            if (message[0]) {
                if (x->msgType == XtCmdConsoleMsg::Normal)
                    Log("%s", message);
                else if (x->msgType == XtCmdConsoleMsg::Warning)
                    Warning("%s", message);
                else if (x->msgType == XtCmdConsoleMsg::Error)
                    Error("%s", message);
                else
                    Error("(UNKNWON MESSAGE TYPE) %s", message);
            }
        }
            break;
        default:
            Error() << "Got unhandled OUT command: " << xt->cmd;
        }
    }
}
