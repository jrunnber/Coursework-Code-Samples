#include "rbfm.h"
#include "iostream"

using namespace std;

RecordBasedFileManager* RecordBasedFileManager::_rbf_manager = 0;

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
    RC rc = pfm->createFile(fileName);
    return rc;
}

RC RecordBasedFileManager::destroyFile(const string &fileName) {
    RC rc = pfm->destroyFile(fileName);
    return rc;
}

RC RecordBasedFileManager::openFile(const string &fileName, FileHandle &fileHandle) {
    RC rc = pfm->openFile(fileName, fileHandle);
    return rc;
}

RC RecordBasedFileManager::closeFile(FileHandle &fileHandle) {
    RC rc = pfm->closeFile(fileHandle);
    return rc;
}

RC RecordBasedFileManager::insertRecord(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const void *data, RID &rid) {
    

    int attrLength = recordDescriptor.size();
    int *lOffset = (int*) calloc(attrLength + 1, sizeof(int));
    int nullBytes = ceil((float)attrLength/8);
    int dataOffset = nullBytes;
    int necDataLength = nullBytes;
    int numNull = 0;
    void *newData = calloc(PAGE_SIZE, 1);

    int8_t nulls[nullBytes];
    memcpy(nulls, (int8_t*) data, nullBytes);

    memcpy(newData, data, nullBytes);

     for(int i = 0; i < nullBytes; i++){
        for(int j = 0; j < 8 && (i*8 + j) < attrLength; j++){
            
            if(nulls[i] < 0){
                numNull++;
                lOffset[i*8 + j] = -1;
            } else {
                if(recordDescriptor[i*8 + j].type == 0){
                    //int
                    
                    memcpy(((char*) newData) + necDataLength, ((char*) data) + dataOffset, sizeof(int));
                    necDataLength += recordDescriptor[i*8 + j].length;
                    lOffset[i*8 + j] = necDataLength;                
                    dataOffset    += recordDescriptor[i*8 + j].length;
                
                } else if(recordDescriptor[i*8 + j].type == 1){
                    //real
                
                    memcpy(((char*) newData) + necDataLength, ((char*) data) + dataOffset, sizeof(float));
                    necDataLength += recordDescriptor[i*8 + j].length;
                    lOffset[i*8 + j] = necDataLength;
                    dataOffset    += recordDescriptor[i*8 + j].length;
                   
                } else { 
                    //varchar
                    int* length = (int*) calloc(1, sizeof(int));
                    memcpy(length, ((char*) data) + dataOffset, sizeof(int));
                    
                    memcpy(((char*) newData) + necDataLength, ((char*) data) + (dataOffset + sizeof(int)), *length);
                    necDataLength += *length;
                    lOffset[i*8 + j] = necDataLength;
                    dataOffset    += (*length + sizeof(int));
                   
                    free(length);
                }
            }
            nulls[i] = nulls[i] << 1;
        }
    }

    int numOffsets = recordDescriptor.size() - numNull;

    int sizeToStore =  numOffsets*4 + necDataLength;

    int numPages = fileHandle.getNumberOfPages();    
    void *readData = calloc(PAGE_SIZE, 1);
    fileHandle.readPage(numPages - 1, readData);
    

    int offToFree = ((int*) readData)[(PAGE_SIZE/sizeof(int)) - 1];
    int numSlots  = ((int*) readData)[(PAGE_SIZE/sizeof(int)) - 2];

    int amountFreeSpace = PAGE_SIZE - (offToFree + (2*(numSlots + 1) + 2 + sizeToStore));

    int *writeOffset = (int*) calloc(numOffsets, sizeof(int));
    int writeOffsetIndex = 0;

    //Offsets will start from beginning of record, nullbytes included
    for(int i = 0; i < attrLength; i++){
        if(lOffset[i] != -1){
            lOffset[i] += (numOffsets*sizeof(int));
            memcpy(((char*)writeOffset) + writeOffsetIndex*sizeof(int), ((char*) lOffset) + i*sizeof(int), sizeof(int));
            writeOffsetIndex++;
        } 
    }
    if(amountFreeSpace < 0 || numPages == 0){
        void *appData = calloc(PAGE_SIZE, 1);
        memcpy(appData, (char*) data, nullBytes);
        memcpy( ((char*) appData) + nullBytes, writeOffset, sizeof(int)*numOffsets);
        memcpy( ((char*) appData) + nullBytes + sizeof(int)*numOffsets, ((char*) newData) + nullBytes, necDataLength - nullBytes);
        int appendDirectoryInfo[4] = {1, 0, 1, sizeToStore + 1};
        memcpy( ((char*) appData) + (PAGE_SIZE - 4*sizeof(int)), appendDirectoryInfo, 4*sizeof(int));
        rid.pageNum = numPages;
        rid.slotNum = 1;
        fileHandle.appendPage(appData);
        free(appData);
    } else {
        memcpy( ((char*) readData) + offToFree, (char*) data, nullBytes);
        memcpy( ((char*) readData) + nullBytes + offToFree, writeOffset, sizeof(int)*numOffsets);
        memcpy( ((char*) readData) + nullBytes + sizeof(int)*numOffsets + offToFree, ((char*) newData) + nullBytes, necDataLength - nullBytes); //added nullbytes 2nd arg
        int addDirectoryInfo[2]    = {numSlots + 1, offToFree};
        int updateDirectoryInfo[2] = {numSlots + 1, offToFree + sizeToStore};
        memcpy( ((char*) readData) + (PAGE_SIZE - (numSlots*2 + 4)*sizeof(int)), addDirectoryInfo, sizeof(int)*2);
        memcpy( ((char*) readData) + (PAGE_SIZE - 2*sizeof(int)), updateDirectoryInfo, 2*sizeof(int));
        fileHandle.writePage(numPages - 1, readData);
        rid.pageNum = numPages - 1;
        rid. slotNum = numSlots + 1;
    }

    free(readData);
    free(lOffset);
    free(writeOffset);
    free(newData);

    return 0;
}

