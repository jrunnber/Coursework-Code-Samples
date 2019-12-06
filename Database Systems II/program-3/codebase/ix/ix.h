#ifndef _ix_h_
#define _ix_h_

#include <vector>
#include <string>

#include "../rbf/pfm.h"
#include "../rbf/rbfm.h"

# define IX_EOF (-1)  // end of the index scan

typedef struct InteriorSlotHeader{
    uint16_t numTrafficCops;
    uint16_t offToFree;
    bool isAboveLeaf;
} InteriorSlotHeader;

typedef uint32_t PagePointer;

typedef struct LeafSlotHeader{
    uint16_t offToFree;
    uint16_t numRecords;
    PagePointer prevPage;
    PagePointer nextPage;
} LeafSlotHeader;

typedef uint16_t LeafDirectoryOffset;

typedef struct LeafDirectoryRecordEntry{
    void * keyData;
    uint16_t offInPage;
    uint16_t numRIDs;
} LeafDirectoryRecordEntry;

typedef struct IndexedLeafRecordEntry
{
    int32_t slotNum;
    LeafDirectoryRecordEntry recordEntry;
} IndexedLeafRecordEntry;

typedef enum { ON_PAGE = 0, NEED_INSERT, CONTINUE } InsertStatus;

class IX_ScanIterator;
class IXFileHandle;

class IndexManager {

    public:
        static IndexManager* instance();

        // Create an index file.
        RC createFile(const string &fileName);

        // Delete an index file.
        RC destroyFile(const string &fileName);

        // Open an index and return an ixfileHandle.
        RC openFile(const string &fileName, IXFileHandle &ixfileHandle);

        // Close an ixfileHandle for an index.
        RC closeFile(IXFileHandle &ixfileHandle);

        // Insert an entry into the given index that is indicated by the given ixfileHandle.
        RC insertEntry(IXFileHandle &ixfileHandle, const Attribute &attribute, const void *key, const RID &rid);

        // Delete an entry from the given index that is indicated by the given ixfileHandle.
        RC deleteEntry(IXFileHandle &ixfileHandle, const Attribute &attribute, const void *key, const RID &rid);

        // Initialize and IX_ScanIterator to support a range search
        RC scan(IXFileHandle &ixfileHandle,
                const Attribute &attribute,
                const void *lowKey,
                const void *highKey,
                bool lowKeyInclusive,
                bool highKeyInclusive,
                IX_ScanIterator &ix_ScanIterator);

        // Print the B+ tree in pre-order (in a JSON record format)
        void printBtree(IXFileHandle &ixfileHandle, const Attribute &attribute);

        friend class IX_ScanIterator;

    protected:
        IndexManager();
        ~IndexManager();

    private:
        static IndexManager *_index_manager;
        static PagedFileManager *_pf_manager;

        void postOrder(IXFileHandle &ixfileHandle, const Attribute &attribute, uint32_t pageNum, bool leaf);

        // Private Helper Methods

        // Interior Page Functions
        void newInteriorPage(void * page);

        InteriorSlotHeader getInteriorSlotHeader(void * page);
        void setInteriorSlotHeader(void * page, InteriorSlotHeader interiorSlotHeader);

        void setInteriorPagePointer(void * page, uint16_t offset, PagePointer pointer);

        unsigned getInteriorPageFreeSpaceSize(void * page);

        // Leaf Page Functions
        void newLeafPage(void * page, PagePointer prevPage, PagePointer nextPage);

        LeafSlotHeader getLeafSlotHeader(void * page);
        void setLeafSlotHeader(void * page, LeafSlotHeader leafSlotHeader);

        LeafDirectoryOffset getLeafDirectoryRecordOffset(void * page, unsigned recordEntryNumber);
        void setLeafDirectoryRecordOffset(void * page, unsigned recordEntryNumber, LeafDirectoryOffset recordOffset);

        LeafDirectoryRecordEntry getLeafDirectoryRecordEntry(void * page, unsigned recordEntryNumber);
        void setLeafDirectoryRecordEntry(void * page, const Attribute &attribute, unsigned recordEntryNumber, LeafDirectoryRecordEntry recordEntry);

        unsigned getLeafPageFreeSpaceSize(void * page);

        // Others
        unsigned getKeySize(const Attribute &attribute, const void *key);

        PagePointer getFirstLeafPageNum(IXFileHandle &ixfileHandle, PagePointer pageNum);

        // Insert Helpers
        RC getLeafPage(IXFileHandle &ixfileHandle, const Attribute &attribute, const void *key, void *leafPage, vector<PagePointer> &path);

        PagePointer traverseTree(IXFileHandle &ixfileHandle, const Attribute &attribute, const void *key, PagePointer pageNum, vector<PagePointer> &path);

        InsertStatus scanForKey (void *leafPage, const Attribute &attribute, const void *key, unsigned &slotNum);

        InsertStatus compareKeys(const Attribute &attribute, const void *currentKey, const void *key);

        void updateExistingKey(void *leafPage, const Attribute &attribute, unsigned slotNum, const RID &rid);

        void addNewKey(void *leafPage, const Attribute &attribute, unsigned slotNum, const void *key, const RID &rid);

        void reorganizeLeafPage(void *page, const Attribute &attribute);

        void splitRoot(IXFileHandle &ixfileHandle, Attribute attr);
        void splitInt(IXFileHandle &ixfileHandle, vector<uint32_t> &path, uint32_t splitPageNum, Attribute attr);
        void insertInt(IXFileHandle &ixfileHandle, uint32_t pageNum, void * keyValue, int pointer, Attribute attr, vector<uint32_t> &path);
        void splitLeaf(IXFileHandle &ixfileHandle, vector<uint32_t> &path, Attribute attr);
};


class IXFileHandle {
    public:

    // variables to keep counter for each operation
    unsigned ixReadPageCounter;
    unsigned ixWritePageCounter;
    unsigned ixAppendPageCounter;

    // Constructor
    IXFileHandle();

    // Destructor
    ~IXFileHandle();

    // Put the current counter values of associated PF FileHandles into variables
    RC collectCounterValues(unsigned &readPageCount, unsigned &writePageCount, unsigned &appendPageCount);

    RC readPage(PageNum pageNum, void *data);

    RC writePage(PageNum pageNum, const void *data);

    RC appendPage(const void *data);

    RC getNumberOfPages();

    friend class IndexManager;
    friend class IX_ScanIterator;

    private:

    FileHandle handle;
};


class IX_ScanIterator {
    public:

		// Constructor
        IX_ScanIterator();

        // Destructor
        ~IX_ScanIterator() {};

        // Get next matching entry
        RC getNextEntry(RID &rid, void *key);

        // Terminate index scan
        RC close();

        friend class IndexManager;

    private:
        IndexManager *ix;

        uint32_t currRID;
        uint32_t totalRID;

        uint32_t currSlot;
        uint32_t totalSlot;

        PagePointer nextPage;

        void * leafPage;

        IXFileHandle    ixfileHandle;
        Attribute attribute;
        const void      *lowKey;
        const void      *highKey;
        bool            lowKeyInclusive;
        bool            highKeyInclusive;

        LeafSlotHeader header;
        //LeafDirectoryOffset recordOffset;
        LeafDirectoryRecordEntry recordEntry;

        RC scanInit(IXFileHandle &fh,
            const Attribute &attr,
            const void      *lk,
            const void      *hk,
            bool            lkInc,
            bool            hkInc);

        RC getNextSlot();
        RC getNextPage();
};


#endif