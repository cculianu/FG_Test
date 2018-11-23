#ifndef QT_CORE_LIB
#include "CommonIncludes.h"
#include "Thread_Win32_Only.h"


Thread::Thread()
    : running(false), started_but_not_yet_running(false), threadId(0), threadHandle(0)
{
}


Thread::~Thread()
{
    if (threadHandle) CloseHandle(threadHandle);
    threadHandle = 0; threadId = 0;
}

bool Thread::wait(unsigned timeout)
{
    if (threadHandle && running || started_but_not_yet_running) {
        DWORD r = WaitForSingleObject(threadHandle, timeout);
        return r == WAIT_OBJECT_0;
    }
    return true;
}

void Thread::kill()
{
    if (threadHandle && running) {
        if (TerminateThread(threadHandle, -1)) {
            running = false;
            started_but_not_yet_running = false;
        }
    }
}

DWORD WINAPI Thread::threadRoutine(LPVOID param)
{
    Thread *thiz = (Thread *)param;
    thiz->running = true;
    thiz->started_but_not_yet_running = false;
    thiz->threadFunc();
    thiz->running = false;
    return 0;
}

bool Thread::start()
{
    if (running || started_but_not_yet_running) return false;
    started_but_not_yet_running = true;

    threadHandle = 
        CreateThread(
        NULL,       // default security attributes
        0,          // default stack size
        (LPTHREAD_START_ROUTINE)threadRoutine,
        this,       // thread function argument
        0,          // default creation flags
        &threadId); // receive thread identifier
    if (!threadId) started_but_not_yet_running = false;
    return !!threadHandle;
}

Mutex::Mutex()
{
    h = CreateMutex(NULL, FALSE, NULL);
}

Mutex::~Mutex()
{
    CloseHandle(h);
}

bool Mutex::lock(unsigned timeout)
{
    DWORD res = WaitForSingleObject(h, timeout);
    return (res == WAIT_OBJECT_0);
}

void Mutex::unlock()
{
    ReleaseMutex(h);
}

Semaphore::Semaphore(unsigned max, unsigned cur) 
{
    h = CreateSemaphore(NULL, cur, max, NULL);
}

Semaphore::~Semaphore()
{
    if (h) CloseHandle(h);
}

bool Semaphore::acquire(unsigned timeout)
{
    if (!h) return false;
    DWORD res = WaitForSingleObject(h, timeout);
    if (res == WAIT_OBJECT_0) return true;
    return false;
}

void Semaphore::release(int ct) {
    if (ct > 0 && h) {
        ReleaseSemaphore(h, ct, NULL);
    }
}
#else // defined QT_CORE_LIB
#include "Thread.h"
Thread::~Thread() {} // for vtable
#endif // !defined QT_CORE_LIB
