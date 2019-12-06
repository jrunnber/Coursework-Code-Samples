#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>

#include "ix.h"

IndexManager* IndexManager::_index_manager = 0;

PagedFileManager *IndexManager::_pf_manager = NULL;

IndexManager* IndexManager::instance()
{
    if(!_index_manager)
        _index_manager = new IndexManager();

    return _index_manager;
}

IndexManager::IndexManager()
{
    _pf_manager = PagedFileManager::instance();
}

IndexManager::~IndexManager()
{
}

RC IndexManager::createFile(const string &fileName)
{
    // Creating a new paged file.
    if (_pf_manager->createFile(fileName))
        return RBFM_CREATE_FAILED;

    // Setting up the first pages.
    void * rootPageData = calloc(PAGE_SIZE, 1);
    if (rootPageData == NULL)
        return RBFM_MALLOC_FAILED;
    newInteriorPage(rootPageData);

    void * firstLeafPageData = calloc(PAGE_SIZE, 1);
    if (firstLeafPageData == NULL)
        return RBFM_MALLOC_FAILED;
    newLeafPage(firstLeafPageData, -1, -1);

    setInteriorPagePointer(rootPageData, sizeof(InteriorSlotHeader), 1);

   // Adds the root and first leaf based page.
    FileHandle handle;
    if (_pf_manager->openFile(fileName.c_str(), handle))
        return RBFM_OPEN_FAILED;
    if (handle.appendPage(rootPageData))
        return RBFM_APPEND_FAILED;
    if (handle.appendPage(firstLeafPageData))
        return RBFM_APPEND_FAILED;

    _pf_manager->closeFile(handle);

    free(rootPageData);
    free(firstLeafPageData);


    return SUCCESS;
}

RC IndexManager::destroyFile(const string &fileName)
{
    return _pf_manager->destroyFile(fileName);
}

RC IndexManager::openFile(const string &fileName, IXFileHandle &ixfileHandle)
{
    return _pf_manager->openFile(fileName.c_str(), ixfileHandle.handle);
}

RC IndexManager::closeFile(IXFileHandle &ixfileHandle)
{
    return _pf_manager->closeFile(ixfileHandle.handle);
}

RC IndexManager::insertEntry(IXFileHandle &ixfileHandle, const Attribute &attribute, const void *key, const RID &rid)
{
    // First we get the leaf page we need and the path to it.
    void * leafPage = calloc(PAGE_SIZE, 1);
    vector<PagePointer> path;
    RC rc = getLeafPage(ixfileHandle, attribute, key, leafPage, path);
    if (rc)
        return rc;

    // Now we scan for the key to see if it is already on the page or needs to be added.
    unsigned sizeToAdd;
    unsigned slotNum;
    InsertStatus status = scanForKey(leafPage, attribute, key, slotNum);

    // We have different functions for updating a key or adding a new one.
    // We also need to split the page if we would make the page too big.
    switch(status)
    {
        case ON_PAGE:
            sizeToAdd = sizeof(RID);
            if (getLeafPageFreeSpaceSize(leafPage) >= sizeToAdd)
            {
                updateExistingKey(leafPage, attribute, slotNum, rid);
            }
            else
            {
                splitLeaf(ixfileHandle, path, attribute);
                insertEntry(ixfileHandle, attribute, key, rid);
                free(leafPage);
                return 0;
            }
            break;
        case NEED_INSERT:
            unsigned keySize = getKeySize(attribute, key);
            sizeToAdd = keySize + sizeof(RID) + 3*sizeof(uint16_t);
            if (getLeafPageFreeSpaceSize(leafPage) >= sizeToAdd)
            {
                addNewKey(leafPage, attribute, slotNum, key, rid);
            }
            else
            {
                splitLeaf(ixfileHandle, path, attribute);
                insertEntry(ixfileHandle, attribute, key, rid);
                free(leafPage);
                return 0;
            }
            break;
    }

    // Write the page out and free it.
    ixfileHandle.writePage(path.back(), leafPage);
    free(leafPage);
    return SUCCESS;
}

RC IndexManager::deleteEntry(IXFileHandle &ixfileHandle, const Attribute &attribute, const void *key, const RID &rid)
{
    // First we get the leaf page we need and ignore the path to it.
    void * leafPage = calloc(PAGE_SIZE, 1);
    vector<PagePointer> path;
    RC rc = getLeafPage(ixfileHandle, attribute, key, leafPage, path);
    if (rc)
    {
        free(leafPage);
        return rc;
    }

    // We check to make sure that the key is on the page, if not we return an error.
    unsigned slotNum;
    InsertStatus status = scanForKey(leafPage, attribute, key, slotNum);
    if (status == NEED_INSERT)
    {
        free(leafPage);
        return -1;
    }

    // Get the header and the record entry.
    LeafSlotHeader leafHeader = getLeafSlotHeader(leafPage);
    LeafDirectoryRecordEntry recordEntry = getLeafDirectoryRecordEntry(leafPage, slotNum);
    
    // If there is only one <key, RID> pair on the page we just set the page to think it is empty.
    if (leafHeader.numRecords == 1 && recordEntry.numRIDs == 1)
    {
        leafHeader.numRecords = 0;
        leafHeader.offToFree = PAGE_SIZE;
        setLeafSlotHeader(leafPage, leafHeader);
        ixfileHandle.writePage(path.back(), leafPage);
        free(recordEntry.keyData);
        free(leafPage);
        return SUCCESS;
    }

    // We make a copy of all the RIDs associated with the key that aren't the RID we want to delete.
    // Intialize Values.
    RID currRID;
    bool foundEntry = false;
    void * rids = malloc((recordEntry.numRIDs - 1) * sizeof(RID));
    int offsetInPage = recordEntry.offInPage;
    int currNumRIDs = 0;

    for (int i = 0; i < recordEntry.numRIDs; i++)
    {
        // Get the current RID.
        memcpy(&currRID, (char *)leafPage + offsetInPage, sizeof(RID));

        // If it is the one to be deleted, don't copy it and show that we found it.
        if (currRID.pageNum == rid.pageNum && currRID.slotNum == rid.slotNum)
        {
            foundEntry = true;
        }
        // If we got to the last RID without finding the one we want we return an error.
        else if(currNumRIDs == recordEntry.numRIDs - 1)
        {
            free(recordEntry.keyData);
            free(leafPage);
            free(rids);
            return -1;
        }
        // Otherwise, we copy the RID into the malloc'd block of RIDs.
        else
        {
            memcpy((char *)rids + currNumRIDs * sizeof(RID), &currRID, sizeof(RID));
            currNumRIDs += 1;
        }
        offsetInPage += sizeof(RID);
    }

    // If we never found the RID and didn't already fail, we fail here.
    if (!foundEntry)
    {
        free(recordEntry.keyData);
        free(leafPage);
        free(rids);
        return -1;
    }

    // We temporarily set the number of RIDs of our record to zero and then reorganize the page.
    uint16_t tempNumRIDs = 0;
    LeafDirectoryOffset endOfRecord = getLeafDirectoryRecordOffset(leafPage, slotNum);

    memcpy (((char*) leafPage + endOfRecord - sizeof(uint16_t)), &tempNumRIDs, sizeof(uint16_t));

    reorganizeLeafPage(leafPage, attribute);

    // We do different things if there are any RIDs left in the record or not.
    if (currNumRIDs != 0)
    {
        // If the record still has RIDs then we need to reinsert them and update the record entry.
        // We reset the header and offset.
        leafHeader = getLeafSlotHeader(leafPage);
        uint16_t offset = leafHeader.offToFree;

        // We copy in the rids to the end of free space.
        offset -= currNumRIDs * sizeof(RID);
        memcpy((char *)leafPage + offset, rids, currNumRIDs * sizeof(RID));

        // We update the header
        leafHeader.offToFree = offset;
        setLeafSlotHeader(leafPage, leafHeader);

        // We update the record entry.
        recordEntry.offInPage = offset;
        recordEntry.numRIDs = currNumRIDs;
        memcpy (((char*) leafPage + endOfRecord - 2 * sizeof(uint16_t)), &recordEntry.offInPage, sizeof(uint16_t));
        memcpy (((char*) leafPage + endOfRecord - sizeof(uint16_t)), &recordEntry.numRIDs, sizeof(uint16_t));
    }
    else
    {
        // If there are no RIDs left, then we need to delete the record entry.
        // We reset the header.
        leafHeader = getLeafSlotHeader(leafPage);

        // We get some offsets for the beggining and end of two blocks.
        // First is everything after the record offset, and second is everything after the record entry.
        LeafDirectoryOffset beginOffsetMove = sizeof(LeafSlotHeader) + (slotNum + 1) * sizeof(LeafDirectoryOffset);
        LeafDirectoryOffset endOffsetMove = getLeafDirectoryRecordOffset(leafPage, leafHeader.numRecords - 1);
        if (slotNum == leafHeader.numRecords - 1)
            endOffsetMove = getLeafDirectoryRecordOffset(leafPage, leafHeader.numRecords - 2);

        LeafDirectoryOffset beginSlotMove = getLeafDirectoryRecordOffset(leafPage, slotNum) - sizeof(LeafDirectoryOffset);
        LeafDirectoryOffset endSlotMove = endOffsetMove - sizeof(LeafDirectoryOffset);

        // We then move everything that was after the record offset back to erase the record offset.
        memmove((char *)leafPage + beginOffsetMove - sizeof(LeafDirectoryOffset), (char *)leafPage + beginOffsetMove, endOffsetMove - beginOffsetMove);

        // Now we have to update all of the remaining record offsets to accont for the first move.
        for (int i = 0; i < leafHeader.numRecords; i++)
        {
            if(i != slotNum)
            {
                LeafDirectoryOffset currentRecordOffset = getLeafDirectoryRecordOffset(leafPage, i);
                currentRecordOffset -= sizeof(LeafDirectoryOffset);
                setLeafDirectoryRecordOffset(leafPage, i, currentRecordOffset);
            }
        }

        // We need to get the size of the record for pointer math.
        unsigned keySize = getKeySize(attribute, recordEntry.keyData);
        int recordSize = keySize + 2*sizeof(uint16_t);

        // We move the seocnd block back by the size of the record to delete it.
        memmove((char *)leafPage + beginSlotMove - recordSize, (char *)leafPage + beginSlotMove, endSlotMove - beginSlotMove);

        // We update the header.
        leafHeader.numRecords -= 1;
        setLeafSlotHeader(leafPage, leafHeader);

        // We have to go through all of the records affected by the second move and update their offsets.
        for (int i = slotNum; i < leafHeader.numRecords; i++)
        {
            LeafDirectoryOffset currentRecordOffset = getLeafDirectoryRecordOffset(leafPage, i);
            currentRecordOffset -= recordSize;
            setLeafDirectoryRecordOffset(leafPage, i, currentRecordOffset);
        }
    }

    // Free values and write to page.
    free(rids);
    ixfileHandle.writePage(path.back(), leafPage);
    free(recordEntry.keyData);
    free(leafPage);
    return SUCCESS;
}


