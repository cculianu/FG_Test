#pragma once
#include <windows.h>

class Thread
{
public:
    Thread();
    virtual ~Thread();

    bool isRunning() const { return running || started_but_not_yet_running;  }
    bool start();
    bool wait(unsigned timeout_ms);
    void kill();

    int id() { return (int)threadId;  }

protected:
    volatile bool running, started_but_not_yet_running;
    DWORD threadId; HANDLE threadHandle;
    static DWORD WINAPI threadRoutine(LPVOID param);
    virtual void threadFunc() = 0;
};

class Mutex
{
public:
    Mutex();
    ~Mutex();

    bool lock(unsigned timeout_ms = INFINITE);
    void unlock();

private:
    HANDLE h;
};

class Semaphore
{
public:
    Semaphore(unsigned max_count, unsigned current_count = 0);
    ~Semaphore();

    bool acquire(unsigned timeout_ms = INFINITE); ///< acquire 1. 
    void release(int ct = 1); ///< add ct to the semaphore 
private:
    HANDLE h;
};
