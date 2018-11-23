#ifndef PAGEDRINGBUFFER_H
#define PAGEDRINGBUFFER_H

#include <vector>

#include "Thread.h"
#include <QSemaphore>

#include <string.h>

#define PAGED_RINGBUFFER_MAGIC 0xbaba7fead

class PagedRingBuffer
{
public:
    PagedRingBuffer(void *mem, unsigned long size_bytes, unsigned long page_size);

    unsigned long pageSize() const { return page_size; }
    unsigned long totalSize() const { return real_size_bytes; }
    unsigned long availableCapacity() const { return avail_size_bytes; }
    unsigned int nPages() const { return npages; }

    void resetToBeginning();

    void *getCurrentReadPage();
    /// returns NULL when a new read page isn't 'ready' yet.  nSkips is the number of pages dropped due to overflows.  Normally should be 0.
    void *nextReadPage(int *nSkips = nullptr);

    /// clear the contents to 0.
    void bzero();

    void *rawData() const { return const_cast<void *>(memBuffer); }

    /// the latest page number written by the writer.  This gets modified for each page but since it's declared 'volatile', reading
    /// it *should* be atomic on x86 and x86_64.  Use this as an indicator of how "behind" the reader is..
    unsigned int latest() const { if (latestPNum) return *latestPNum; return 0; }
    /// Returns the last page this reader saw.  Compare it to latest() to get an idea of how far behind this reader is.
    unsigned int latestPageRead() const { return lastPageRead; }

protected:
    union {
        void *memBuffer;
        volatile unsigned int *latestPNum;
    };
    char *mem; // points one unsigned int past memBuffer
    unsigned long real_size_bytes, avail_size_bytes, page_size;
    unsigned int npages, lastPageRead;
    int pageIdx;

    struct Header {
        volatile unsigned int magic;
        volatile unsigned int pageNum;
    };
};


/// Page lifecycle for writing is:
///  1. void * page = grabNextPageForWrite()
///  2. do stuff with the page (write to it)
///  3. call commitCurrentWritePage() to indicate it now has valid data
///  4. repeat 1-3 above for next page
class PagedRingBufferWriter : public PagedRingBuffer
{
public:
    PagedRingBufferWriter(void *mem, unsigned long size_bytes, unsigned long page_size);
    virtual ~PagedRingBufferWriter();

    unsigned long nPagesWritten() const { return nWritten; }

    /// grab the next write page.
    /// note that grabbing more than 1 page at a time will result in an error.
    /// Grab the page, write to it, then commit it sometime later before grabbing
    /// another one.
    virtual void *grabNextPageForWrite();
    /// Updates the magic header info so that the current write page can now
    /// be effectively written by reading processes/tasks.  Call this after
    /// being done with a page returned from grabNextPageForWrite(), and before
    /// calling grabNextPageForWrite() again after being done with the page.
    bool commitCurrentWritePage();

    void initializeForWriting(); ///< generally, call this before first writing to the buffer to clear it to 0

private:
    unsigned long nWritten;
    unsigned int lastPageWritten;

};

class PagedScanWriter;

class PagedScanReader : public PagedRingBuffer
{
public:
    PagedScanReader(unsigned scan_size_samples,
                    unsigned meta_data_size_bytes,
                    void *mem, unsigned long mem_size_bytes, unsigned long page_size);

    PagedScanReader(const PagedScanReader &r);
    PagedScanReader(const PagedScanWriter &w);

    unsigned metaDataSizeBytes() const { return meta_data_size_bytes; }
    unsigned long long scansRead() const { return scanCt; }
    unsigned long long samplesRead() const { return scanCt * static_cast<unsigned long long>(scan_size_samps); }
    /// includes 'dead' or 'skipped' scans in total (due to overflows)
    unsigned long long scansReadVirtual() const { return scanCtV; }
    /// includes 'dead' or 'skipped' scans in total (due to overflows)
    unsigned long long samplesReadVirtual() const { return scanCtV * static_cast<unsigned long long>(scan_size_samps); }

    /// number of scans per call to next()
    unsigned scansPerPage() const { return nScansPerPage; }
    /// in samples
    unsigned scanSizeSamps() const { return scan_size_samps; }

    const short *next(int *nSkips, void **metaPtr = nullptr, unsigned *scans_returned = nullptr);

private:
    unsigned scan_size_samps, meta_data_size_bytes;
    unsigned nScansPerPage;
    unsigned long long scanCt, scanCtV;
};

class PagedScanWriter : public PagedRingBufferWriter
{
public:
    typedef int(*ErrFunc_t)(const char *fmt,...);

    ErrFunc_t ErrFunc, DbgFunc;

    PagedScanWriter(unsigned scan_size_samples, unsigned meta_data_size_bytes, void *mem, unsigned long size_bytes, unsigned long page_size,
                    const std::vector<int> & chan_mapping = std::vector<int>());
    ~PagedScanWriter();

    /// in samples
    unsigned scanSizeSamps() const { return scan_size_samps; }
    unsigned scanSizeBytes() const { return scan_size_bytes; }
    unsigned scansPerPage() const { return nScansPerPage; }
    unsigned long long scansWritten() const { return scanCt; }
    unsigned long long samplesWritten() const { return sampleCt; }
    unsigned metaDataSizeBytes() const { return meta_data_size_bytes; }


    /// write full scans, optionally writing metadata
    bool write(const short *scans, unsigned nScans, const void *meta = nullptr);

    /// write partial scans -- optimization for pitch != w in FG_SpikeGL.exe.. no metadata support
    void writePartialBegin();
    bool writePartial(const void *partialData, unsigned bytes, const void *meta_of_size_metaDataSizeBytes = nullptr);
    bool writePartialEnd(); // call this when your partial write is done and you are *sure* you have a multiple of 1 or more full scans written


    // same as super class but also initializes the ScanRemapper
    /*virtual*/ void *grabNextPageForWrite();

protected:
    void commit(); ///< called by write to commit the current page

private:
    short *currPage;
    unsigned scan_size_samps, scan_size_bytes, meta_data_size_bytes;
    unsigned nScansPerPage, nBytesPerPage, pageOffset /*in scans*/, partial_offset /* in bytes */, partial_bytes_written, partial_rem;
    unsigned long long scanCt, sampleCt;

    class ScanRemapper : public Thread, public QSemaphore {
    public:
        ErrFunc_t ErrFunc, DbgFunc;

        ScanRemapper(short *page, unsigned nScansPerPage, unsigned scanLen, const std::vector<int> & mapping);
        ~ScanRemapper();
    protected:
        void threadFunc();
    private:
        volatile bool pleaseStop;
        short *scans;
        unsigned nScansPerPage, scanLen;
        const std::vector<int> & map;
    };

    const std::vector<int> chan_mapping;
    ScanRemapper *mapper;
};


#endif // PAGEDRINGBUFFER_H