RC IndexManager::scan(IXFileHandle &ixfileHandle,
        const Attribute &attribute,
        const void      *lowKey,
        const void      *highKey,
        bool            lowKeyInclusive,
        bool            highKeyInclusive,
        IX_ScanIterator &ix_ScanIterator)
{
    // We just intialize a scan iterator.
    return ix_ScanIterator.scanInit(ixfileHandle, attribute, lowKey, highKey, lowKeyInclusive, highKeyInclusive);
}

void IndexManager::printBtree(IXFileHandle &ixfileHandle, const Attribute &attribute){
    printf("Root: \n");
    postOrder(ixfileHandle, attribute, 0, false);
}

void IndexManager::postOrder(IXFileHandle &ixfileHandle, const Attribute &attribute, uint32_t pageNum, bool leaf){
    void * pageData = malloc(PAGE_SIZE);
    ixfileHandle.readPage(pageNum, pageData);

    if(!leaf){
        InteriorSlotHeader header;
        memcpy(&header, pageData, sizeof(struct InteriorSlotHeader));

        PagePointer child;
        printf("'KeyS': [");
        for(int offset = sizeof(InteriorSlotHeader) + sizeof(PagePointer); offset < header.offToFree; offset += sizeof(PagePointer)){
            if(attribute.type == 2){
                printf("Implement me!\n");
            } else if(attribute.type == 1){
                float cKey;
                memcpy(&cKey, ((char*) pageData) + offset, sizeof(int));
                printf("'%f',", cKey);
                offset += sizeof(int);
            } else {
                int cKey;
                memcpy(&cKey, ((char*) pageData) + offset, sizeof(int));
                printf("'%d',", cKey);
                offset += sizeof(int);
            }       
        }
        printf("],\n");
        for(int offset = sizeof(InteriorSlotHeader); offset < header.offToFree; offset += sizeof(int)){
            if(attribute.type == 2){
                printf("Implement me!\n");
            } else if(attribute.type == 1){
                leaf = header.isAboveLeaf;
                memcpy(&child, ((char*) pageData) + offset, sizeof(PagePointer));
                postOrder(ixfileHandle, attribute, child, leaf);
                offset += sizeof(PagePointer);
            } else {
                leaf = header.isAboveLeaf;
                memcpy(&child, ((char*) pageData) + offset, sizeof(PagePointer));
                postOrder(ixfileHandle, attribute, child, leaf);
                offset += sizeof(PagePointer);
            }
        }
    } else {
        LeafSlotHeader header;
        memcpy(&header, pageData, sizeof(struct LeafSlotHeader));

        uint16_t * offsetArray = (uint16_t*) malloc((header.numRecords + 1)*sizeof(uint16_t));
        memcpy(offsetArray + 1, ((char*) pageData) + sizeof(LeafSlotHeader), (header.numRecords)*sizeof(uint16_t));
        offsetArray[0] = sizeof(LeafSlotHeader) + sizeof(uint16_t)*header.numRecords;

        void * key;

        if(attribute.type == 0){
            int key; 
            RID rid;
            int numRIDs = 0;
            int offInPage;
            printf("{'Keys': [");
            for(int i = 0; i < header.numRecords; i++){
                memcpy(&key, ((char*) pageData) + offsetArray[i], sizeof(int));
                memcpy(&offInPage, ((char*) pageData) + offsetArray[i] + sizeof(int), sizeof(uint16_t));
                memcpy(&numRIDs, ((char*) pageData) + offsetArray[i] + sizeof(int) + sizeof(uint16_t), sizeof(uint16_t));
                printf("'%d:[", key);
                for(int j = 0; j < numRIDs; j++){
                    memcpy(&rid, ((char*) pageData) + offInPage + j*sizeof(RID), sizeof(RID));
                    printf("(%d,%d),", rid.pageNum, rid.slotNum);
                }
                printf("]',");
            }
            free(offsetArray);
        } else if(attribute.type == 1){
            float key; 
            RID rid;
            int numRIDs = 0;
            int offInPage;
            printf("{'Keys': [");
            for(int i = 0; i < header.numRecords; i++){
                memcpy(&key, ((char*) pageData) + offsetArray[i], sizeof(int));
                memcpy(&offInPage, ((char*) pageData) + offsetArray[i] + sizeof(int), sizeof(uint16_t));
                memcpy(&numRIDs, ((char*) pageData) + offsetArray[i] + sizeof(int) + sizeof(uint16_t), sizeof(uint16_t));
                printf("'%f:[", key);
                for(int j = 0; j < numRIDs; j++){
                    memcpy(&rid, ((char*) pageData) + offInPage + j*sizeof(RID), sizeof(RID));
                    printf("(%d,%d),", rid.pageNum, rid.slotNum);
                }
                printf("]',");
            }
            free(offsetArray);
        } else {
            free(offsetArray);
            free(pageData);
            return;
        }
    }
    free(pageData);
    return;
}

IX_ScanIterator::IX_ScanIterator()
: currRID(0), totalRID(0), currSlot(0), totalSlot(0), nextPage(0)
{
    ix = IndexManager::instance();
}

