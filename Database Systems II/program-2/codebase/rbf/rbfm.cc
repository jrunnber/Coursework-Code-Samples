#include "rbfm.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string.h>
#include <iomanip>

RecordBasedFileManager* RecordBasedFileManager::_rbf_manager = NULL;
PagedFileManager *RecordBasedFileManager::_pf_manager = NULL;

RecordBasedFileManager* RecordBasedFileManager::instance()
{
    if(!_rbf_manager)
        _rbf_manager = new RecordBasedFileManager();

    return _rbf_manager;
}

RecordBasedFileManager::RecordBasedFileManager()
{
}

RecordBasedFileManager::~RecordBasedFileManager()
{
}

RC RecordBasedFileManager::createFile(const string &fileName) {
    // Creating a new paged file.
    if (_pf_manager->createFile(fileName))
        return RBFM_CREATE_FAILED;

    // Setting up the first page.
    void * firstPageData = calloc(PAGE_SIZE, 1);
    if (firstPageData == NULL)
        return RBFM_MALLOC_FAILED;
    newRecordBasedPage(firstPageData);
    // Adds the first record based page.

    FileHandle handle;
    if (_pf_manager->openFile(fileName.c_str(), handle))
        return RBFM_OPEN_FAILED;
    if (handle.appendPage(firstPageData))
        return RBFM_APPEND_FAILED;
    _pf_manager->closeFile(handle);

    free(firstPageData);

    return SUCCESS;
}

RC RecordBasedFileManager::destroyFile(const string &fileName) {
	return _pf_manager->destroyFile(fileName);
}

RC RecordBasedFileManager::openFile(const string &fileName, FileHandle &fileHandle) {
    return _pf_manager->openFile(fileName.c_str(), fileHandle);
}

RC RecordBasedFileManager::closeFile(FileHandle &fileHandle) {
    return _pf_manager->closeFile(fileHandle);
}

RC RecordBasedFileManager::insertRecord(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const void *data, RID &rid) {
    // Gets the size of the record.
    unsigned recordSize = getRecordSize(recordDescriptor, data);

    // Cycles through pages looking for enough free space for the new entry.
    void *pageData = malloc(PAGE_SIZE);
    if (pageData == NULL)
        return RBFM_MALLOC_FAILED;
    bool pageFound = false;
    unsigned i;
    unsigned numPages = fileHandle.getNumberOfPages();
    for (i = 0; i < numPages; i++)
    {
        if (fileHandle.readPage(i, pageData))
            return RBFM_READ_FAILED;

        // When we find a page with enough space (accounting also for the size that will be added to the slot directory), we stop the loop.
        if (getPageFreeSpaceSize(pageData) >= sizeof(SlotDirectoryRecordEntry) + recordSize)
        {
            pageFound = true;
            break;
        }
    }

    // If we can't find a page with enough space, we create a new one
    if(!pageFound)
    {
        newRecordBasedPage(pageData);
    }

    SlotDirectoryHeader slotHeader = getSlotDirectoryHeader(pageData);

    // Setting up the return RID. //Modified for delete / update
    rid.pageNum = i;
    //rid.slotNum = slotHeader.recordEntriesNumber;
    SlotDirectoryRecordEntry emptySlot; 
    int j;
    for(j = 0; j <= slotHeader.recordEntriesNumber; j++){
        emptySlot = getSlotDirectoryRecordEntry(pageData, j);
        if( ((int) emptySlot.length) == 0 && emptySlot.offset == 0)
            break;
    }

    rid.slotNum = j;

    // Adding the new record reference in the slot directory.
    SlotDirectoryRecordEntry newRecordEntry;
    newRecordEntry.length = recordSize;
    newRecordEntry.offset = slotHeader.freeSpaceOffset - recordSize;
    setSlotDirectoryRecordEntry(pageData, rid.slotNum, newRecordEntry);

    // Updating the slot directory header.
    slotHeader.freeSpaceOffset = newRecordEntry.offset;
    slotHeader.recordEntriesNumber += 1;
    setSlotDirectoryHeader(pageData, slotHeader);

    // Adding the record data.
    setRecordAtOffset (pageData, newRecordEntry.offset, recordDescriptor, data);

    // Writing the page to disk.
    if (pageFound)
    {
        if (fileHandle.writePage(i, pageData))
            return RBFM_WRITE_FAILED;
    }
    else
    {
        if (fileHandle.appendPage(pageData))
            return RBFM_APPEND_FAILED;
    }

    free(pageData);
    return SUCCESS;
}

