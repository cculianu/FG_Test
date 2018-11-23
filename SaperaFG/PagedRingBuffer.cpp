#include "CommonIncludes.h"
#include "PagedRingBuffer.h"
#include <string.h>

PagedRingBuffer::PagedRingBuffer(void *m, unsigned long sz, unsigned long psz)
    : memBuffer(m), mem(reinterpret_cast<char *>(m)+sizeof(unsigned int)), real_size_bytes(sz), avail_size_bytes(sz-sizeof(unsigned int)), page_size(psz)
{
    resetToBeginning();
}

void PagedRingBuffer::resetToBeginning()
{
    lastPageRead = 0; pageIdx = -1;
    npages = avail_size_bytes/(page_size + sizeof(Header));
    if (page_size > avail_size_bytes || !page_size || !avail_size_bytes || !npages || !real_size_bytes || avail_size_bytes > real_size_bytes) {
        memBuffer = nullptr; mem = nullptr; page_size = 0; avail_size_bytes = 0; npages = 0; real_size_bytes = 0;
    }
}

void *PagedRingBuffer::getCurrentReadPage()
{
    if (!mem || !npages || !avail_size_bytes) return nullptr;
    if (pageIdx < 0 || pageIdx >= int(npages)) return nullptr;
    Header *h = reinterpret_cast<Header *>(&mem[ (page_size+sizeof(Header)) * size_t(pageIdx) ]);
    Header hdr; memcpy(&hdr, h, sizeof(hdr)); // hopefully avoid some potential race conditions
    if (hdr.magic != unsigned(PAGED_RINGBUFFER_MAGIC) || hdr.pageNum != lastPageRead) return nullptr;
    return reinterpret_cast<char *>(h)+sizeof(Header);
}

void *PagedRingBuffer::nextReadPage(int *nSkips)
{
    if (!mem || !npages || !avail_size_bytes) return nullptr;
    int nxt = (pageIdx+1) % int(npages);
    if (nxt < 0) nxt = 0;
    Header *h = reinterpret_cast<Header *>(&mem[ (page_size+sizeof(Header)) * size_t(nxt) ]);
    Header hdr; memcpy(&hdr, h, sizeof(hdr)); // hopefully avoid some potential race conditions
    if (hdr.magic == unsigned(PAGED_RINGBUFFER_MAGIC) && hdr.pageNum >= lastPageRead+1U) {
        if (nSkips) *nSkips = int(hdr.pageNum-(lastPageRead+1)); // record number of overflows/lost pages here!
        lastPageRead = hdr.pageNum;
        pageIdx = nxt;
        return reinterpret_cast<char *>(h)+sizeof(Header);
    }
    // if we get to this point, a new read page isn't 'ready' yet!
    if (nSkips) *nSkips = 0;
    return nullptr;
}

void PagedRingBuffer::bzero() {
    if (mem && avail_size_bytes) memset(mem, 0, avail_size_bytes);
}

PagedRingBufferWriter::PagedRingBufferWriter(void *mem, unsigned long sz, unsigned long psz)
    : PagedRingBuffer(mem, sz, psz)
{
    lastPageWritten = 0;
    nWritten = 0;
}

PagedRingBufferWriter::~PagedRingBufferWriter() {}

void PagedRingBufferWriter::initializeForWriting()
{
    bzero();
    pageIdx = -1;
    nWritten = 0;
    lastPageWritten = 0;
}

void *PagedRingBufferWriter::grabNextPageForWrite()
{
    if (!mem || !npages || !avail_size_bytes) return nullptr;
    int nxt = (pageIdx+1) % int(npages);
    if (nxt < 0) nxt = 0;
    Header *h = reinterpret_cast<Header *>(&mem[ (page_size+sizeof(Header)) * size_t(nxt) ]);
    h->magic = 0; h->pageNum = 0;
    pageIdx = nxt;
    return reinterpret_cast<char *>(h)+sizeof(Header);
}

bool PagedRingBufferWriter::commitCurrentWritePage()
{
    if (!mem || !npages || !avail_size_bytes) return false;
    int pg = pageIdx % int(npages);
    if (pg < 0) pg = 0;
    Header *h = reinterpret_cast<Header *>(&mem[ (page_size+sizeof(Header)) * size_t(pg) ]);
    pageIdx = pg;
    h->pageNum = *latestPNum = ++lastPageWritten;
    h->magic = unsigned(PAGED_RINGBUFFER_MAGIC);
    ++nWritten;
    return true;
}

PagedScanReader::PagedScanReader(unsigned scan_size_samples, unsigned meta_data_size_bytes, void *mem, unsigned long size_bytes, unsigned long page_size)
    : PagedRingBuffer(mem, size_bytes, page_size), scan_size_samps(scan_size_samples), meta_data_size_bytes(meta_data_size_bytes)
{
    if (meta_data_size_bytes > page_size) meta_data_size_bytes = page_size;
    nScansPerPage = scan_size_samps ? ((page_size-meta_data_size_bytes)/(scan_size_samps*sizeof(short))) : 0;
    scanCt = scanCtV = 0;
}