RC IX_ScanIterator::scanInit(IXFileHandle &fh,
        const Attribute &attr,
        const void      *lk,
        const void      *hk,
        bool            lkInc,
        bool            hkInc)
{
    // We need to know the RIDs for the current slot, the slots for the current page, and a pointer to the next page.
    currRID = 0;
    totalRID = 0;
    currSlot = 0;
    totalSlot = 0;
    nextPage = 0;

    // Keep a buffer to hold the current page.
    leafPage = malloc(PAGE_SIZE);

    // Store the variables passed in.
    ixfileHandle = fh;
    attribute.name = attr.name;
    attribute.type = attr.type;
    attribute.length = attr.length;
    lowKey = lk;
    highKey = hk;
    lowKeyInclusive = lkInc;
    highKeyInclusive = hkInc;

    // If lowKey is null then we need to start with the first entry in the file.
    if (lowKey == NULL)
    {
        // We get the first leaf page's number then read it in.
        PageNum pageNum = ix->getFirstLeafPageNum(ixfileHandle, 0);
        if (pageNum == -1)
            return RBFM_READ_FAILED;

        if (fh.readPage(pageNum, leafPage))
            return RBFM_READ_FAILED;

        // We read in the header and update some global variables.
        header = ix->getLeafSlotHeader(leafPage);
        totalSlot = header.numRecords;
        nextPage = header.nextPage;

        // If the page has no slots, we return.
        if (totalSlot == 0)
            return IX_EOF;

        // We get the last global we need from the record entry.
        recordEntry = ix->getLeafDirectoryRecordEntry(leafPage, currSlot);
        totalRID = recordEntry.numRIDs;
    }
    else
    {
        // Otherwise we get the page that the record is on.
        vector<PagePointer> path;
        // Get total number of pages
        RC rc = ix->getLeafPage(ixfileHandle, attribute, lowKey, leafPage, path);
        if(rc)
            return rc;

        // Get number of slots on first page
        header = ix->getLeafSlotHeader(leafPage);
        totalSlot = header.numRecords;

        // We find the slot num that lowKey is in, or the next slot if lowKey doesn't exist.
        for (uint32_t i = 0; i < totalSlot; i++)
        {
            LeafDirectoryRecordEntry currentEntry = ix->getLeafDirectoryRecordEntry(leafPage, i);
            InsertStatus result = ix->compareKeys(attribute, currentEntry.keyData, lowKey);
            free(currentEntry.keyData);
            if (result == ON_PAGE)
            {
                if (lowKeyInclusive == true)
                {
                    currSlot = i;
                    totalRID = currentEntry.numRIDs;
                }
                else
                {
                    currSlot = i+1;
                    currentEntry = ix->getLeafDirectoryRecordEntry(leafPage, i+1);
                    totalRID = currentEntry.numRIDs;
                }
                break;
            }
            else if (result == NEED_INSERT)
            {
                currSlot = i;
                totalRID = currentEntry.numRIDs;
                break;
            }
        }

        recordEntry = ix->getLeafDirectoryRecordEntry(leafPage, currSlot);
    }

    return SUCCESS;
}

RC IX_ScanIterator::getNextEntry(RID &rid, void *key)
{
    // We reset the header and record entry
    header = ix->getLeafSlotHeader(leafPage);
    free(recordEntry.keyData);
    recordEntry = ix->getLeafDirectoryRecordEntry(leafPage, currSlot);

    // We check to see if we need to move on to the next slot.
    if (currRID >= totalRID)
    {
        currRID = 0;
        currSlot++;
        RC rc = getNextSlot();
        if (rc)
            return rc;
    }

    // If there is a high key we check to see if we are done.
    if (highKey != NULL)
    {
        InsertStatus result = ix->compareKeys(attribute, recordEntry.keyData, highKey);
        if ((result == ON_PAGE && highKeyInclusive == false) || result == NEED_INSERT)
        {
            return IX_EOF;
        }
    }

    // We copy the rid from the page to the return value.
    memcpy(&rid.pageNum, (char *)leafPage + recordEntry.offInPage + currRID * sizeof(RID), sizeof(uint32_t));
    memcpy(&rid.slotNum, (char *)leafPage + recordEntry.offInPage + currRID * sizeof(RID) + sizeof(uint32_t), sizeof(uint32_t));

    // We copy the key from the page into the return value.
    int keySize = ix->getKeySize(attribute, recordEntry.keyData);
    LeafDirectoryOffset recordOffset = ix->getLeafDirectoryRecordOffset(leafPage, currSlot);
    memcpy(&key, (char *)leafPage + recordOffset, keySize);

    // We increment the RID.
    currRID++;


    return SUCCESS;
}

RC IX_ScanIterator::close()
{
    // Just free some global variables.
    free(recordEntry.keyData);
    free(leafPage);
    return SUCCESS;
}


// All of the IXFileHandle functions just keep track of counters and call the pf equivalent.
IXFileHandle::IXFileHandle()
{
    ixReadPageCounter = 0;
    ixWritePageCounter = 0;
    ixAppendPageCounter = 0;
}

IXFileHandle::~IXFileHandle()
{
}

RC IXFileHandle::collectCounterValues(unsigned &readPageCount, unsigned &writePageCount, unsigned &appendPageCount)
{
    readPageCount = ixReadPageCounter;
    writePageCount = ixWritePageCounter;
    appendPageCount = ixAppendPageCounter;
    return SUCCESS;
}

RC IXFileHandle::readPage(PageNum pageNum, void *data)
{
    ixReadPageCounter++;

    return handle.readPage(pageNum, data);
}


RC IXFileHandle::writePage(PageNum pageNum, const void *data)
{
    ixWritePageCounter++;

    return handle.writePage(pageNum, data);
}


RC IXFileHandle::appendPage(const void *data)
{
    ixAppendPageCounter++;

    return handle.appendPage(data);
}

RC IXFileHandle::getNumberOfPages()
{
    return handle.getNumberOfPages();
}


// SCANITERATOR HELPER FUNCTIONS

RC IX_ScanIterator::getNextSlot()
{
    // We check to see if we need to move to the next page
    if (currSlot >= totalSlot)
    {
        currSlot = 0;
        RC rc = getNextPage();
        if (rc)
            return rc;
    }

    // If not, we just regrab the record entry and update globals.
    free(recordEntry.keyData);
    recordEntry = ix->getLeafDirectoryRecordEntry(leafPage, currSlot);
    totalRID = recordEntry.numRIDs;

    return SUCCESS;
}

RC IX_ScanIterator::getNextPage()
{
    // We check to see if we moved to the l;ast page if so we reurn.
    if (header.nextPage == -1)
    {
        return IX_EOF;
    }

    // We read in the new page.
    if (ixfileHandle.readPage(nextPage, leafPage))
        return RBFM_READ_FAILED;

    // We update the haeder, record, and globals.
    header = ix->getLeafSlotHeader(leafPage);
    totalSlot = header.numRecords;
    nextPage = header.nextPage;

    free(recordEntry.keyData);
    recordEntry = ix->getLeafDirectoryRecordEntry(leafPage, currSlot);
    totalRID = recordEntry.numRIDs;

    return SUCCESS;
}


// HELPER FUNCTIONS

// Interior Page Functions
// Configures a new interior page.
void IndexManager::newInteriorPage(void * page)
{
    memset(page, 0, PAGE_SIZE);
    // Writes the slot directory header.
    InteriorSlotHeader interiorSlotHeader;
    interiorSlotHeader.numTrafficCops = 0;
    interiorSlotHeader.offToFree = sizeof(InteriorSlotHeader) + sizeof(PagePointer);
    interiorSlotHeader.isAboveLeaf = true;
    setInteriorSlotHeader(page, interiorSlotHeader);
}

InteriorSlotHeader IndexManager::getInteriorSlotHeader(void * page)
{
    // Getting the slot directory header.
    InteriorSlotHeader interiorSlotHeader;
    memcpy (&interiorSlotHeader, page, sizeof(InteriorSlotHeader));
    return interiorSlotHeader;
}

void IndexManager::setInteriorSlotHeader(void * page, InteriorSlotHeader interiorSlotHeader)
{
    // Setting the slot directory header.
    memcpy (page, &interiorSlotHeader, sizeof(InteriorSlotHeader));
}

