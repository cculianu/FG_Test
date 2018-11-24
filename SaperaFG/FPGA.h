#pragma once
#include "CommonIncludes.h"
#include "Globals.h"
class XtCmdQueueOut;

class FPGA
{
public:
    FPGA(const int parms[6], XtCmdQueueOut *);
    ~FPGA();

    bool isOk() const { return is_ok; }
    const std::string & port() const { return PortNum;  }
    const std::string & portConfig() const { return PortConfig; }

    void write(const std::string &); ///< queued write.. returns immediately, writes in another thread
    void readAll(std::list<std::string> & out_list); ///< reads all data available, returns immediately, may return an empty list if no data is available

    void protocol_Write(int CMD_Code, int Value_1, INT32 Value_2);

private:
    XtCmdQueueOut    *xtOut = nullptr;
    std::string PortNum, PortConfig;
    DCB Port1DCB;
    COMMTIMEOUTS CommTimeouts;
    HANDLE hPort1 = INVALID_HANDLE_VALUE;
    bool is_ok = false;

    bool configure(const int parms[6]);
    bool setupCOM();

    struct Handler;

    Handler *handler = nullptr;
};