RC RecordBasedFileManager::readRecord(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const RID &rid, void *data) {
    // Retrieve the specified page
    void * pageData = malloc(PAGE_SIZE);
    if (fileHandle.readPage(rid.pageNum, pageData))
        return RBFM_READ_FAILED;

    // Checks if the specific slot id exists in the page
    SlotDirectoryHeader slotHeader = getSlotDirectoryHeader(pageData);
    
    if(slotHeader.recordEntriesNumber < rid.slotNum)
        return RBFM_SLOT_DN_EXIST;

    // Gets the slot directory record entry data
    SlotDirectoryRecordEntry recordEntry = getSlotDirectoryRecordEntry(pageData, rid.slotNum);

    if(recordEntry.offset == 0 && recordEntry.length == 0){
        return -1;
    }

    // Tombstone check
    if ((recordEntry.offset >= 0) && (recordEntry.length >= 0))
    {
        // Retrieve the actual entry data
        getRecordAtOffset(pageData, recordEntry.offset, recordDescriptor, data);
    }
    else
    {
        RID tomb;
        tomb.slotNum = recordEntry.length * (-1);
        tomb.pageNum = recordEntry.offset * (-1);
        readRecord(fileHandle, recordDescriptor, tomb, data);
    }

    free(pageData);
    return SUCCESS;
}

RC RecordBasedFileManager::printRecord(const vector<Attribute> &recordDescriptor, const void *data) {
    // Parse the null indicator and save it into an array
    int nullIndicatorSize = getNullIndicatorSize(recordDescriptor.size());
    char nullIndicator[nullIndicatorSize];
    memset(nullIndicator, 0, nullIndicatorSize);
    memcpy(nullIndicator, data, nullIndicatorSize);
    
    // We've read in the null indicator, so we can skip past it now
    unsigned offset = nullIndicatorSize;

    cout << "----" << endl;
    for (unsigned i = 0; i < (unsigned) recordDescriptor.size(); i++)
    {
        cout << setw(10) << left << recordDescriptor[i].name << ": ";
        // If the field is null, don't print it
        bool isNull = fieldIsNull(nullIndicator, i);
        if (isNull)
        {
            cout << "NULL" << endl;
            continue;
        }
        switch (recordDescriptor[i].type)
        {
            case TypeInt:
                uint32_t data_integer;
                memcpy(&data_integer, ((char*) data + offset), INT_SIZE);
                offset += INT_SIZE;

                cout << "" << data_integer << endl;
            break;
            case TypeReal:
                float data_real;
                memcpy(&data_real, ((char*) data + offset), REAL_SIZE);
                offset += REAL_SIZE;

                cout << "" << data_real << endl;
            break;
            case TypeVarChar:
                // First VARCHAR_LENGTH_SIZE bytes describe the varchar length
                uint32_t varcharSize;
                memcpy(&varcharSize, ((char*) data + offset), VARCHAR_LENGTH_SIZE);
                offset += VARCHAR_LENGTH_SIZE;

                // Gets the actual string.
                char *data_string = (char*) malloc(varcharSize + 1);
                if (data_string == NULL)
                    return RBFM_MALLOC_FAILED;
                memcpy(data_string, ((char*) data + offset), varcharSize);

                // Adds the string terminator.
                data_string[varcharSize] = '\0';
                offset += varcharSize;

                cout << data_string << endl;
                free(data_string);
            break;
        }
    }
    cout << "----" << endl;

    return SUCCESS;
}

RC RecordBasedFileManager::deleteRecord(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const RID &rid) {
    void * pageData = malloc(PAGE_SIZE);
    if (fileHandle.readPage(rid.pageNum, pageData))
        return RBFM_READ_FAILED;

    SlotDirectoryHeader slotHeader = getSlotDirectoryHeader(pageData);
    
    if(slotHeader.recordEntriesNumber < rid.slotNum)
        return RBFM_SLOT_DN_EXIST;

    // Gets the slot directory record entry data
    SlotDirectoryRecordEntry recordEntry = getSlotDirectoryRecordEntry(pageData, rid.slotNum);

    // It is what you wrote, but a a function
    removeRecord(pageData, recordEntry);

    //Set the slot of the removed record to length and offset 0
    recordEntry.length = 0;
    recordEntry.offset = 0;
    setSlotDirectoryRecordEntry(pageData, rid.slotNum, recordEntry);

    //Write the page back with all changes
    fileHandle.writePage(rid.pageNum, pageData);

    free(pageData);

    return SUCCESS;
}