void IndexManager::setInteriorPagePointer(void * page, uint16_t offset, PagePointer pointer)
{
    // Setting a pointer.
    memcpy  (
            ((char*) page + offset),
            &pointer,
            sizeof(PagePointer)
            );
}

// Computes the free space of a page (function of the free space pointer and the slot directory size).
unsigned IndexManager::getInteriorPageFreeSpaceSize(void * page) 
{
    InteriorSlotHeader interiorSlotHeader = getInteriorSlotHeader(page);
    return PAGE_SIZE - interiorSlotHeader.offToFree;
}



// Leaf Page Functions
// Configures a new leaf page.
void IndexManager::newLeafPage(void * page, PagePointer prevPage, PagePointer nextPage)
{
    memset(page, 0, PAGE_SIZE);
    // Writes the slot directory header.
    LeafSlotHeader leafSlotHeader;
    leafSlotHeader.offToFree = PAGE_SIZE;
    leafSlotHeader.numRecords = 0;
    leafSlotHeader.prevPage = prevPage;
    leafSlotHeader.nextPage = nextPage;
    setLeafSlotHeader(page, leafSlotHeader);
}

LeafSlotHeader IndexManager::getLeafSlotHeader(void * page)
{
    // Getting the slot directory header.
    LeafSlotHeader leafSlotHeader;
    memcpy (&leafSlotHeader, page, sizeof(LeafSlotHeader));
    return leafSlotHeader;
}

void IndexManager::setLeafSlotHeader(void * page, LeafSlotHeader leafSlotHeader)
{
    // Setting the slot directory header.
    memcpy (page, &leafSlotHeader, sizeof(LeafSlotHeader));
}

LeafDirectoryOffset IndexManager::getLeafDirectoryRecordOffset(void * page, unsigned recordEntryNumber)
{

    if( (int) recordEntryNumber < 0){
        return sizeof(struct LeafSlotHeader);
    }
  
    // Setting the slot directory entry data.
    LeafDirectoryOffset recordOffset;
    memcpy  (
            &recordOffset,
            (((char*) page) + sizeof(LeafSlotHeader) + recordEntryNumber * sizeof(LeafDirectoryOffset)),
            sizeof(LeafDirectoryOffset)
            );
    return recordOffset;
}

void IndexManager::setLeafDirectoryRecordOffset(void * page, unsigned recordEntryNumber, LeafDirectoryOffset recordOffset)
{
    // Setting the slot directory entry data.
    memcpy  (
            ((char*) page + sizeof(LeafSlotHeader) + recordEntryNumber * sizeof(LeafDirectoryOffset)),
            &recordOffset,
            sizeof(LeafDirectoryOffset)
            );
}

LeafDirectoryRecordEntry IndexManager::getLeafDirectoryRecordEntry(void * page, unsigned recordEntryNumber)
{
    LeafSlotHeader header = getLeafSlotHeader(page);

    // Getting the slot directory entry data.
    LeafDirectoryOffset startRecordOffset = sizeof(LeafSlotHeader) + header.numRecords * sizeof(LeafDirectoryOffset);
    if (recordEntryNumber != 0)
    {
        startRecordOffset = getLeafDirectoryRecordOffset(page, (recordEntryNumber - 1));
    }

    LeafDirectoryOffset endRecordOffset = getLeafDirectoryRecordOffset(page, recordEntryNumber);

    LeafDirectoryRecordEntry recordEntry;
    recordEntry.keyData = malloc((endRecordOffset - startRecordOffset) - 2 * sizeof(uint16_t));

    memcpy  (
            recordEntry.keyData,
            ((char*) page + startRecordOffset),
            (endRecordOffset - startRecordOffset) - 2 * sizeof(uint16_t)
            );
    memcpy  (
            &recordEntry.offInPage,
            ((char*) page + endRecordOffset - 2 * sizeof(uint16_t)),
            sizeof(uint16_t)
            );
    memcpy  (
            &recordEntry.numRIDs,
            ((char*) page + endRecordOffset - sizeof(uint16_t)),
            sizeof(uint16_t)
            );

    return recordEntry;
}

void IndexManager::setLeafDirectoryRecordEntry(void * page, const Attribute &attribute, unsigned recordEntryNumber, LeafDirectoryRecordEntry recordEntry)
{
    // Setting the slot directory entry data.
    LeafSlotHeader header = getLeafSlotHeader(page);

    LeafDirectoryOffset startRecordOffset = sizeof(LeafSlotHeader);
    if (recordEntryNumber != 0) {
        startRecordOffset = getLeafDirectoryRecordOffset(page, (recordEntryNumber - 1));
    }

    LeafDirectoryOffset endRecordOffset;
    if (recordEntryNumber >= header.numRecords)
    {
        unsigned keySize = getKeySize(attribute, recordEntry.keyData);
        endRecordOffset = startRecordOffset + keySize + 2*sizeof(uint16_t);
    }
    else
    {
        endRecordOffset = getLeafDirectoryRecordOffset(page, recordEntryNumber);
    }

    memcpy  (
            ((char*) page + startRecordOffset),
            recordEntry.keyData,
            (endRecordOffset - startRecordOffset) - 2 * sizeof(uint16_t)
            );
    memcpy  (
            ((char*) page + endRecordOffset - 2 * sizeof(uint16_t)),
            &recordEntry.offInPage,
            sizeof(uint16_t)
            );
    memcpy  (
            ((char*) page + endRecordOffset - sizeof(uint16_t)),
            &recordEntry.numRIDs,
            sizeof(uint16_t)
            );
}

// Computes the free space of a leaf page (function of the free space pointer and the slot directory size).
unsigned IndexManager::getLeafPageFreeSpaceSize(void * page) 
{
    LeafSlotHeader leafSlotHeader = getLeafSlotHeader(page);
    LeafDirectoryOffset lastRecordOffset;
    if (leafSlotHeader.numRecords == 0)
        lastRecordOffset = sizeof(LeafSlotHeader);
    else
        lastRecordOffset = getLeafDirectoryRecordOffset(page, (leafSlotHeader.numRecords - 1));
    return leafSlotHeader.offToFree - lastRecordOffset;
}


// Others
unsigned IndexManager::getKeySize(const Attribute &attribute, const void *key)
{
    // Just gets the key size.
    unsigned size = 0;
    switch (attribute.type)
    {
        case TypeInt:
            size += INT_SIZE;
        break;
        case TypeReal:
            size += REAL_SIZE;
        break;
        case TypeVarChar:
            uint32_t varcharSize;
            // We have to get the size of the VarChar field by reading the integer that precedes the string value itself
            memcpy(&varcharSize, (char*) key, VARCHAR_LENGTH_SIZE);
            size += varcharSize;
        break;
    }
    return size;
}

PagePointer IndexManager::getFirstLeafPageNum(IXFileHandle &ixfileHandle, PagePointer pageNum)
{
    // Read in the pageNum passed to the function and get the header.
    void * currentPage = calloc(PAGE_SIZE, 1);
    if(ixfileHandle.readPage(pageNum, currentPage))
    {
        free(currentPage);
        return -1;
    }
    InteriorSlotHeader currentHeader = getInteriorSlotHeader(currentPage);

    // Get the page pointer to the next page from the header
    PagePointer nextPageNum;
    memcpy(&nextPageNum, (char *)currentPage + sizeof(InteriorSlotHeader), sizeof(PagePointer));

    // If we are jut above the leaves, then we return the pointer.
    if (currentHeader.isAboveLeaf == true)
    {
        free(currentPage);
        return nextPageNum;
    }
    // Else we recurse.
    else
    {
        free(currentPage);
        return getFirstLeafPageNum(ixfileHandle, nextPageNum);
    }
}


// Insert Functions
RC IndexManager::getLeafPage(IXFileHandle &ixfileHandle, const Attribute &attribute, const void *key, void *leafPage, vector<PagePointer> &path)
{
    // We call traverse tree on the root, then we read the page num it gives back.
    PagePointer root = 0;
    path.push_back(root);
    PagePointer pageNum = traverseTree(ixfileHandle, attribute, key, root, path);
    
    if (ixfileHandle.readPage(pageNum, leafPage))
        return RBFM_READ_FAILED;

    return SUCCESS;
}

