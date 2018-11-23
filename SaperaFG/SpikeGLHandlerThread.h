#ifndef SpikeGLHandlerThread_H
#define SpikeGLHandlerThread_H

#include "XtCmd.h"
#include "Thread.h"
#include <list>
#include <vector>
#include <atomic>

class SpikeGLHandlerThread : public Thread
{
public:
    std::atomic_bool pleaseStop = false;
    const int maxCommandQ = 3072;

    SpikeGLHandlerThread()  {}
    virtual ~SpikeGLHandlerThread();

    bool pushCmd(const XtCmd *c, DWORD timeout_ms = INFINITE);
    XtCmd * popCmd(std::vector<BYTE> &outBuf, DWORD timeout_ms = INFINITE);
    int cmdQSize() const;

protected:
    void tryToStop();
    virtual void threadFunc() = 0;
    typedef std::list<std::vector<BYTE> > CmdList;
    CmdList cmds;
    std::atomic_int nCmd = 0;
    mutable Mutex mut;
};

class SpikeGLOutThread : public SpikeGLHandlerThread
{
public:
    SpikeGLOutThread()  {}
    ~SpikeGLOutThread();

    bool pushConsoleMsg(const std::string & msg, int mtype = XtCmdConsoleMsg::Normal);
    bool pushConsoleDebug(const std::string & msg) { return pushConsoleMsg(msg, XtCmdConsoleMsg::Debug); }
    bool pushConsoleError(const std::string & msg) { return pushConsoleMsg(msg, XtCmdConsoleMsg::Error); }
    bool pushConsoleWarning(const std::string & msg) { return pushConsoleMsg(msg, XtCmdConsoleMsg::Warning); }

protected:
    void threadFunc();
};

class SpikeGLInputThread : public SpikeGLHandlerThread
{
public:
    SpikeGLInputThread()  {}
    ~SpikeGLInputThread();
protected:
    void threadFunc();
};

#endif
