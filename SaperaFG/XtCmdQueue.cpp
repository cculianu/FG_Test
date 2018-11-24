#include "CommonIncludes.h"
#include "XtCmdQueue.h"
#include <string.h>

XtCmdQueue::~XtCmdQueue() { }
XtCmdQueueOut::~XtCmdQueueOut() { }
XtCmdQueueIn::~XtCmdQueueIn() { }

bool XtCmdQueue::pushCmd(const XtCmd *c, int timeout_ms)
{
    if (mut.tryLock(int(timeout_ms))) {
        if (nCmd >= maxCommandQ) {
            // todo.. handle command q overflow here!
            mut.unlock();
            return false;
        }
        cmds.push_back(Buffer());
        ++nCmd;
        Buffer & v(cmds.back());
        v.resize(static_cast<Buffer::size_type>(c->len + (c->data - reinterpret_cast<const Byte *>(c))));
        ::memcpy(&v[0], c, v.size());
        mut.unlock();
        emit gotCmd(this);
        return true;
    }
    return false;
}

XtCmd * XtCmdQueue::popCmd(Buffer &outBuf, int timeout_ms)
{
    XtCmd *ret = nullptr;
    if (mut.tryLock(int(timeout_ms))) {
        if (nCmd) {
            Buffer & v(cmds.front());
            outBuf.swap(v);
            cmds.pop_front();
            --nCmd;
            if (outBuf.size() >= sizeof(XtCmd)) {
                ret = reinterpret_cast<XtCmd *>(&outBuf[0]);
            }
        }
        mut.unlock();
    }
    return ret;
}

int XtCmdQueue::cmdQSize() const
{
    mut.lock();
    int ret = nCmd;
    mut.unlock();
    return ret;
}

bool XtCmdQueueOut::pushConsoleMsg(const std::string & str, int mtype)
{
    XtCmdConsoleMsg *xt = XtCmdConsoleMsg::allocInit(str, mtype);
    bool ret = pushCmd(xt);
    free(xt);
    return ret;
}