PagedScanReader::PagedScanReader(const PagedScanReader &o)
    : PagedRingBuffer(o.rawData(), o.totalSize(), o.pageSize()), scan_size_samps(o.scanSizeSamps()), meta_data_size_bytes(o.meta_data_size_bytes)
{
    if (meta_data_size_bytes > page_size) meta_data_size_bytes = page_size;
    nScansPerPage = scan_size_samps ? ((page_size-meta_data_size_bytes)/(scan_size_samps*sizeof(short))) : 0;
    scanCt = scanCtV = 0;
}

PagedScanReader::PagedScanReader(const PagedScanWriter &o)
    : PagedRingBuffer(o.rawData(), o.totalSize(), o.pageSize()), scan_size_samps(o.scanSizeSamps()), meta_data_size_bytes(o.metaDataSizeBytes())
{
    if (meta_data_size_bytes > page_size) meta_data_size_bytes = page_size;
    nScansPerPage = scan_size_samps ? ((page_size-meta_data_size_bytes)/(scan_size_samps*sizeof(short))) : 0;
    scanCt = scanCtV = 0;
}

const short *PagedScanReader::next(int *nSkips, void **metaPtr, unsigned *scans_returned)
{
    int sk = 0;
    const short *scans = reinterpret_cast<short *>(nextReadPage(&sk));
    if (nSkips) *nSkips = sk;
    if (scans_returned) *scans_returned = 0;
    if (metaPtr) *metaPtr = nullptr;
    if (scans) {
        if (scans_returned) *scans_returned = nScansPerPage;
        scanCtV += static_cast<unsigned long long>(nScansPerPage*(unsigned(sk)+1U));
        scanCt += static_cast<unsigned long long>(nScansPerPage);
        if (metaPtr && meta_data_size_bytes)
            *metaPtr = const_cast<short *>(scans+(nScansPerPage*scan_size_samps));
    }
    return scans;
}

static int dummyErrFunc(const char *fmt, ...) { (void)fmt; return 0; }

PagedScanWriter::PagedScanWriter(unsigned scan_size_samples, unsigned meta_data_size_bytes, void *mem, unsigned long size_bytes, unsigned long page_size, const std::vector<int> & cmap)
    : PagedRingBufferWriter(mem, size_bytes, page_size), ErrFunc(&dummyErrFunc), scan_size_samps(scan_size_samples), scan_size_bytes(scan_size_samples*sizeof(short)), meta_data_size_bytes(meta_data_size_bytes), chan_mapping(cmap),  mapper(nullptr)
{
    if (meta_data_size_bytes > page_size) meta_data_size_bytes = page_size;
    nScansPerPage = scan_size_bytes ? ((page_size-meta_data_size_bytes)/scan_size_bytes) : 0;
    nBytesPerPage = nScansPerPage * scan_size_bytes;
    pageOffset = 0; partial_offset = 0; partial_bytes_written = 0; partial_rem = 0;
    scanCt = 0;
    sampleCt = 0;
    currPage = nullptr;
}

PagedScanWriter::~PagedScanWriter()
{
    if (mapper) delete mapper; mapper = nullptr; // tell mapper to end and kill it
}

void PagedScanWriter::writePartialBegin()
{
    partial_offset = pageOffset*scan_size_bytes;
    partial_bytes_written = 0;
    partial_rem = 0;
}

bool PagedScanWriter::writePartialEnd()
{
    bool ret = !(partial_offset % scan_size_bytes); // should always be aligned.  if not, return false and indicate to caller something is off.

    pageOffset = partial_offset / scan_size_bytes;
    scanCt += static_cast<unsigned long long>(partial_bytes_written / scan_size_bytes);
    sampleCt += static_cast<unsigned long long>(partial_bytes_written / sizeof(short));
    partial_bytes_written = 0; // guard against shitty use of class
    partial_rem = 0;

    if (pageOffset > nScansPerPage) return false; // should never happen.  indicates bug in this code.
    if (pageOffset == nScansPerPage) commit();  // should never happen!
    return ret;
}

bool PagedScanWriter::writePartial(const void *data, unsigned nbytes, const void *meta_data)
{
    unsigned dataOffset = 0;
    while (nbytes) {
        if (!currPage) { currPage = reinterpret_cast<short *>(grabNextPageForWrite()); pageOffset = 0; partial_offset = 0; partial_rem = 0; }
        unsigned spaceLeft = (nScansPerPage*scan_size_bytes) - partial_offset;
        if (!spaceLeft) {
            ErrFunc("FATAL! Improper use of class or bad code in PagedScanWriter::writePartial() call!  spaceLeft = 0 when it should not be 0!");
            return false; // should not be reached.. a safeguard in case of improper pagesize of improper use of class
        }
        unsigned n2write = nbytes > spaceLeft ? spaceLeft : nbytes;
        memcpy(reinterpret_cast<char *>(currPage)+partial_offset, reinterpret_cast<const char *>(data)+dataOffset, n2write);
        dataOffset += n2write;
        partial_offset += n2write;
        partial_bytes_written += n2write;
        nbytes -= n2write;
        spaceLeft -= n2write;
        /*if (mapper) {
            mapper->release((partial_rem+n2write)/scan_size_bytes);
            partial_rem = (partial_rem+n2write) % scan_size_bytes;
        }*/

        if (!spaceLeft) {
            if (meta_data_size_bytes) {
                if (!meta_data) return false; // force caller to give us metadata when we are closing up a page!
                memcpy(reinterpret_cast<char *>(currPage)+partial_offset, meta_data, meta_data_size_bytes);
            }
            commit();
        }
    }
    return true;
}