RC RecordBasedFileManager::updateRecord(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const void *data, const RID &rid) {
    
    // Read the page.
    // Retrieve the specified page
    void * pageData = malloc(PAGE_SIZE);
    if (fileHandle.readPage(rid.pageNum, pageData))
        return RBFM_READ_FAILED;

    // Checks if the specific slot id exists in the page
    SlotDirectoryHeader slotHeader = getSlotDirectoryHeader(pageData);
    
    if(slotHeader.recordEntriesNumber < rid.slotNum)
        return RBFM_SLOT_DN_EXIST;

    // Gets the slot directory record entry data
    SlotDirectoryRecordEntry recordEntry = getSlotDirectoryRecordEntry(pageData, rid.slotNum);

    unsigned updatedRecordSize = getRecordSize(recordDescriptor, data);
    // We remove the old record and update the slot header.
    if(recordEntry.length == 0 && recordEntry.offset == 0){
        return UPDATE_RECORD_DNE;
    } else if (recordEntry.length > 0 && recordEntry.offset > 0){
    
        removeRecord(pageData, recordEntry);
        slotHeader = getSlotDirectoryHeader(pageData);

        // Check if there is enough space on the page.
        // Gets the size of the updated record.

        if (getPageFreeSpaceSize(pageData) >= updatedRecordSize)
        {
            // We can work on the same page.
            // Update our record entry
            recordEntry.length = updatedRecordSize;
            recordEntry.offset = slotHeader.freeSpaceOffset - updatedRecordSize;
            setSlotDirectoryRecordEntry(pageData, rid.slotNum, recordEntry);

            // Update the slot header.
            slotHeader.freeSpaceOffset = recordEntry.offset;
            setSlotDirectoryHeader(pageData, slotHeader);

            // Adding the new record data.
            setRecordAtOffset (pageData, recordEntry.offset, recordDescriptor, data);

            //Write the page back with all changes
            fileHandle.writePage(rid.pageNum, pageData);
        } 
        else
        {
            // We need to move the record to a different page.
            // We simply call insert record, then use the reurned RID to make a tombstone.
            RID newRID;
            insertRecord(fileHandle, recordDescriptor, data, newRID);

            // Making a tombstone.
            recordEntry.length = newRID.slotNum * (-1);
            recordEntry.offset = newRID.pageNum * (-1);
            setSlotDirectoryRecordEntry(pageData, rid.slotNum, recordEntry);
            fileHandle.writePage(rid.pageNum, pageData);
        }

    } else {
        RID tombRID;
        tombRID.slotNum = recordEntry.length*-1;
        tombRID.pageNum = recordEntry.offset*-1;
        void *tombData = malloc(PAGE_SIZE);
        if (fileHandle.readPage(tombRID.pageNum, tombData))
            return RBFM_READ_FAILED;

        // Checks if the specific slot id exists in the page
        SlotDirectoryHeader tombHeader = getSlotDirectoryHeader(tombData);
        
        if(tombHeader.recordEntriesNumber < tombRID.slotNum)
            return RBFM_SLOT_DN_EXIST;

        // Gets the slot directory record entry data
        SlotDirectoryRecordEntry tombEntry = getSlotDirectoryRecordEntry(tombData, tombRID.slotNum);

        if (getPageFreeSpaceSize(tombData) >= updatedRecordSize)
        {
            removeRecord(tombData, tombEntry);
            tombHeader = getSlotDirectoryHeader(tombData);
            // We can work on the same page.
            // Update our record entry
            tombEntry.length = updatedRecordSize;
            tombEntry.offset = tombHeader.freeSpaceOffset - updatedRecordSize;
            setSlotDirectoryRecordEntry(tombData, tombRID.slotNum, tombEntry);

            // Update the slot header.
            tombHeader.freeSpaceOffset = tombEntry.offset;
            setSlotDirectoryHeader(tombData, tombHeader);

            // Adding the new record data.
            setRecordAtOffset (tombData, tombEntry.offset, recordDescriptor, data);

            //Write the page back with all changes
            fileHandle.writePage(tombRID.pageNum, tombData);
        } 
        else
        {
            deleteRecord(fileHandle, recordDescriptor, tombRID);
            // We need to move the record to a different page.
            // We simply call insert record, then use the reurned RID to make a tombstone.
            RID newRID;
            insertRecord(fileHandle, recordDescriptor, data, newRID);

            // Making a tombstone.
            recordEntry.length = newRID.slotNum * (-1);
            recordEntry.offset = newRID.pageNum * (-1);
            setSlotDirectoryRecordEntry(pageData, rid.slotNum, recordEntry);

            fileHandle.writePage(rid.pageNum, pageData);
        }
        free(tombData);
    }

    free(pageData);

    return SUCCESS;
}

RC RecordBasedFileManager::scan(FileHandle &fileHandle,
      const vector<Attribute> &recordDescriptor,
      const string &conditionAttribute,
      const CompOp compOp,                  // comparision type such as "<" and "="
      const void *value,                    // used in the comparison
      const vector<string> &attributeNames, // a list of projected attributes
      RBFM_ScanIterator &rbfm_ScanIterator){

    RBFM_ScanIterator *newIt = new RBFM_ScanIterator(fileHandle, recordDescriptor, conditionAttribute, compOp, value, attributeNames);
    //RBFM_ScanIterator newScan(fileHandle, recordDescriptor, conditionAttribute, compOp, value, attributeNames);
    //rbfm_ScanIterator = newScan;
    memcpy(&rbfm_ScanIterator, newIt, sizeof(struct RBFM_ScanIterator));
    //delete(newIt);
    


    return SUCCESS;
}