PagePointer IndexManager::traverseTree(IXFileHandle &ixfileHandle, const Attribute &attribute, const void *key, PagePointer pageNum, vector<PagePointer> &path)
{
    // We read in a page.
    void * currentPage = calloc(PAGE_SIZE, 1);
    
    if (ixfileHandle.readPage(pageNum, currentPage))
    {
        free(currentPage);
        return RBFM_READ_FAILED;
    }

    // Get the headr and intialize some variables.
    InteriorSlotHeader currentHeader = getInteriorSlotHeader(currentPage);
    
    uint32_t pointerOffset = sizeof(InteriorSlotHeader);
    uint32_t dataOffset = sizeof(InteriorSlotHeader) + sizeof(PagePointer);
    uint32_t length;
    PagePointer nextPage;

    // We go through the page to find where the pointer we want is at.
    void * currentKey = calloc(PAGE_SIZE, 1);
    for (int i = 0; i < currentHeader.numTrafficCops; i++)
    {
        // We copy the traffic cop for comparisons
        if (attribute.type == TypeVarChar)
        {
            memcpy(&length, (char *)currentPage + dataOffset, sizeof(uint32_t));
            dataOffset += sizeof(uint32_t);
            memcpy(currentKey, (char *)currentPage + dataOffset, length);
            dataOffset += length;
        }
        else
        {
            length = sizeof(uint32_t);
            memcpy(currentKey, (char *)currentPage + dataOffset, length);
            dataOffset += length;
        }

        // We compare the key, if the result is CONTINUE, we just keep seraching.
        InsertStatus result = compareKeys(attribute, currentKey, key);
        if (result == CONTINUE)
        {
            pointerOffset = dataOffset;
            dataOffset += sizeof(PagePointer);
        }
        // Otherwise, we grab the page pointer from behind the traffic cop, push it to the path and either return it or recurse.
        else
        {
            memcpy(&nextPage, (char *)currentPage + pointerOffset, sizeof(PagePointer));
            path.push_back(nextPage);
            if (currentHeader.isAboveLeaf == true)
            {
                free(currentKey);
                free(currentPage);
                return nextPage;
            }
            else
            {
                free(currentKey);
                free(currentPage);
                return traverseTree(ixfileHandle, attribute, key, nextPage, path);
            }
        }
    }

    // If we kept continueing, then we take the last pointer and do the ame as above.
    memcpy(&nextPage, (char *)currentPage + pointerOffset, sizeof(PagePointer));
    path.push_back(nextPage);
    if (currentHeader.isAboveLeaf == true)
    {
        free(currentKey);
        free(currentPage);
        return nextPage;
    }
    else
    {
        free(currentKey);
        free(currentPage);
        return traverseTree(ixfileHandle, attribute, key, nextPage, path);
    }
}

InsertStatus IndexManager::scanForKey (void *leafPage, const Attribute &attribute, const void *key, unsigned &slotNum)
{
    // Get the header, then scan the records.
    LeafSlotHeader leafHeader = getLeafSlotHeader(leafPage);
    for (int i = 0; i < leafHeader.numRecords; i++)
    {
        // We get the current record, then compare them.
        slotNum = i;
        LeafDirectoryRecordEntry currentEntry = getLeafDirectoryRecordEntry(leafPage, slotNum);
        unsigned currentKeySize = getKeySize(attribute, currentEntry.keyData);
        
        InsertStatus result = compareKeys(attribute, currentEntry.keyData, key);
        free(currentEntry.keyData);

        // we return based on the comparison, or continue.
        if (result == ON_PAGE)
        {
            return ON_PAGE;
        }
        else if (result == NEED_INSERT)
        {
            return NEED_INSERT;
        }
    }

    // If we kept continuing then it isn't on the page and should be added at the end.
    slotNum = leafHeader.numRecords;
    return NEED_INSERT;
}

InsertStatus IndexManager::compareKeys(const Attribute &attribute, const void *currentKey, const void *key)
{
    // A simple comparison based on type.
    switch (attribute.type)
    {
        case TypeInt:
            int currentKeyi, keyi;
            memcpy(&currentKeyi, currentKey, sizeof(int));
            memcpy(&keyi, key, sizeof(int));
            if (currentKeyi == keyi)
                return  ON_PAGE;
            else if (currentKeyi > keyi)
                return NEED_INSERT;
            else
                return CONTINUE;
        break;
        case TypeReal:
            float currentKeyf, keyf;
            memcpy(&currentKeyf, currentKey, sizeof(float));
            memcpy(&keyf, key, sizeof(float));
            if (currentKeyf == keyf)
                return  ON_PAGE;
            else if (currentKeyf > keyf)
                return NEED_INSERT;
            else
                return CONTINUE;
        break;
        case TypeVarChar:
            int cmp = strcmp((char*)currentKey, (char*)key);
            if (cmp == 0)
                return  ON_PAGE;
            else if (cmp > 0)
                return NEED_INSERT;
            else
                return CONTINUE;
        break;
    }
    return CONTINUE;
}


void IndexManager::updateExistingKey(void *leafPage, const Attribute &attribute, unsigned slotNum, const RID &rid)
{
    // We get the header, record, and the set of all RIDs associated with the key.
    LeafSlotHeader leafHeader = getLeafSlotHeader(leafPage);
    LeafDirectoryRecordEntry record = getLeafDirectoryRecordEntry(leafPage, slotNum);
    void *rids = calloc(record.numRIDs + 1, sizeof(RID));
    memcpy(rids, (char *)leafPage + record.offInPage, record.numRIDs*sizeof(RID));

    // We add the new RID to that memory.
    memcpy((char*)rids + record.numRIDs*sizeof(RID), &rid, sizeof(RID));

    // We temporarily set the numRIDs to 0 to reorganize the page.
    uint16_t tempNumRIDs = 0;
    LeafDirectoryOffset endOfRecord = getLeafDirectoryRecordOffset(leafPage, slotNum);

    memcpy (((char*) leafPage + endOfRecord - sizeof(uint16_t)), &tempNumRIDs, sizeof(uint16_t));

    reorganizeLeafPage(leafPage, attribute);

    // We reset the header.
    leafHeader = getLeafSlotHeader(leafPage);
    uint16_t offset = leafHeader.offToFree;

    // We add in all of the RIDs asscoiated with the key at the free space offset and update that.
    offset -= (record.numRIDs + 1) * sizeof(RID);
    memcpy((char *)leafPage + offset, rids, (record.numRIDs + 1) * sizeof(RID));

    leafHeader.offToFree = offset;
    setLeafSlotHeader(leafPage, leafHeader);

    // We then update the record entry with the correct info.
    record.offInPage = offset;
    record.numRIDs += 1;
    memcpy (((char*) leafPage + endOfRecord - 2 * sizeof(uint16_t)), &record.offInPage, sizeof(uint16_t));
    memcpy (((char*) leafPage + endOfRecord - sizeof(uint16_t)), &record.numRIDs, sizeof(uint16_t));

    free(record.keyData);
    free(rids);
}

