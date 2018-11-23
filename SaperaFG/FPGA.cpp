#include "CommonIncludes.h"
#include "FPGA.h"
#include "Thread_Win32_Only.h"

struct FPGA::Handler : public Thread {
    HANDLE h;
    volatile bool pleaseStop;
    Mutex mut;
    std::list<std::string> q, rq;
    bool debugFlag[64];

    Handler(HANDLE h) : h(h) { memset(debugFlag, 0, sizeof(debugFlag)); }
    ~Handler(); 

    void threadFunc(); ///< from Thread
    bool readLine(std::string & partialLine);
    int readAllLines(std::string & partialLine);
};

FPGA::Handler::~Handler() {
    if (isRunning()) { 
        pleaseStop = true; 
        if (!wait(250)) kill();
    }
}

FPGA::FPGA(const int parms[6])
    : hPort1(INVALID_HANDLE_VALUE), is_ok(false), handler(0)
{
    if (is_ok = configure(parms)) {
        handler = new Handler(hPort1);
        handler->start();

        // auto-reset dout and leds..
        protocol_Write(1, 0, 0);
        protocol_Write(2, 0, 0);
    }
}


FPGA::~FPGA()
{
    delete handler;
    handler = 0;
    if (hPort1 != INVALID_HANDLE_VALUE) CloseHandle(hPort1);
    hPort1 = INVALID_HANDLE_VALUE;
}

