#ifndef SpikeGLHandlerThread_H
#define SpikeGLHandlerThread_H

#include "CommonIncludes.h"
#include "XtCmd.h"
#include <QMutex>
#include <list>
#include <vector>
#include <atomic>
#include <QObject>

class XtCmdQueue : public QObject
{
    Q_OBJECT
public:
    static constexpr int maxCommandQ = 3072;
    typedef quint8 Byte;
    typedef std::vector<Byte> Buffer;

    XtCmdQueue(QObject *parent = nullptr) : QObject(parent)  {}
    ~XtCmdQueue() override;

    bool pushCmd(const XtCmd *c, int timeout_ms = -1); ///< can be called from any thread. will emit gotCmd(this)
    XtCmd * popCmd(Buffer &outBuf, int timeout_ms = -1); ///< can be called from any thread.
    int cmdQSize() const; ///< can be called from any thread

signals:
    void gotCmd(XtCmdQueue *self);

protected:
    typedef std::list<Buffer> CmdList;
    CmdList cmds;
    std::atomic_int nCmd = 0;
    mutable QMutex mut;
};

class XtCmdQueueOut : public XtCmdQueue
{
public:
    XtCmdQueueOut(QObject *parent = nullptr) : XtCmdQueue(parent)  {}
    ~XtCmdQueueOut() override;

    bool pushConsoleMsg(const std::string & msg, int mtype = XtCmdConsoleMsg::Normal);
    bool pushConsoleDebug(const std::string & msg) { return pushConsoleMsg(msg, XtCmdConsoleMsg::Debug); }
    bool pushConsoleError(const std::string & msg) { return pushConsoleMsg(msg, XtCmdConsoleMsg::Error); }
    bool pushConsoleWarning(const std::string & msg) { return pushConsoleMsg(msg, XtCmdConsoleMsg::Warning); }
};

class XtCmdQueueIn : public XtCmdQueue
{
public:
    XtCmdQueueIn(QObject *parent = nullptr) : XtCmdQueue(parent)  {}
    ~XtCmdQueueIn() override;
};

#endif