void IndexManager::addNewKey(void *leafPage, const Attribute &attribute, unsigned slotNum, const void *key, const RID &rid)
{
    // We get the header and add in the new RID.
    LeafSlotHeader leafHeader = getLeafSlotHeader(leafPage);
    
    leafHeader.offToFree -= sizeof(RID);
    memcpy((char *)leafPage + leafHeader.offToFree, &rid, sizeof(RID));

    // We get the key size for later.
    unsigned keySize = getKeySize(attribute, key);
    int slotSize = keySize + 2*sizeof(uint16_t);

    // We move everything from the offset of slotnum over by one offset.
    LeafDirectoryOffset beginOffsetMove = sizeof(LeafSlotHeader) + slotNum * sizeof(LeafDirectoryOffset);
    LeafDirectoryOffset endOffsetMove = beginOffsetMove;
    if (leafHeader.numRecords != 0)
        endOffsetMove = getLeafDirectoryRecordOffset(leafPage, leafHeader.numRecords - 1);

    memmove((char *)leafPage + beginOffsetMove + sizeof(LeafDirectoryOffset), (char *)leafPage + beginOffsetMove, endOffsetMove - beginOffsetMove);

    // Then we update all of the offsets to be correct again with this move.
    for (int i = 0; i <= leafHeader.numRecords; i++)
    {
        if(i != slotNum)
        {
            LeafDirectoryOffset currentRecordOffset = getLeafDirectoryRecordOffset(leafPage, i);
            currentRecordOffset += sizeof(LeafDirectoryOffset);
            setLeafDirectoryRecordOffset(leafPage, i, currentRecordOffset);
        }
    }

    // Now we move everything from the entry of slotnum over by the size of the record to insert.
    LeafDirectoryOffset beginSlotMove = sizeof(LeafSlotHeader) + (leafHeader.numRecords + 1) * sizeof(LeafDirectoryOffset);
    if (slotNum != 0)
        beginSlotMove = getLeafDirectoryRecordOffset(leafPage, slotNum - 1);
    LeafDirectoryOffset endSlotMove = beginSlotMove;
    if (leafHeader.numRecords != 0)
    {
        if (slotNum >= leafHeader.numRecords)
            endSlotMove = endOffsetMove + sizeof(LeafDirectoryOffset);
        else
            endSlotMove = getLeafDirectoryRecordOffset(leafPage, slotNum + 1);
    }
    memmove((char *)leafPage + beginSlotMove + slotSize, (char *)leafPage + beginSlotMove, endSlotMove - beginSlotMove);

    // We set the offset for the new record.
    LeafDirectoryOffset newRecordOffset = beginSlotMove + slotSize;
    setLeafDirectoryRecordOffset(leafPage, slotNum, newRecordOffset);

    // We set the record for the new record
    LeafDirectoryRecordEntry newRecord;
    newRecord.keyData = malloc(keySize);
    memcpy(newRecord.keyData, key, keySize);
    newRecord.offInPage = leafHeader.offToFree;
    newRecord.numRIDs = 1;

    memcpy((char *)leafPage + beginSlotMove, newRecord.keyData, keySize);
    memcpy((char *)leafPage + beginSlotMove + keySize, &newRecord.offInPage, sizeof(uint16_t));
    memcpy((char *)leafPage + beginSlotMove + keySize + sizeof(uint16_t), &newRecord.numRIDs, sizeof(uint16_t));

    // We update the header.
    leafHeader.numRecords += 1;
    setLeafSlotHeader(leafPage, leafHeader);

    // Lastly we fix all of the offsets that were changed by the second move.
    for (int i = slotNum + 1; i < leafHeader.numRecords; i++)
    {
        LeafDirectoryOffset currentRecordOffset = getLeafDirectoryRecordOffset(leafPage, i);
        currentRecordOffset += slotSize;
        setLeafDirectoryRecordOffset(leafPage, i, currentRecordOffset);
    }

    free(newRecord.keyData);

}

// This is almost entirely taken from the given code for assignment 2, with modifications to fit our setup.
void IndexManager::reorganizeLeafPage(void *page, const Attribute &attribute)
{
    LeafSlotHeader header = getLeafSlotHeader(page);

    // Add all live records to vector, keeping track of slot numbers
    vector<IndexedLeafRecordEntry> liveRecords;
    for (unsigned i = 0; i < header.numRecords; i++)
    {
        IndexedLeafRecordEntry entry;
        entry.slotNum = i;
        entry.recordEntry = getLeafDirectoryRecordEntry(page, i);
        if (entry.recordEntry.numRIDs != 0)
            liveRecords.push_back(entry);
        else
            free(entry.recordEntry.keyData);
    }
    // Sort records by offset, descending
    auto comp = [](IndexedLeafRecordEntry first, IndexedLeafRecordEntry second) 
        {return first.recordEntry.offInPage > second.recordEntry.offInPage;};
    sort(liveRecords.begin(), liveRecords.end(), comp);

    // Move each record back filling in any gap preceding the record
    uint16_t pageOffset = PAGE_SIZE;
    LeafDirectoryRecordEntry current;
    LeafDirectoryOffset endOfRecord;
    for (unsigned i = 0; i < liveRecords.size(); i++)
    {
        current = liveRecords[i].recordEntry;
        pageOffset -= current.numRIDs * sizeof(RID);

        // Use memmove rather than memcpy because locations may overlap
        memmove((char*)page + pageOffset, (char*)page + current.offInPage, current.numRIDs * sizeof(RID));
        current.offInPage = pageOffset;
        //setLeafDirectoryRecordEntry(page, attribute, liveRecords[i].slotNum, current);
        endOfRecord = getLeafDirectoryRecordOffset(page, liveRecords[i].slotNum);
        memcpy (((char*) page + endOfRecord - 2 * sizeof(uint16_t)), &current.offInPage, sizeof(uint16_t));

        free(current.keyData);
    }
    header.offToFree = pageOffset;
    setLeafSlotHeader(page, header);
}

void IndexManager::splitRoot(IXFileHandle &ixfileHandle, Attribute attr){
    void * rootPage = malloc(PAGE_SIZE);
    ixfileHandle.readPage(0, rootPage);
    InteriorSlotHeader rootHead;
    memcpy(&rootHead, rootPage, sizeof(struct InteriorSlotHeader));

    int offsetInRoot, length;
    int numPointers = 0;
    void * splitKey;

    // Find out where to split the page
    if(attr.type == 2){
        splitKey = malloc(PAGE_SIZE);
        for(offsetInRoot = sizeof(struct InteriorSlotHeader) + sizeof(PagePointer); offsetInRoot < PAGE_SIZE/2; offsetInRoot += sizeof(PagePointer)){
            numPointers++;
            memcpy(&length, ((char*) rootPage) + offsetInRoot, sizeof(int));
            offsetInRoot += sizeof(int);
            memcpy(splitKey, ((char*) rootPage) + offsetInRoot, length);
            offsetInRoot += length;
        }
    } else {
        splitKey = malloc(sizeof(int));
        length = sizeof(int);
        numPointers = 256;
        offsetInRoot = sizeof(struct InteriorSlotHeader) + numPointers*(sizeof(PagePointer)) + (numPointers - 1)*length;
        memcpy(splitKey, ((char*) rootPage) + offsetInRoot, length);
        offsetInRoot += length;
    }

    // The Frist half of the root
    void * tempPageLeft = calloc(PAGE_SIZE, 1);

    InteriorSlotHeader tempLeft;
    memcpy(&tempLeft, rootPage, sizeof(struct InteriorSlotHeader));

    memcpy(tempPageLeft, rootPage, offsetInRoot - length);
    tempLeft.numTrafficCops  = numPointers;
    tempLeft.offToFree = offsetInRoot - length;
    // keep isAboveLeaf value from root
    memcpy(tempPageLeft, &tempLeft, sizeof(struct InteriorSlotHeader));

    void * tempPageRight = calloc(PAGE_SIZE, 1);

    InteriorSlotHeader tempRight;
    memcpy(&tempRight, rootPage, sizeof(struct InteriorSlotHeader));

    memcpy(((char*) tempPageRight) + sizeof(struct InteriorSlotHeader), ((char*) rootPage) + offsetInRoot, rootHead.offToFree - offsetInRoot);
    tempRight.numTrafficCops  = rootHead.numTrafficCops - numPointers;
    tempRight.offToFree = (rootHead.offToFree - offsetInRoot) + sizeof(struct InteriorSlotHeader);
    // keep isAboveLeaf value from root
    memcpy(tempPageRight, &tempRight, sizeof(struct InteriorSlotHeader));

    ixfileHandle.appendPage(tempPageLeft);
    ixfileHandle.appendPage(tempPageRight);
    free(tempPageLeft);
    free(tempPageRight);

    memset(rootPage, 0, PAGE_SIZE);
    rootHead.numTrafficCops = 1;
    rootHead.offToFree = sizeof(struct InteriorSlotHeader) + 2*sizeof(PagePointer) + length;
    rootHead.isAboveLeaf = false;

    uint32_t numPages = ixfileHandle.getNumberOfPages();
    numPages -= 2;

    //memcpy in: rootHead struct, pointer to left page, splitkey, pointer to right page
    memcpy(rootPage                                                                          , &rootHead , sizeof(struct InteriorSlotHeader));
    memcpy(((char*) rootPage) + sizeof(struct InteriorSlotHeader)                            , &numPages , sizeof(uint32_t));
    memcpy(((char*) rootPage) + sizeof(struct InteriorSlotHeader) + sizeof(uint32_t)         , splitKey  , length);
    numPages++; 
    memcpy(((char*) rootPage) + sizeof(struct InteriorSlotHeader) + sizeof(uint32_t) + length, &numPages , sizeof(uint32_t));

    ixfileHandle.writePage(0, rootPage);

    free(splitKey);
    free(rootPage);

    return;
}