RC RecordBasedFileManager::readAttribute(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const RID &rid, const string &attributeName, void *data){
    void * pageData = malloc(PAGE_SIZE);
    if (fileHandle.readPage(rid.pageNum, pageData))
        return RBFM_READ_FAILED;

    SlotDirectoryRecordEntry recordEntry = getSlotDirectoryRecordEntry(pageData, rid.slotNum);

    void * recordData = malloc(PAGE_SIZE);

    if(recordEntry.offset > 0 && recordEntry.length > 0){
        memcpy(recordData, ((char*) pageData) + recordEntry.offset,  recordEntry.length);
    } else {
        RID tomb;
        tomb.slotNum = recordEntry.length * (-1);
        tomb.pageNum = recordEntry.offset * (-1);
        readAttribute(fileHandle, recordDescriptor, tomb, attributeName, data);
        free(pageData);
        free(recordData);
        return SUCCESS;
    }

    RecordLength recordLength;
    memcpy(&recordLength, recordData, sizeof(RecordLength));

    int numNullBytes = getNullIndicatorSize(recordDescriptor.size());
    char nullBytes[numNullBytes];
    memcpy(nullBytes, ((char*) recordData) + sizeof(RecordLength), numNullBytes);


    unsigned i;
    unsigned nullCnt = 0;
    for(i = 0; i < recordDescriptor.size(); i++){
        if(recordDescriptor[i].name == attributeName){
            break;
        } else if(i == recordLength - 1){
            return ATTR_NOT_IN_DESC;
        }
        if(fieldIsNull(nullBytes, i)){
            nullCnt++;
        }
    }

    if(fieldIsNull(nullBytes, i)){
        char ret = 128;
        return ret;
        return SUCCESS;
    }

    uint16_t offsetStart, offsetEnd;
    if(i == 0){
        offsetStart = (recordLength)*sizeof(ColumnOffset) + numNullBytes + sizeof(RecordLength);
        memcpy(&offsetEnd, ((char*) recordData) + numNullBytes + sizeof(RecordLength), sizeof(ColumnOffset));
    } else {
        memcpy(&offsetStart, ((char*) recordData) + numNullBytes + sizeof(RecordLength) + (i - (1 + nullCnt))*sizeof(ColumnOffset), sizeof(ColumnOffset));
        memcpy(&offsetEnd,   ((char*) recordData) + numNullBytes + sizeof(RecordLength) + (i - nullCnt)*sizeof(ColumnOffset),     sizeof(ColumnOffset));
    }
    memset(data, 0, 1);
    memcpy(((char*) data) + 1, ((char*) recordData) + offsetStart, offsetEnd - offsetStart);



    free(pageData);
    free(recordData);

    return SUCCESS;

}

SlotDirectoryHeader RecordBasedFileManager::getSlotDirectoryHeader(void * page)
{
    // Getting the slot directory header.
    SlotDirectoryHeader slotHeader;
    memcpy (&slotHeader, page, sizeof(SlotDirectoryHeader));
    return slotHeader;
}

void RecordBasedFileManager::setSlotDirectoryHeader(void * page, SlotDirectoryHeader slotHeader)
{
    // Setting the slot directory header.
    memcpy (page, &slotHeader, sizeof(SlotDirectoryHeader));
}

SlotDirectoryRecordEntry RecordBasedFileManager::getSlotDirectoryRecordEntry(void * page, unsigned recordEntryNumber)
{
    // Getting the slot directory entry data.
    SlotDirectoryRecordEntry recordEntry;
    memcpy  (
            &recordEntry,
            ((char*) page + sizeof(SlotDirectoryHeader) + recordEntryNumber * sizeof(SlotDirectoryRecordEntry)),
            sizeof(SlotDirectoryRecordEntry)
            );

    return recordEntry;
}

void RecordBasedFileManager::setSlotDirectoryRecordEntry(void * page, unsigned recordEntryNumber, SlotDirectoryRecordEntry recordEntry)
{
    // Setting the slot directory entry data.
    memcpy  (
            ((char*) page + sizeof(SlotDirectoryHeader) + recordEntryNumber * sizeof(SlotDirectoryRecordEntry)),
            &recordEntry,
            sizeof(SlotDirectoryRecordEntry)
            );
}

// Configures a new record based page, and puts it in "page".
void RecordBasedFileManager::newRecordBasedPage(void * page)
{
    memset(page, 0, PAGE_SIZE);
    // Writes the slot directory header.
    SlotDirectoryHeader slotHeader;
    slotHeader.freeSpaceOffset = PAGE_SIZE;
    slotHeader.recordEntriesNumber = 0;
	memcpy (page, &slotHeader, sizeof(SlotDirectoryHeader));
}

unsigned RecordBasedFileManager::getRecordSize(const vector<Attribute> &recordDescriptor, const void *data) 
{
    // Read in the null indicator
    int nullIndicatorSize = getNullIndicatorSize(recordDescriptor.size());
    char nullIndicator[nullIndicatorSize];
    memset(nullIndicator, 0, nullIndicatorSize);
    memcpy(nullIndicator, (char*) data, nullIndicatorSize);

    // Offset into *data. Start just after the null indicator
    unsigned offset = nullIndicatorSize;
    // Running count of size. Initialize it to the size of the header
    unsigned size = sizeof (RecordLength) + (recordDescriptor.size()) * sizeof(ColumnOffset) + nullIndicatorSize;

    for (unsigned i = 0; i < (unsigned) recordDescriptor.size(); i++)
    {
        // Skip null fields
        if (fieldIsNull(nullIndicator, i))
            continue;
        switch (recordDescriptor[i].type)
        {
            case TypeInt:
                size += INT_SIZE;
                offset += INT_SIZE;
            break;
            case TypeReal:
                size += REAL_SIZE;
                offset += REAL_SIZE;
            break;
            case TypeVarChar:
                uint32_t varcharSize;
                // We have to get the size of the VarChar field by reading the integer that precedes the string value itself
                memcpy(&varcharSize, (char*) data + offset, VARCHAR_LENGTH_SIZE);
                size += varcharSize;
                offset += varcharSize + VARCHAR_LENGTH_SIZE;
            break;
        }
    }

    return size;
}