bool FPGA::configure(const int parms[6])
{
    std::string str1, str2;

    memset(&Port1DCB, 0, sizeof(Port1DCB));

    if (hPort1 != INVALID_HANDLE_VALUE) CloseHandle(hPort1);

    switch (parms[0]) // Port Num
    {
    case 0:		PortNum = "COM1";				break;
    case 1:		PortNum = "COM2";				break;
    case 2:		PortNum = "COM3";				break;
    case 3:		PortNum = "COM4";				break;
    default:	PortNum = "COM1";				break;
    }

    hPort1 = CreateFile(tcharify(PortNum), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (hPort1 != INVALID_HANDLE_VALUE)
    {
        Port1DCB.DCBlength = sizeof(DCB);
        GetCommState(hPort1, &Port1DCB);
        CloseHandle(hPort1);
        hPort1 = INVALID_HANDLE_VALUE;
    }

    // Change the DCB structure settings
    Port1DCB.fBinary = TRUE;				// Binary mode; no EOF check
    Port1DCB.fParity = TRUE;				// Enable parity checking 
    Port1DCB.fDsrSensitivity = FALSE;		// DSR sensitivity 
    Port1DCB.fErrorChar = FALSE;			// Disable error replacement 
    Port1DCB.fOutxDsrFlow = FALSE;			// No DSR output flow control 
    Port1DCB.fAbortOnError = FALSE;			// Do not abort reads/writes on error
    Port1DCB.fNull = FALSE;					// Disable null stripping 
    Port1DCB.fTXContinueOnXoff = TRUE;		// XOFF continues Tx 

    switch (parms[1]) // BAUD Rate
    {
    case 0:		Port1DCB.BaudRate = 230400;		break;
    case 1:		Port1DCB.BaudRate = 115200;		break;
    case 2:		Port1DCB.BaudRate = 57600;		break;
    case 3:		Port1DCB.BaudRate = 38400;		break;
    case 4:		Port1DCB.BaudRate = 28800;		break;
    case 5:		Port1DCB.BaudRate = 19200;      break;
    case 6:		Port1DCB.BaudRate = 9600;		break;
    case 7:		Port1DCB.BaudRate = 4800;		break;
    case 8:		Port1DCB.BaudRate = 2400;		break;
    default:	Port1DCB.BaudRate = 0;			break;
    }

    switch (parms[2]) // Number of bits/byte, 5-8 
    {
    case 0:		Port1DCB.ByteSize = 8;			break;
    case 1:		Port1DCB.ByteSize = 7;			break;
    default:	Port1DCB.ByteSize = 0;			break;
    }

    switch (parms[3]) // 0-4=no,odd,even,mark,space 
    {
    case 0:		Port1DCB.Parity = NOPARITY;
        str1 = "N";						break;
    case 1:		Port1DCB.Parity = EVENPARITY;
        str1 = "E";						break;
    case 2:		Port1DCB.Parity = ODDPARITY;
        str1 = "O";						break;
    default:	Port1DCB.Parity = 0;
        str1 = "X";						break;
    }

    switch (parms[4])
    {
    case 0:		Port1DCB.StopBits = ONESTOPBIT;
        str2 = "1";						break;
    case 1:		Port1DCB.StopBits = TWOSTOPBITS;
        str2 = "2";						break;
    default:	Port1DCB.StopBits = 0;
        str2 = "0";						break;
    }

#if 0
    switch (parms[5])
    {
    case 0:		
        Port1DCB.fOutxCtsFlow = TRUE;					// CTS output flow control 
        Port1DCB.fDtrControl = DTR_CONTROL_ENABLE;		// DTR flow control type 
        Port1DCB.fOutX = FALSE;							// No XON/XOFF out flow control 
        Port1DCB.fInX = FALSE;							// No XON/XOFF in flow control 
        Port1DCB.fRtsControl = RTS_CONTROL_ENABLE;		// RTS flow control   
        break;
    case 1:		
        Port1DCB.fOutxCtsFlow = FALSE;					// No CTS output flow control 
        Port1DCB.fDtrControl = DTR_CONTROL_ENABLE;		// DTR flow control type 
        Port1DCB.fOutX = FALSE;							// No XON/XOFF out flow control 
        Port1DCB.fInX = FALSE;							// No XON/XOFF in flow control 
        Port1DCB.fRtsControl = RTS_CONTROL_ENABLE;		// RTS flow control 
        break;
    case 2:		
        Port1DCB.fOutxCtsFlow = FALSE;					// No CTS output flow control 
        Port1DCB.fDtrControl = DTR_CONTROL_ENABLE;		// DTR flow control type 
        Port1DCB.fOutX = TRUE;							// Enable XON/XOFF out flow control 
        Port1DCB.fInX = TRUE;							// Enable XON/XOFF in flow control 
        Port1DCB.fRtsControl = RTS_CONTROL_ENABLE;		// RTS flow control    
        break;
    default:	break;
    }
#else
    Port1DCB.fOutxCtsFlow = FALSE;					// CTS output flow control 
    Port1DCB.fDtrControl = DTR_CONTROL_DISABLE;		// DTR flow control type 
    Port1DCB.fOutX = FALSE;							// No XON/XOFF out flow control 
    Port1DCB.fInX = FALSE;							// No XON/XOFF in flow control 
    Port1DCB.fRtsControl = RTS_CONTROL_DISABLE;		// RTS flow control   
#endif
    char buf[512];
    _snprintf_c(buf, sizeof(buf), "%s %d, %d, %s, %s", PortNum.c_str(), Port1DCB.BaudRate, Port1DCB.ByteSize, str1.c_str(), str2.c_str());
    PortConfig = buf;
    spikeGL->pushConsoleDebug(PortConfig);

    return setupCOM();
}

bool FPGA::setupCOM()
{
    if (hPort1 != INVALID_HANDLE_VALUE) CloseHandle(hPort1);

    hPort1 = CreateFile(tcharify(PortNum), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (hPort1 == INVALID_HANDLE_VALUE)
    {
        spikeGL->pushConsoleWarning("Port Open Failed");
        return false;
    }

    DCB tmpDcb;
    memset(&tmpDcb, 0, sizeof(tmpDcb));
    tmpDcb.DCBlength = sizeof(tmpDcb);
    Port1DCB.DCBlength = sizeof(DCB);			//Initialize the DCBlength member. 

    if (!GetCommState(hPort1, &tmpDcb))	// Get the default port setting information.
    {
        spikeGL->pushConsoleWarning("GetCommState Failed");
        CloseHandle(hPort1); hPort1 = INVALID_HANDLE_VALUE;
        return false;
    }

    tmpDcb.BaudRate = Port1DCB.BaudRate;
    tmpDcb.ByteSize = Port1DCB.ByteSize;
    tmpDcb.Parity = Port1DCB.Parity;
    tmpDcb.StopBits = Port1DCB.StopBits;
    tmpDcb.fOutxCtsFlow = Port1DCB.fOutxCtsFlow;
    tmpDcb.fDtrControl = Port1DCB.fDtrControl;
    tmpDcb.fOutX = Port1DCB.fOutX;
    tmpDcb.fInX = Port1DCB.fInX;
    tmpDcb.fRtsControl = Port1DCB.fRtsControl;

    tmpDcb.fBinary = Port1DCB.fBinary;
    tmpDcb.fParity = Port1DCB.fParity;
    tmpDcb.fDsrSensitivity = Port1DCB.fDsrSensitivity;
    tmpDcb.fErrorChar = Port1DCB.fErrorChar;
    tmpDcb.fOutxDsrFlow = Port1DCB.fOutxDsrFlow;
    tmpDcb.fAbortOnError = Port1DCB.fAbortOnError;
    tmpDcb.fNull = Port1DCB.fNull;
    tmpDcb.fTXContinueOnXoff = Port1DCB.fTXContinueOnXoff;

    //Re-configure the port with the new DCB structure. 
    if (!SetCommState(hPort1, &tmpDcb))
    {
        spikeGL->pushConsoleWarning("SetCommState Failed");
        CloseHandle(hPort1); hPort1 = INVALID_HANDLE_VALUE;
        return false;
    }

    if (!GetCommState(hPort1, &Port1DCB))	// Get and save the new port setting information.
    {
        spikeGL->pushConsoleWarning("GetCommState Failed");
        CloseHandle(hPort1); hPort1 = INVALID_HANDLE_VALUE;
        return false;
    }

    memset(&CommTimeouts, 0, sizeof(CommTimeouts));
    GetCommTimeouts(hPort1, &CommTimeouts);
    double ms_per_char = 1000.0 / double(Port1DCB.BaudRate / double(Port1DCB.ByteSize ? Port1DCB.ByteSize : 8));
    int msPerChar = int(ceil(ms_per_char));
    {
        char buf[128];
        _snprintf_c(buf, sizeof(buf), "CommTimeouts calculation --> Baud rate: %d ~= %d msPerChar", Port1DCB.BaudRate, msPerChar);
        spikeGL->pushConsoleDebug(buf);
    }
    CommTimeouts.ReadIntervalTimeout =  msPerChar*2;
    CommTimeouts.ReadTotalTimeoutConstant = 10;
    CommTimeouts.ReadTotalTimeoutMultiplier = msPerChar;
    CommTimeouts.WriteTotalTimeoutMultiplier = msPerChar*3;
    CommTimeouts.WriteTotalTimeoutConstant = 100;

    // Set the time-out parameters for all read and write operations on the port. 
    if (!SetCommTimeouts(hPort1, &CommTimeouts))
    {
        spikeGL->pushConsoleWarning("SetCommTimeouts Failed");
        CloseHandle(hPort1); hPort1 = INVALID_HANDLE_VALUE;
        return false;
    }

    // Clear the port of any existing data. 
    if (PurgeComm(hPort1, PURGE_TXCLEAR | PURGE_RXCLEAR) == 0)
    {
        spikeGL->pushConsoleWarning("Clearing The Port Failed");
        CloseHandle(hPort1); hPort1 = INVALID_HANDLE_VALUE;
        return false;
    }
    return true;
}

bool FPGA::Handler::readLine(std::string & partial)
{
    char c;
    DWORD nb = 0;
    while (ReadFile(h, &c, 1, &nb, NULL)) {
        if (nb == 1) {
            partial.push_back(c);
            if (c == '\n') return true;
        } else {
            if (!debugFlag[0]) {
                spikeGL->pushConsoleDebug("FPGA::Handler::readLine() read 0 bytes... so partial reads works OK! Yay!");
                debugFlag[0] = true;
            }
            return false;
        }
        nb = 0;
    }
    spikeGL->pushConsoleDebug("FPGA::Handler::readLine() ReadFile() returned FALSE... WTF?");
    return false;
}

int FPGA::Handler::readAllLines(std::string & partial) 
{
    int ct = 0;
    while ((!partial.empty() && partial.back() == '\n') || readLine(partial)) {
        if (mut.lock(10)) {
            rq.push_back(partial);
            mut.unlock();
            ++ct;
            spikeGL->pushConsoleDebug(std::string("FPGA::Handler::readAllLines() -- read from COM ") + partial);
            partial.clear();
        } else {
            spikeGL->pushConsoleDebug("FPGA::Handler::readAllLines() -- Waiting on mutex...");
        }
    }
    return ct;
}

void FPGA::Handler::threadFunc()
{
    pleaseStop = false;
    std::string partial;
    while (!pleaseStop) {
        int nxferred = 0;
        if (partial.length() < 512) partial.reserve(512);
        std::list<std::string> my;
        if (mut.lock(10)) {
            my.swap(q);
            mut.unlock();
        }
        for (std::list<std::string>::const_iterator it = my.begin(); it != my.end(); ++it) {
            DWORD nb = 0, len = (DWORD)(*it).length();
            spikeGL->pushConsoleDebug(std::string("Attempt to write: ") + *it);
            if (!WriteFile(h, (*it).c_str(), len, &nb, NULL)) {
                spikeGL->pushConsoleDebug("FPGA::Handler::threadFunc() -- WriteFile() returned FALSE!");
            } else if (nb != len) {
                char buf[512];
                _snprintf_c(buf, sizeof(buf), "FPGA::Handler::threadFunc() -- WriteFile() returned %d, expected %d! (write timeout?)", (int)nb, (int)len);
                spikeGL->pushConsoleDebug(buf);
            } else {
                ++nxferred;
                spikeGL->pushConsoleDebug(std::string("Wrote to COM --> ") + *it);
            }
            nxferred += readAllLines(partial);
        }
        nxferred += readAllLines(partial);
        if (!nxferred && !pleaseStop) Sleep(10);
    }
}

void FPGA::write(const std::string &s) { ///< queued write.. returns immediately, writes in another thread
    if (!handler) return;
    if (handler->mut.lock()) {
        handler->q.push_back(s);
        handler->mut.unlock();
    } else {
        spikeGL->pushConsoleWarning("INTERNAL ERROR -- FPGA::write() could not obtain a required mutex!");
    }
}

void FPGA::readAll(std::list<std::string> & ret) { ///< reads all data available, returns immediately, may return an empty list if no data is available
    ret.clear();
    if (handler) {
        if (handler->mut.lock()) {
            ret.swap(handler->rq);
            handler->mut.unlock();
        } else {
            spikeGL->pushConsoleWarning("INTERNAL ERROR -- FPGA::readAll() could not obtain a required mutex!");
        }
    }
}

void FPGA::protocol_Write(int CMD_Code, int Value_1, INT32 Value_2)
{
    char    str[512];

    _snprintf_c(str, sizeof(str), "%c%02d%05d%06d\r\n", '~', CMD_Code, Value_1, Value_2);
    write(str);
}