void IndexManager::insertInt(IXFileHandle &ixfileHandle, uint32_t pageNum, void * keyValue, int pointer, Attribute attr, vector<uint32_t> &path){

    void * intPage = malloc(PAGE_SIZE);
    ixfileHandle.readPage(pageNum, intPage);

    int keyValueLength;
    void * keyValueVal;
    
    if(attr.type == 2){
        memcpy(&keyValueLength, keyValue, sizeof(int));
        keyValueVal = malloc(keyValueLength);
        memcpy(keyValueVal, ((char*) keyValue) + sizeof(int), keyValueLength);
    } 

    InteriorSlotHeader intHead;
    memcpy(&intHead, intPage, sizeof(struct InteriorSlotHeader));

    int insertSize;
    if(attr.type == 2){
        insertSize = 2*sizeof(int) + keyValueLength;
    } else {
        insertSize = 2*sizeof(int);
    }

    void * checkKey;
    if(PAGE_SIZE - intHead.offToFree < insertSize){
        if(pageNum == 0){
            // Cascading split into root
            // If root splits there will only be two pages to check on the depth with split
            splitRoot(ixfileHandle, attr);
            void * rootPage = malloc(PAGE_SIZE);
            ixfileHandle.readPage(0, rootPage);
            int newPageNum;
            if(attr.type == 2){
                int length;
                checkKey = malloc(PAGE_SIZE);
                memcpy(&length, ((char*) rootPage) + sizeof(struct InteriorSlotHeader) + sizeof(int), sizeof(int));
                memcpy(checkKey, ((char*) rootPage) + sizeof(struct InteriorSlotHeader) + 2*sizeof(int), length);
                if(compareKeys(attr, checkKey,  keyValueVal) == NEED_INSERT){
                    memcpy(&newPageNum, ((char*) rootPage) + sizeof(struct InteriorSlotHeader), sizeof(int));
                } else {
                    memcpy(&newPageNum, ((char*) rootPage) + sizeof(struct InteriorSlotHeader) + 2*sizeof(int) + length, sizeof(int));
                }
            } else {
                checkKey = malloc(sizeof(int));
                memcpy(checkKey, ((char*) rootPage) + sizeof(struct InteriorSlotHeader) + sizeof(int), sizeof(int));
                if(compareKeys(attr, checkKey,  keyValue) == NEED_INSERT){
                    memcpy(&newPageNum, ((char*) rootPage) + sizeof(struct InteriorSlotHeader), sizeof(int));
                } else {
                    memcpy(&newPageNum, ((char*) rootPage) + sizeof(struct InteriorSlotHeader) + 2*sizeof(int), sizeof(int));
                }
            }
            path.push_back(newPageNum);
            insertInt(ixfileHandle, newPageNum, keyValue, pointer, attr, path);
            free(rootPage);
            free(checkKey);
            return;
        } else {
            // Cascading split into non root
            // If not root There may be multiple pages on the parent of the splitting page
            splitInt(ixfileHandle, path, pageNum, attr);

            int parentPageNum;
            for(unsigned i = 0; i < path.size(); i++){
                if(path[i] == pageNum)
                    break;
                parentPageNum = path[i];
            }

            void * parentPage = malloc(PAGE_SIZE);
            ixfileHandle.readPage(parentPageNum, parentPage);

            void * checkKey;
            int offsetInParent = sizeof(InteriorSlotHeader);
            int newPageNum;
            if(attr.type == 2){
                int length; 
                checkKey = malloc(PAGE_SIZE);
                while(offsetInParent < intHead.offToFree){
                    memcpy(&newPageNum, ((char*) parentPage) + offsetInParent, sizeof(int));
                    offsetInParent += sizeof(int);
                    memcpy(&length, ((char*) parentPage) + offsetInParent, sizeof(int));
                    offsetInParent += sizeof(int);
                    memcpy(checkKey, ((char*) parentPage) + offsetInParent, length);
                    offsetInParent += length;
                    if(compareKeys(attr, checkKey, keyValueVal) == NEED_INSERT)
                        break;
                }
            } else {
                checkKey = malloc(sizeof(int));
                while(offsetInParent < PAGE_SIZE){
                    memcpy(&newPageNum, ((char*) parentPage) + offsetInParent, sizeof(int));
                    offsetInParent += sizeof(int);
                    memcpy(checkKey, ((char*) parentPage) + offsetInParent, sizeof(int));
                    offsetInParent += sizeof(int);
                    if(compareKeys(attr, checkKey, keyValue) == NEED_INSERT)
                        break;
                }
            }
            free(checkKey);
            path.pop_back();
            path.push_back(newPageNum);
            insertInt(ixfileHandle, newPageNum, keyValue, pointer, attr, path);
        }
    } else {
        // Non cascading split
        int intOffset;
        if(attr.type == 2){
            int length;
            checkKey = malloc(PAGE_SIZE);
            for(intOffset = sizeof(struct InteriorSlotHeader); intOffset < intHead.offToFree; intOffset += 4){
                memcpy(&length, ((char*) intPage) + intOffset, sizeof(int));
                intOffset += sizeof(int);
                memcpy(checkKey, ((char*) intPage) + intOffset, length);
                intOffset += length;
                if(compareKeys(attr, checkKey, keyValueVal) == NEED_INSERT)
                    break;
            }
        } else {
            checkKey = malloc(sizeof(int));
            for(intOffset = sizeof(struct InteriorSlotHeader); intOffset < intHead.offToFree; intOffset += 4){
                memcpy(checkKey, ((char*) intPage) + intOffset, sizeof(int));
                intOffset += 4;
                if(compareKeys(attr, checkKey, keyValue) == NEED_INSERT)
                    break;
            }
        } 
        intOffset += 4;

        if(intOffset > intHead.offToFree)
            intOffset = intHead.offToFree;

        memmove(((char*) intPage) + intOffset + insertSize, ((char*) intPage) + intOffset, intHead.offToFree - intOffset);
        memcpy(((char*) intPage) + intOffset, keyValue, insertSize - sizeof(int));
        memcpy(((char*) intPage) + intOffset + insertSize - sizeof(int), &pointer, sizeof(int));

        intHead.offToFree += insertSize;
        intHead.numTrafficCops += 1;

        memcpy(intPage, &intHead, sizeof(struct InteriorSlotHeader));

        free(checkKey);
    }

    ixfileHandle.writePage(pageNum, intPage);
    free(intPage);
    return;
}