// Calculate actual bytes for null-indicator for the given field counts
int RecordBasedFileManager::getNullIndicatorSize(int fieldCount) 
{
    return int(ceil((double) fieldCount / CHAR_BIT));
}

bool RecordBasedFileManager::fieldIsNull(char *nullIndicator, int i)
{
    int indicatorIndex = i / CHAR_BIT;
    int indicatorMask  = 1 << (CHAR_BIT - 1 - (i % CHAR_BIT));
    return (nullIndicator[indicatorIndex] & indicatorMask) != 0;
}

// Computes the free space of a page (function of the free space pointer and the slot directory size).
unsigned RecordBasedFileManager::getPageFreeSpaceSize(void * page) 
{
    SlotDirectoryHeader slotHeader = getSlotDirectoryHeader(page);
    return slotHeader.freeSpaceOffset - slotHeader.recordEntriesNumber * sizeof(SlotDirectoryRecordEntry) - sizeof(SlotDirectoryHeader);
}

// Support header size and null indicator. If size is less than recordDescriptor size, then trailing records are null
void RecordBasedFileManager::getRecordAtOffset(void *page, unsigned offset, const vector<Attribute> &recordDescriptor, void *data)
{
    // Pointer to start of record
    char *start = (char*) page + offset;

    // Allocate space for null indicator.
    int nullIndicatorSize = getNullIndicatorSize(recordDescriptor.size());
    char nullIndicator[nullIndicatorSize];
    memset(nullIndicator, 0, nullIndicatorSize);

    // Get number of columns and size of the null indicator for this record
    RecordLength len = 0;
    memcpy (&len, start, sizeof(RecordLength));
    int recordNullIndicatorSize = getNullIndicatorSize(len);

    // Read in the existing null indicator
    memcpy (nullIndicator, start + sizeof(RecordLength), recordNullIndicatorSize);

    // If this new recordDescriptor has had fields added to it, we set all of the new fields to null
    for (unsigned i = len; i < recordDescriptor.size(); i++)
    {
        int indicatorIndex = (i+1) / CHAR_BIT;
        int indicatorMask  = 1 << (CHAR_BIT - 1 - (i % CHAR_BIT));
        nullIndicator[indicatorIndex] |= indicatorMask;
    }
    // Write out null indicator
    memcpy(data, nullIndicator, nullIndicatorSize);

    // Initialize some offsets
    // rec_offset: points to data in the record. We move this forward as we read data from our record
    unsigned rec_offset = sizeof(RecordLength) + recordNullIndicatorSize + len * sizeof(ColumnOffset);
    // data_offset: points to our current place in the output data. We move this forward as we write to data.
    unsigned data_offset = nullIndicatorSize;
    // directory_base: points to the start of our directory of indices
    char *directory_base = start + sizeof(RecordLength) + recordNullIndicatorSize;
    
    for (unsigned i = 0; i < recordDescriptor.size(); i++)
    {
        if (fieldIsNull(nullIndicator, i))
            continue;
        
        // Grab pointer to end of this column
        ColumnOffset endPointer;
        memcpy(&endPointer, directory_base + i * sizeof(ColumnOffset), sizeof(ColumnOffset));

        // rec_offset keeps track of start of column, so end-start = total size
        uint32_t fieldSize = endPointer - rec_offset;

        // Special case for varchar, we must give data the size of varchar first
        if (recordDescriptor[i].type == TypeVarChar)
        {
            memcpy((char*) data + data_offset, &fieldSize, VARCHAR_LENGTH_SIZE);
            data_offset += VARCHAR_LENGTH_SIZE;
        }
        // Next we copy bytes equal to the size of the field and increase our offsets
        memcpy((char*) data + data_offset, start + rec_offset, fieldSize);
        rec_offset += fieldSize;
        data_offset += fieldSize;
    }
}