RC RecordBasedFileManager::readRecord(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const RID &rid, void *data) {
    void *readData = calloc(1, PAGE_SIZE);
    fileHandle.readPage(rid.pageNum, readData);

    int* numSlots = (int*)  calloc(1, sizeof(int));
    memcpy(numSlots, ((char*) readData) + (PAGE_SIZE - 2*sizeof(int)), sizeof(int));

    int* slots = (int*) calloc(*numSlots*2, sizeof(int));
    memcpy(slots, ((char*) readData) + (PAGE_SIZE - (*numSlots + 1)*2*sizeof(int)), *numSlots*2*sizeof(int));

    int recordStartOffset = -1; //start of record in file
    for(int i = 0; i < *numSlots; i++){
        if(slots[i*2] == rid.slotNum){
            recordStartOffset = slots[i*2 + 1];
        }    
    }

    if(recordStartOffset == -1){
        printf("incorrect record offset!\n");
        exit(1);
    }

    int attrLength = recordDescriptor.size();
    int nullBytes = ceil((float)attrLength/8);
    int8_t nulls[nullBytes];
    memcpy(nulls, ((int8_t*) readData) + recordStartOffset, nullBytes);

    void *outData = calloc(1, PAGE_SIZE);
    memcpy(outData, nulls, nullBytes);
    int outputOffset = nullBytes;


    int numOfOffsets = 0;
    for(int i = 0; i < nullBytes; i++){
        for(int j = 0; j < 8 && (i*8 + j) < attrLength; j++){
            if(nulls[i] >= 0){
                numOfOffsets++;
            }
            nulls[i] = nulls[i] << 1;
        }
    }

    int *arrayOfOffsets = (int*) calloc(numOfOffsets, sizeof(int));
    memcpy(arrayOfOffsets, ((char*) readData) + recordStartOffset + nullBytes, numOfOffsets*sizeof(int));
   

    int recordDataSize = arrayOfOffsets[numOfOffsets - 1] - (nullBytes +numOfOffsets*sizeof(int));
    void *actualRecordData = (char*) calloc(recordDataSize, 1);
    memcpy(actualRecordData, ((char* ) readData) + recordStartOffset + nullBytes + numOfOffsets*sizeof(int), recordDataSize);

    memcpy(nulls, ((int8_t*) readData) + recordStartOffset, nullBytes);

    int offsetsCounter = 0;
    int actualRecordDataOffset = 0;
    for(int i = 0; i < nullBytes; i++){
        for(int j = 0; j < 8 && (i*8 + j) < attrLength; j++){
            if(nulls[i] >= 0){
                if(recordDescriptor[i*8 + j].type == 2){ 
                    if(offsetsCounter == 0){
                        int length = arrayOfOffsets[0] - (nullBytes + numOfOffsets*sizeof(int));
                        memcpy(((char*) outData) + outputOffset, &length, sizeof(int));
                        outputOffset += sizeof(int);
                        memcpy(((char*) outData) + outputOffset, ((char*) actualRecordData), length);
                        outputOffset += length;
                        actualRecordDataOffset += length;
                    } else {
                        int length = arrayOfOffsets[offsetsCounter] - arrayOfOffsets[offsetsCounter - 1];
                        memcpy(((char*) outData) + outputOffset, &length, sizeof(int));
                        outputOffset += sizeof(int);
                        memcpy(((char*) outData) + outputOffset, ((char*) actualRecordData) + actualRecordDataOffset, length);
                        outputOffset += length;
                        actualRecordDataOffset += length;
                   } 
                } else if(recordDescriptor[i*8 + j].type == 1){
                    //real
                    float* temp = (float*) calloc(1, sizeof(float));
                    memcpy(temp, ((char*) actualRecordData) + actualRecordDataOffset, sizeof(float));
                    memcpy(((char*) outData) + outputOffset, temp, sizeof(float));
                    outputOffset += sizeof(float);
                    actualRecordDataOffset += sizeof(float);
                    free(temp);
                } else {
                    //int
                    int* temp = (int*) calloc(1, sizeof(int));
                    memcpy(temp, ((char*) actualRecordData) + actualRecordDataOffset, sizeof(int));
                    memcpy(((char*) outData) + outputOffset, temp, sizeof(int));
                    outputOffset += sizeof(int);
                    actualRecordDataOffset += sizeof(int);
                    free(temp);
                }
                offsetsCounter++; 
            } 
            nulls[i] = nulls[i] << 1;
        }
    }

  
    memcpy(data, outData, outputOffset);


    free(readData);
    free(numSlots);
    free(slots);
    free(outData);
    free(arrayOfOffsets);
    free(actualRecordData);
 
    return 0;
        
}

