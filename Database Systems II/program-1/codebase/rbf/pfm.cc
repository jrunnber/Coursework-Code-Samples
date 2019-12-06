#include "pfm.h"

PagedFileManager* PagedFileManager::_pf_manager = 0;

PagedFileManager* PagedFileManager::instance()
{
    if(!_pf_manager)
        _pf_manager = new PagedFileManager();

    return _pf_manager;
}


PagedFileManager::PagedFileManager()
{
}


PagedFileManager::~PagedFileManager()
{
}


RC PagedFileManager::createFile(const string &fileName)
{

    const char* fName = fileName.c_str();

    FILE* file;
    if( (file = fopen(fName, "rb")) ){
        fclose(file);
        return -1;
    }

    file = fopen(fName, "wb");
    fclose(file);
    return 0;
}


RC PagedFileManager::destroyFile(const string &fileName)
{
    const char* fName = fileName.c_str();

    FILE* file;
    if( (file = fopen(fName, "rb")) ){
        fclose(file);
        remove(fName);
        return 0;
    }

    return -1;

}


RC PagedFileManager::openFile(const string &fileName, FileHandle &fileHandle)
{

    const char* fName = fileName.c_str();

    FILE* file;
    if( (file = fopen(fName, "r+b")) && (fileHandle.fp == NULL) ){
      fileHandle.fp = file;
      return 0;
    }

    return -1;
}


RC PagedFileManager::closeFile(FileHandle &fileHandle)
{

    if(fclose(fileHandle.fp) == 0){
        return 0;
    }

    return -1;
}


FileHandle::FileHandle()
{
    fp = NULL;
	readPageCounter = 0;
	writePageCounter = 0;
	appendPageCounter = 0;
}


FileHandle::~FileHandle()
{
    fp = NULL;
}

RC FileHandle::readPage(PageNum pageNum, void *data)
{


    if( (fp == NULL) || (pageNum >= getNumberOfPages()) || pageNum < 0)
        return -1;

    fseek(fp, PAGE_SIZE*pageNum, SEEK_SET);
    fread(data, sizeof(char), PAGE_SIZE, fp);
    readPageCounter++;

    return 0;
}


RC FileHandle::writePage(PageNum pageNum, const void *data)
{
    if( (fp == NULL) || (pageNum > getNumberOfPages()) )
        return -1;

    if(pageNum == getNumberOfPages()){
        return appendPage(data);
    }

    void* buf = calloc(sizeof(char), PAGE_SIZE);
    memcpy(buf, data, PAGE_SIZE);

    fseek(fp, PAGE_SIZE*pageNum, SEEK_SET);
    if(fwrite(buf, sizeof(char), PAGE_SIZE, fp) == PAGE_SIZE){
        writePageCounter++;
        free(buf);
        return 0;
    }

    free(buf);
    return -1;
}


RC FileHandle::appendPage(const void *data)
{
    if(fp == NULL)
        return -1;

    void* buf = calloc(sizeof(char), PAGE_SIZE);
    memcpy(buf, data, PAGE_SIZE);

    fseek(fp, 0, SEEK_END);
    if(fwrite(buf, sizeof(char), PAGE_SIZE, fp) == PAGE_SIZE){
        appendPageCounter++;
        fflush(fp);
        free(buf);
        return 0;
    }
    free(buf);
    return -1;
}



unsigned FileHandle::getNumberOfPages()
{
    if(fp == NULL)
        return 0;
    
    struct stat buf; 
    int fd = fileno(fp);
    fstat(fd, &buf);

    return buf.st_size/PAGE_SIZE;

}


RC FileHandle::collectCounterValues(unsigned &readPageCount, unsigned &writePageCount, unsigned &appendPageCount)
{
    readPageCount = readPageCounter;
    writePageCount = writePageCounter;
    appendPageCount = appendPageCounter;
	return 0;
}