void RecordBasedFileManager::setRecordAtOffset(void *page, unsigned offset, const vector<Attribute> &recordDescriptor, const void *data)
{
    // Read in the null indicator
    int nullIndicatorSize = getNullIndicatorSize(recordDescriptor.size());
    char nullIndicator[nullIndicatorSize];
    memset (nullIndicator, 0, nullIndicatorSize);
    memcpy(nullIndicator, (char*) data, nullIndicatorSize);

    // Points to start of record
    char *start = (char*) page + offset;

    // Offset into *data
    unsigned data_offset = nullIndicatorSize;
    // Offset into page header
    unsigned header_offset = 0;

    RecordLength len = recordDescriptor.size();
    memcpy(start + header_offset, &len, sizeof(len));
    header_offset += sizeof(len);

    memcpy(start + header_offset, nullIndicator, nullIndicatorSize);
    header_offset += nullIndicatorSize;

    // Keeps track of the offset of each record
    // Offset is relative to the start of the record and points to the END of a field
    ColumnOffset rec_offset = header_offset + (recordDescriptor.size()) * sizeof(ColumnOffset);

    unsigned i = 0;
    for (i = 0; i < recordDescriptor.size(); i++)
    {
        if (!fieldIsNull(nullIndicator, i))
        {
            // Points to current position in *data
            char *data_start = (char*) data + data_offset;

            // Read in the data for the next column, point rec_offset to end of newly inserted data
            switch (recordDescriptor[i].type)
            {
                case TypeInt:
                    memcpy (start + rec_offset, data_start, INT_SIZE);
                    rec_offset += INT_SIZE;
                    data_offset += INT_SIZE;
                break;
                case TypeReal:
                    memcpy (start + rec_offset, data_start, REAL_SIZE);
                    rec_offset += REAL_SIZE;
                    data_offset += REAL_SIZE;
                break;
                case TypeVarChar:
                    unsigned varcharSize;
                    // We have to get the size of the VarChar field by reading the integer that precedes the string value itself
                    memcpy(&varcharSize, data_start, VARCHAR_LENGTH_SIZE);
                    memcpy(start + rec_offset, data_start + VARCHAR_LENGTH_SIZE, varcharSize);
                    // We also have to account for the overhead given by that integer.
                    rec_offset += varcharSize;
                    data_offset += VARCHAR_LENGTH_SIZE + varcharSize;
                break;
            }
        }
        // Copy offset into record header
        // Offset is relative to the start of the record and points to END of field
        memcpy(start + header_offset, &rec_offset, sizeof(ColumnOffset));
        header_offset += sizeof(ColumnOffset);
    }
}

// I made this part a function because both delete and update want to use this, but then diverge, so we can't just call delete in update
void RecordBasedFileManager::removeRecord(void *page, SlotDirectoryRecordEntry recordEntry)
{
    SlotDirectoryHeader slotHeader = getSlotDirectoryHeader(page);

    // Offset to end of Slot Directory
    unsigned endDirOffset = sizeof(SlotDirectoryHeader) + slotHeader.recordEntriesNumber * sizeof(SlotDirectoryRecordEntry);

    // All the data in the file after the slot directory but before the record being deleted
    unsigned slideSize = recordEntry.offset - endDirOffset;
    void * slideData = malloc(slideSize);

    // Grab the aformentioned data
    memcpy(slideData, (char*) page + endDirOffset, slideSize);

    // Put the data back, offset, to overwrite the deleted record data
    memcpy((char*) page + endDirOffset + recordEntry.length, slideData, slideSize);

    for(int i = 0; i < slotHeader.recordEntriesNumber; i++)
    {
        // Iterate over every slot in the directory
        SlotDirectoryRecordEntry recordEntryTemp = getSlotDirectoryRecordEntry(page, i);
        // If the record was moved fix the offset
        if((recordEntryTemp.offset >= 0) && (recordEntryTemp.offset < recordEntry.offset))
        {
            recordEntryTemp.offset += recordEntry.length;
            setSlotDirectoryRecordEntry(page, i, recordEntryTemp);
        }
    }

    // Updating the slot directory header.
    slotHeader.freeSpaceOffset = slotHeader.freeSpaceOffset + recordEntry.length;
    setSlotDirectoryHeader(page, slotHeader);

    free(slideData);
}

RBFM_ScanIterator::RBFM_ScanIterator(FileHandle &fileHandleCons, const vector<Attribute> &recordDescriptorCons, const string &conditionAttributeCons, const CompOp compOpCons, const void *valueCons, const vector<string> &attributeNamesCons) {
    lastRID.slotNum = -1;
    lastRID.pageNum = 0;
    pageData = malloc(PAGE_SIZE);
    (fileHandleCons).readPage(0, pageData);
    memcpy(&lastSlotHead, pageData, sizeof(struct SlotDirectoryHeader));
    fileHandle = fileHandleCons;
    recordDescriptor = recordDescriptorCons;
    conditionAttribute = conditionAttributeCons;
    compOp = compOpCons;
    if(valueCons == NULL){
        value = NULL;
    } else {
        value = malloc(PAGE_SIZE);                  
        memcpy(value, valueCons, 100); 
    }                   
    attributeNames = attributeNamesCons; 
}

RBFM_ScanIterator::~RBFM_ScanIterator()
{
    free(pageData);
    free(value);
}

bool RBFM_ScanIterator::fieldIssNull(char *nullIndicator, int i)
{
    int indicatorIndex = i / CHAR_BIT;
    int indicatorMask  = 1 << (CHAR_BIT - 1 - (i % CHAR_BIT));
    return (nullIndicator[indicatorIndex] & indicatorMask) != 0;
}