RC RecordBasedFileManager::printRecord(const vector<Attribute> &recordDescriptor, const void *data) {
    int attrLength = recordDescriptor.size();
    int nullBytes = ceil((float)attrLength/8);
    int dataOffset = nullBytes;
    int necDataLength = nullBytes;

    int8_t nulls[nullBytes];
    memcpy(nulls, (int8_t*) data, nullBytes);

     for(int i = 0; i < nullBytes; i++){
        for(int j = 0; j < 8 && (i*8 + j) < attrLength; j++){
            cout << recordDescriptor[i*8 + j].name;
            if(nulls[i] < 0){
                printf(": NULL\n");
                //null
            } else {
                if(recordDescriptor[i*8 + j].type == 0){
                    //int
                    int* temp = (int*) calloc(1, sizeof(int));
                    memcpy(temp, ((char*) data) + dataOffset, sizeof(int));
                    printf(": %d\n", *temp);
                    necDataLength += recordDescriptor[i*8 + j].length;
                    dataOffset    += recordDescriptor[i*8 + j].length;
                    free(temp);
                } else if(recordDescriptor[i*8 + j].type == 1){
                    //real
                    float* temp = (float*) calloc(1, sizeof(float));
                    memcpy(temp, ((char*) data) + dataOffset, sizeof(float));
                    printf(": %f\n", *temp);
                    necDataLength += recordDescriptor[i*8 + j].length;
                    dataOffset    += recordDescriptor[i*8 + j].length;
                    free(temp);
                } else { 
                    //varchar
                    int* length = (int*) calloc(1, sizeof(int));
                    memcpy(length, ((char*) data) + dataOffset, sizeof(int));
                    //Plus 1 to null terminate in the case that it doesn't in the data
                    char* vChar = (char*) calloc(*length + 1, sizeof(char));
                    memcpy(vChar, ((char*) data) + (dataOffset + sizeof(int)), *length);
                    printf(": %s\n", vChar);
                    necDataLength += *length;
                    dataOffset    += (*length + sizeof(int));
                    free(vChar);
                    free(length);
                }
            }
            nulls[i] = nulls[i] << 1;
        }
    }

    return 0;
}