void IndexManager::splitInt(IXFileHandle &ixfileHandle, vector<uint32_t> &path, uint32_t splitPageNum, Attribute attr){
    void * splitPage = malloc(PAGE_SIZE);
    
    int parentPageNum;
    for(unsigned i = 0; i < path.size(); i++){
        if(path[i] == splitPageNum)
            break;
        parentPageNum = path[i];
    }

    ixfileHandle.readPage(splitPageNum, splitPage);

    int offsetInSplit, length;
    int numPointers = 0;
    void * splitKey = malloc(PAGE_SIZE);

    if(attr.type == 2){
        for(offsetInSplit = sizeof(struct InteriorSlotHeader); offsetInSplit < PAGE_SIZE/2; offsetInSplit += 4){
            numPointers++;
            memcpy(&length, ((char*) splitPage) + offsetInSplit, sizeof(int));
            memcpy(splitKey, ((char*) splitPage) + offsetInSplit, sizeof(int) + length);
            offsetInSplit += (sizeof(int) + length);
        }
    } else {
        length = 4;
        offsetInSplit = sizeof(struct InteriorSlotHeader) + 256*sizeof(PagePointer) + 255*sizeof(int);
        numPointers = 256;
        memcpy(splitKey, ((char*) splitPage) + offsetInSplit, length);
        offsetInSplit += 4;
    }

    InteriorSlotHeader splitHead;
    memcpy(&splitHead, splitPage, sizeof(struct InteriorSlotHeader));
    
    void * newPage = calloc(1, PAGE_SIZE);

    InteriorSlotHeader newHead;
    memcpy(&newHead, splitPage, sizeof(struct InteriorSlotHeader));

    memcpy(((char*) newPage) + sizeof(struct InteriorSlotHeader), ((char*) splitPage) + offsetInSplit, splitHead.offToFree - offsetInSplit);
    // keep isAboveLeaf value from splitPage
    newHead.numTrafficCops  = splitHead.numTrafficCops - numPointers;
    newHead.offToFree = (splitHead.offToFree - offsetInSplit) + sizeof(struct InteriorSlotHeader);

    memcpy(newPage, &newHead, sizeof(struct InteriorSlotHeader));

    splitHead.numTrafficCops  = numPointers;
    splitHead.offToFree = offsetInSplit;

    memcpy(splitPage, &splitHead, sizeof(struct InteriorSlotHeader));

    ixfileHandle.appendPage(newPage);
    ixfileHandle.writePage(splitPageNum, splitPage);

    free(newPage);
    free(splitPage);

    path.pop_back();

    uint32_t newPageNum = ixfileHandle.handle.getNumberOfPages() - 1;

    insertInt(ixfileHandle, parentPageNum, splitKey, newPageNum, attr, path);

    free(splitKey);
 
    return;
}

void IndexManager::splitLeaf(IXFileHandle &ixfileHandle, vector<uint32_t> &path, Attribute attr){
    void * leafPage = malloc(PAGE_SIZE);
    int leafPageNum = path[path.size() - 1];
    int parentPageNum = path[path.size() - 2];;

    ixfileHandle.readPage(leafPageNum, leafPage);

    LeafSlotHeader leafHeader;

    memcpy(&leafHeader, leafPage, sizeof(struct LeafSlotHeader));

    int totRIDs = leafHeader.offToFree / sizeof(RID);

    uint16_t *offsetArray = (uint16_t*) malloc((leafHeader.numRecords) * sizeof(uint16_t));
    memcpy(offsetArray, ((char*) leafPage) + sizeof(struct LeafSlotHeader), leafHeader.numRecords*sizeof(uint16_t));
    
    int count = 0;
    int i;
    uint16_t numRIDs;
    for(i = 0; i < leafHeader.numRecords; i++){
        memcpy(&numRIDs, ((char*) leafPage) + offsetArray[i] - sizeof(uint16_t), sizeof(uint16_t));
        count += numRIDs;
        if(count >= totRIDs/2)
            break;
    }

    void * insertKey = malloc(PAGE_SIZE);

    if(attr.type == 2){
        int length;
        memcpy(&length, ((char*) leafPage) + offsetArray[i], sizeof(int));
        memcpy(insertKey, ((char*) leafPage) + offsetArray[i] + sizeof(int), length);
    } else {
        memcpy(insertKey, ((char*) leafPage) + offsetArray[i], sizeof(int));
    }

    // Setup new page
    void * newPage = calloc(1, PAGE_SIZE);
    LeafSlotHeader newHeader;
    newHeader.offToFree = PAGE_SIZE - (totRIDs - count)*sizeof(RID);
    newHeader.numRecords = leafHeader.numRecords - (i + 1);
    newHeader.prevPage = leafPageNum;
    newHeader.nextPage = leafHeader.nextPage;

    memcpy(newPage, &newHeader, sizeof(struct LeafSlotHeader));

    uint16_t offsetEnd = offsetArray[leafHeader.numRecords - 1];
    uint16_t offsetBreak = offsetArray[i];
    uint16_t offsetStart = sizeof(struct LeafSlotHeader) + sizeof(uint16_t)*leafHeader.numRecords;

    int newOffsetToFree = PAGE_SIZE;
    uint16_t offsetJ;
    for(int j = i + 1; j < leafHeader.numRecords; j++){
        // Get offset and numRids from slot j in leaf
        memcpy(&numRIDs, ((char*) leafPage) + offsetArray[j] - sizeof(uint16_t), sizeof(uint16_t));
        memcpy(&offsetJ, ((char*) leafPage) + offsetArray[j] - 2*sizeof(uint16_t), sizeof(uint16_t));
    
        newOffsetToFree -= numRIDs*sizeof(RID);
        // copy RID chunk to temp page, and put its offset on the corresponding entry in leafPage
        
        memcpy(((char*) newPage) + newOffsetToFree, ((char*) leafPage) + offsetJ, numRIDs*sizeof(RID));
        memcpy(((char*) leafPage) + offsetArray[j] - 2*sizeof(uint16_t), &newOffsetToFree, sizeof(uint16_t));
    }

    for(int j = i + 1; j < leafHeader.numRecords; j++){
        offsetArray[j] -= ((i+1)*sizeof(uint16_t) + (offsetArray[i] - (sizeof(LeafSlotHeader) + leafHeader.numRecords*sizeof(uint16_t))));
    }

    memcpy(((char*) newPage) + sizeof(struct LeafSlotHeader), offsetArray + i + 1, sizeof(uint16_t)*newHeader.numRecords);

    memcpy(((char*) newPage) + sizeof(struct LeafSlotHeader) + newHeader.numRecords*sizeof(uint16_t),
          ((char*) leafPage) + offsetArray[i], 
          offsetEnd - offsetArray[i]);
    // New page complete

    // Slide entries to end of retained offsets
    uint16_t slideDist = sizeof(uint16_t)*(leafHeader.numRecords - (i + 1));
    memmove(((char*) leafPage) + sizeof(struct LeafSlotHeader) + sizeof(uint16_t)*(i + 1), 
           ((char*) leafPage) + offsetStart,
           offsetBreak - offsetStart); 

    // Set adjusted offsets
    for(int j = 0; j <= i; j++){
        offsetArray[j] -= slideDist;
        memcpy(((char*) leafPage) + sizeof(LeafSlotHeader) + sizeof(uint16_t)*j, offsetArray + j, sizeof(uint16_t));
    }

    newOffsetToFree = PAGE_SIZE;
    void * tempPage = calloc(1, PAGE_SIZE);

    // Reorginize RID chunkc (compact)
    for(int j = 0; j <= i; j++){
        // Get offset and numRids from slot j in leaf
        memcpy(&numRIDs, ((char*) leafPage) + offsetArray[j] - sizeof(uint16_t), sizeof(uint16_t));
        memcpy(&offsetJ, ((char*) leafPage) + offsetArray[j] - 2*sizeof(uint16_t), sizeof(uint16_t));

        newOffsetToFree -= numRIDs*sizeof(RID);
        // copy RID chunk to temp page, and put its offset on the corresponding entry in leafPage
        memcpy(((char*) tempPage) + newOffsetToFree, ((char*) leafPage) + offsetJ, numRIDs*sizeof(RID));
        memcpy(((char*) leafPage) + offsetArray[j] - 2*sizeof(uint16_t), &newOffsetToFree, sizeof(uint16_t));
    }
    // Put the RID chunks from tempPage onto leafPage which has updated offsets
    memcpy(((char*) leafPage) + newOffsetToFree, ((char*) tempPage) + newOffsetToFree, PAGE_SIZE - newOffsetToFree);

    ixfileHandle.appendPage(newPage);

    leafHeader.offToFree = newOffsetToFree;
    leafHeader.numRecords = i + 1;
    leafHeader.nextPage = ixfileHandle.getNumberOfPages() - 1;

    memcpy(leafPage, &leafHeader, sizeof(struct LeafSlotHeader));

    ixfileHandle.writePage(leafPageNum, leafPage);

    free(offsetArray);
    free(tempPage);
    free(leafPage);
    free(newPage);

    path.pop_back();

    insertInt(ixfileHandle, parentPageNum, insertKey, leafHeader.nextPage, attr, path);

    free(insertKey);

    return;
}