RC RBFM_ScanIterator::getNextRecord(RID &rid, void *data) { 
int i;
check_page_change:

    //index from 0 dont forget add -1s
    //Check to see if we need to go to the next page
    if(lastRID.slotNum == (unsigned) (lastSlotHead.recordEntriesNumber - 1)){
        //reset slotNum to -1 / increment pageNum
        lastRID.slotNum = -1;
        lastRID.pageNum++;
        if(lastRID.pageNum == fileHandle.getNumberOfPages()){
            return RBFM_EOF;
        }
        //Grab the new page and slotHead
        (fileHandle).readPage(lastRID.pageNum, pageData);
        memcpy(&lastSlotHead, pageData, sizeof(SlotDirectoryHeader));
    }

    //Get the next record entry
    i = 1;
    SlotDirectoryRecordEntry recordEntry;
    memcpy  (
            &recordEntry,
            ((char*) pageData + (sizeof(struct SlotDirectoryHeader) + (lastRID.slotNum + i) * sizeof(struct SlotDirectoryRecordEntry))),
            sizeof(struct SlotDirectoryRecordEntry)
            );
    //SlotDirectoryRecordEntry recordEntry = getSlotDirectoryRecordEntry(pageData, lastRID.slotNum + i); ^^^^^^^^^^^^^^
    //Continue grabing slotEntries is the given are tombstones
    while(recordEntry.offset <= 0 && recordEntry.length <= 0){
        i++;

        memcpy  (
            &recordEntry,
            ((char*) pageData + sizeof(SlotDirectoryHeader) + (lastRID.slotNum + i) * sizeof(SlotDirectoryRecordEntry)),
            sizeof(SlotDirectoryRecordEntry)
            );

        //recordEntry = getSlotDirectoryRecordEntry(pageData, lastRID.slotNum + i); ^^^^^^^^^^^^^^^^^^^^^^^
        if(lastRID.slotNum + i == lastSlotHead.recordEntriesNumber){
            lastRID.slotNum += i;
            goto check_page_change;
        }
    }
    //Set the slotNum to the one selected
    lastRID.slotNum += i;

    //Grab the record specified by the slot
    void* recordData = malloc(PAGE_SIZE);
    memcpy(recordData, ((char*) pageData) + recordEntry.offset, recordEntry.length);

    //Grab the recordLength
    RecordLength recordLength;
    memcpy(&recordLength, recordData, sizeof(RecordLength));

    //Grab the nullBuytes of the record
    int numNullBytes;
    numNullBytes = int(ceil((double) (recordLength) / CHAR_BIT));

    char nullBytes[numNullBytes];
    //char nullBytes[numNullBytes];
    memcpy(nullBytes, ((char*) recordData) + sizeof(RecordLength), numNullBytes);

    //Variables for various loops
    unsigned dataOffset = 0;
    string attributeName;

    int k;
    unsigned nullCnt = 0;
    void* attributeTBC = calloc(PAGE_SIZE, 1);

    attributeName = conditionAttribute;

    //If statement to void comparison when NO_OP is specified
    if(compOp < NO_OP){
        //Grab attribute to compare
        for(k = 0; k < recordLength; k++){
            if((recordDescriptor)[k].name == attributeName){
                break;
            } else if(k == recordLength - 1){
                return ATTR_NOT_IN_DESC;
            }
            if(fieldIssNull(nullBytes, k)){
                nullCnt++;
            }
        }

        uint16_t offsetStart, offsetEnd;
        if(fieldIssNull(nullBytes, k)){
                //Attribute is null
        } else {
            if(k == 0){
                offsetStart = recordLength*sizeof(ColumnOffset) + numNullBytes + sizeof(RecordLength);
                memcpy(&offsetEnd, ((char*) recordData) + numNullBytes + sizeof(RecordLength), sizeof(ColumnOffset));
            } else {
                memcpy(&offsetStart, ((char*) recordData) + numNullBytes + sizeof(RecordLength) + (k - 1 - nullCnt)*sizeof(ColumnOffset), sizeof(ColumnOffset));
                memcpy(&offsetEnd,   ((char*) recordData) + numNullBytes + sizeof(RecordLength) + (k - nullCnt)*sizeof(ColumnOffset),     sizeof(ColumnOffset));
            }            
                memcpy(attributeTBC, ((char*) recordData) + offsetStart, offsetEnd - offsetStart);
        }

        if(recordDescriptor[k].type == 0){
            int attrToBeCompared = 0;
            int valueToCompareTo = 0;
            memcpy(&attrToBeCompared, attributeTBC, sizeof(int));
            memcpy(&valueToCompareTo, value, sizeof(int));
            switch(compOp){
                case 0:
                  //if not equal skip
                  if(attrToBeCompared != valueToCompareTo){
                    goto check_page_change;
                  }
                  break;
                case 1:
                   if(attrToBeCompared > valueToCompareTo){
                    goto check_page_change;
                   }
                  break; 
                case 2:
                   if(attrToBeCompared >= valueToCompareTo){
                    goto check_page_change;
                   }
                    break;
                case 3:
                  if(attrToBeCompared < valueToCompareTo){
                    goto check_page_change;
                  }
                    break;
                case 4:
                  if(attrToBeCompared <= valueToCompareTo){
                    goto check_page_change;
                  }
                    break;
                case 5:
                  if(attrToBeCompared == valueToCompareTo){
                    goto check_page_change;
                  }
                    break;
                case 6:
                    //No-op
                    break;
                default: 
                    return OP_NOT_FOUND;
                    break;
                }
        } else if(recordDescriptor[k].type == 1){
            float attrToBeCompared, valueToCompareTo;
            memcpy(&attrToBeCompared, attributeTBC, sizeof(float));
            memcpy(&valueToCompareTo, value, sizeof(float));
            switch(compOp){
                case 0:
                  //if not equal skip
                  if(attrToBeCompared != valueToCompareTo){
                    goto check_page_change;
                  }
                  break;
                case 1:
                   if(attrToBeCompared > valueToCompareTo){
                    goto check_page_change;
                   }
                  break; 
                case 2:
                   if(attrToBeCompared >= valueToCompareTo){
                    goto check_page_change;
                   }
                    break;
                case 3:
                  if(attrToBeCompared < valueToCompareTo){
                    goto check_page_change;
                  }
                    break;
                case 4:
                  if(attrToBeCompared <= valueToCompareTo){
                    goto check_page_change;
                  }
                    break;
                case 5:
                  if(attrToBeCompared == valueToCompareTo){
                    goto check_page_change;
                  }
                    break;
                case 6:
                    //No-op
                    break;
                default: 
                    return OP_NOT_FOUND;
                    break;
                }
        } else {
            switch(compOp){
            case 0:
                //if not equal skip
                if(strcmp((char*) attributeTBC, (char*) value) != 0){
                    goto check_page_change;
                }
                break;
            case 1:
                if(strcmp((char*) attributeTBC, (char*) value) >= 0){
                    goto check_page_change;
                }
              break; 
            case 2:
                if(strcmp((char*) attributeTBC, (char*) value) > 0){
                goto check_page_change;
               }
                break;
            case 3:
                if(strcmp((char*) attributeTBC, (char*) value) <= 0){
                    goto check_page_change;
                }
                break;
            case 4:
                if(strcmp((char*) attributeTBC, (char*) value) < 0){
                    goto check_page_change;
                }
                break;
            case 5:
                if(strcmp((char*) attributeTBC, (char*) value) == 0){
                    goto check_page_change;
                }
                break;
            case 6:
                //No-op
                break;
            default: 
                return OP_NOT_FOUND;
                break;
            }
        }      
    } // End if not NO_OP

    int numOutNullBytes = int(ceil((double) (recordLength) / CHAR_BIT));
    char outNullBytes[numOutNullBytes];
    memset(outNullBytes, 0, numOutNullBytes);

    uint16_t offsetFirst, offsetEnd;
    void *tempData = malloc(PAGE_SIZE);
    //Grab attributes to return
    for(unsigned j = 0; j < (attributeNames).size(); j++){
        nullCnt = 0;
        attributeName = (attributeNames)[j];
        for(k = 0; k < recordLength; k++){
            if((recordDescriptor)[k].name == attributeName){
                break;
            } else if(k == recordLength - 1){
                return ATTR_NOT_IN_DESC;
            }
            if(fieldIssNull(nullBytes, k)){
                nullCnt++;
            }
        }
 
        if(fieldIssNull(nullBytes, k)){
            //attr is null
            char shiftme = 1;
            shiftme = shiftme << (7 - k%8);
            outNullBytes[k/8] |= shiftme;
        } else {
            uint16_t offsetStart;

            if(k == 0){
                offsetFirst = offsetStart = recordLength*sizeof(ColumnOffset) + numNullBytes + sizeof(RecordLength);
                memcpy(&offsetEnd, ((char*) recordData) + numNullBytes + sizeof(RecordLength), sizeof(ColumnOffset));
            } else {
                memcpy(&offsetStart, ((char*) recordData) + numNullBytes + sizeof(RecordLength) + (k - 1 - nullCnt)*sizeof(ColumnOffset), sizeof(ColumnOffset));
                memcpy(&offsetEnd,   ((char*) recordData) + numNullBytes + sizeof(RecordLength) + (k - nullCnt)*sizeof(ColumnOffset),     sizeof(ColumnOffset));
            }

            if((recordDescriptor)[k].type == 2){
                int *len = (int*) calloc(sizeof(int), 1);
                *len = offsetEnd - offsetStart;
                memcpy(((char*) tempData) + dataOffset, len, sizeof(int));
                dataOffset += sizeof(int);
                free(len);
            }
            memcpy(((char*) tempData) + dataOffset, ((char*) recordData) + offsetStart, offsetEnd - offsetStart);
            dataOffset += (offsetEnd - offsetStart);
        }
    }
  
    memcpy(data, outNullBytes, numOutNullBytes);
    memcpy(((char*) data) + numOutNullBytes, tempData, dataOffset);
   

    free(attributeTBC);
    free(recordData);
    free(tempData);
    
    return SUCCESS;
}