void *PagedScanWriter::grabNextPageForWrite()
{
    if (mapper) { delete mapper; mapper = nullptr; }
    void *p = PagedRingBufferWriter::grabNextPageForWrite();
    /* NOTE THAT CHANNEL MAPPING IS BROKEN FOR NOW.. or so it appears.  It lead to performance issues & hangs -- see 4/12/2016 email from Jim Chen.
       So we disable it and just do the more kosher thing of having a correct channel mapping setup in SpikeGL GUI.
       This means the data file will always have raw frame format.. but at least things work well.  
    if (chan_mapping.size()) {
        mapper = new ScanRemapper(reinterpret_cast<short *>(p), nScansPerPage, scan_size_samps, chan_mapping);
        mapper->ErrFunc = ErrFunc; mapper->DbgFunc = DbgFunc;
        if (!mapper->start()) {
            ErrFunc("FATAL! Failed to start ScanRemapper thread in PagedScanWriter::grabNextPageForWrite()");
        }
    }*/
    return p;
}

bool PagedScanWriter::write(const short *scans, unsigned nScans, const void *meta) {
    unsigned scansOff = 0; //in scans
    while (nScans) {
        if (!currPage) { currPage = reinterpret_cast<short *>(grabNextPageForWrite()); pageOffset = 0; }
        unsigned spaceLeft = nScansPerPage - pageOffset;
        if (!spaceLeft) { 
            ErrFunc("FATAL! Improper use of class or bad code in PagedScanWriter::write() call!  spaceLeft = 0 when it should not be 0!");
            return false; /* this should *NEVER* be reached!! A safeguard, though, in case of improper use of class and/or too small a pagesize */ 
        }
        unsigned n2write = nScans > spaceLeft ? spaceLeft : nScans;
        memcpy(currPage+(pageOffset*scan_size_samps), scans+(scansOff*scan_size_samps), n2write*scan_size_bytes);
        pageOffset += n2write;
        partial_offset = pageOffset * scan_size_bytes;
        scansOff += n2write;
        scanCt += static_cast<unsigned long long>(n2write);
        sampleCt += static_cast<unsigned long long>(n2write*scan_size_samps);
        nScans -= n2write;
        spaceLeft -= n2write;
        /*if (mapper) mapper->release(n2write); /// inform remapper that this many new scans arrived and can be reordered*/

        if (!spaceLeft) {  // if clause here is needed as above block may modify spaceLeft
            if (meta_data_size_bytes) {
                if (!meta) return false; // force caller to give us metadata when we are closing up a page!
                // append metadata to end of each page
                memcpy(currPage + (pageOffset*scan_size_samps), meta, meta_data_size_bytes);
            }
            commit(); // 'commit' the page
        }
    }
    return true;
}

void PagedScanWriter::commit()
{
    if (currPage) {
        /*if (mapper) { 
            //DbgFunc("PagedScanWriter::commit() called, but mapper %d is still active. Releasing and waiting...", mapper->id());
            mapper->release(nScansPerPage); 
            mapper->wait(); 
            delete mapper; mapper = 0;
        } // wait for mapper to complete channel reordering..*/
        commitCurrentWritePage();
        currPage = nullptr; pageOffset = 0; partial_offset = 0;
    }
}

PagedScanWriter::ScanRemapper::ScanRemapper(short *page, unsigned nScansPerPage, unsigned scanLen, const std::vector<int> & mapping)
    : ErrFunc(&dummyErrFunc), pleaseStop(false), scans(page), nScansPerPage(nScansPerPage), scanLen(scanLen), map(mapping)
{}

PagedScanWriter::ScanRemapper::~ScanRemapper()
{
    pleaseStop = true;
    if (isRunning()) {
        //DbgFunc("ScanMapper Thread %d delete called while still running, attempting to end thread gracefully...", id());
        release(); 
        wait();
    }
}

void PagedScanWriter::ScanRemapper::threadFunc()
{
    
    //DbgFunc("ScanRemapper Thread %d started.", id());
    short *scan = scans;
    int ct = 0;
    std::vector<short> tmpScan; tmpScan.resize(scanLen);
    while (ct < int(nScansPerPage)) {
        acquire();
        if (pleaseStop) {
            DbgFunc("ScanRemapper Thread %d received premature stop. Exiting...", id());
            return;
        }
        for (auto i = 0U; i < scanLen; ++i)
            tmpScan[i] = scan[map[i]];
        memcpy(scan, &tmpScan[0], scanLen*sizeof(scan[0]));
        scan += scanLen;
        ++ct;
    }
    //DbgFunc("ScanRemapper Thread %d completed successfully after processing %d scans.", id(), (int)nScansPerPage);
}


