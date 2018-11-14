#include "CommonIncludes.h"
#include "SpikeGLHandlerThread.h"
#include <stdio.h>
#include <io.h>
#include <sys/stat.h>
#include <stdio.h>
#include <fcntl.h>
#include <io.h>
#include <stdlib.h>
#include <string.h>

SpikeGLHandlerThread::~SpikeGLHandlerThread() { }
SpikeGLOutThread::~SpikeGLOutThread() { 
    tryToStop(); 
    if (running) kill();
}
SpikeGLInputThread::~SpikeGLInputThread() { 
    tryToStop(); 
    if (running) kill();
}

void SpikeGLHandlerThread::tryToStop() {
    if (running) { pleaseStop = true; wait(300); }
}

bool SpikeGLHandlerThread::pushCmd(const XtCmd *c, DWORD timeout_ms)
{
    if (mut.lock(timeout_ms)) {
        if (nCmd >= maxCommandQ) {
            // todo.. handle command q overflow here!
            mut.unlock();
            return false;
        }
        cmds.push_back(std::vector<BYTE>());
        ++nCmd;
        std::vector<BYTE> & v(cmds.back());
        v.resize(c->len + (((char *)c->data) - (char *)c));
        ::memcpy(&v[0], c, v.size());
        mut.unlock();
        return true;
    }
    return false;
}

XtCmd * SpikeGLHandlerThread::popCmd(std::vector<BYTE> &outBuf, DWORD timeout_ms)
{
    XtCmd *ret = 0;
    if (mut.lock(timeout_ms)) {
        if (nCmd) {
            std::vector<BYTE> & v(cmds.front());
            outBuf.swap(v);
            cmds.pop_front();
            --nCmd;
            if (outBuf.size() >= sizeof(XtCmd)) {
                ret = (XtCmd *)&outBuf[0];
            }
        }
        mut.unlock();
    }
    return ret;
}

int SpikeGLHandlerThread::cmdQSize() const
{
    int ret = -1;
    if (mut.lock()) {
        ret = nCmd;
        mut.unlock();
    }
    return ret;
}

bool SpikeGLOutThread::pushConsoleMsg(const std::string & str, int mtype)
{
    XtCmdConsoleMsg *xt = XtCmdConsoleMsg::allocInit(str, mtype);
    bool ret = pushCmd(xt);
    free(xt);
    return ret;
}

void SpikeGLOutThread::threadFunc()
{
    _setmode(_fileno(stdout), O_BINARY);
    while (!pleaseStop) {
        int ct = 0;
        if (mut.lock(100)) {
            CmdList my;
            my.splice(my.begin(), cmds);
            nCmd = 0;
            mut.unlock();
            for (CmdList::iterator it = my.begin(); it != my.end(); ++it) {
                XtCmd *c = (XtCmd *)&((*it)[0]);
                if (!c->write(stdout)) {
                    // todo.. handle error here...
                }
                ++ct;
            }
            fflush(stdout);
            if (!ct) Sleep(10);
        }
    }
}

void SpikeGLInputThread::threadFunc()
{
    _setmode(_fileno(stdin), O_BINARY);
    std::vector<BYTE> buf;
    XtCmd *xt = 0;
    while (!pleaseStop) {
        if ((xt = XtCmd::read(buf, stdin))) {
            if (!pushCmd(xt)) {
                // todo.. handle error here...
            }
        }
    }
}





