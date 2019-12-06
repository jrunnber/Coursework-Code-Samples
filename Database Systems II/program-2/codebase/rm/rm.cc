
#include "rm.h"
#include <string.h>

//Changed rbf and pf manager to RelationMAnager::
RelationManager* RelationManager::_rm = 0;
RecordBasedFileManager *RelationManager::_rbf_manager = NULL;
PagedFileManager *RelationManager::_pf_manager = NULL;
FileHandle TablesHandle;
FileHandle ColumnsHandle;
int table_id = 1;

RelationManager* RelationManager::instance()
{
    if(!_rm)
        _rm = new RelationManager();

    return _rm;
}

RelationManager::RelationManager()
{
}

RelationManager::~RelationManager()
{
}

RC RelationManager::createCatalog()
{
    vector<Attribute> tablesDescriptor;
    vector<Attribute> columnsDescriptor;
    catalogDescripters(tablesDescriptor, columnsDescriptor);
    // Create the files.
    string TablesFile = "TablesFile";
    string ColumnsFile = "ColumnsFile";
    if (_rbf_manager->createFile(TablesFile))
        return RBFM_CREATE_FAILED;

    if (_rbf_manager->createFile(ColumnsFile))
        return RBFM_CREATE_FAILED;


    // Open the files.
    _rbf_manager->openFile(TablesFile, TablesHandle);
    _rbf_manager->openFile(ColumnsFile, ColumnsHandle);


    // Make and insert the intial records.
    RID rid;
    void *record = calloc(100, 1);
    prepareTableRecord(1, 6, "Tables", 10, TablesFile, record);
    _rbf_manager->insertRecord(TablesHandle, tablesDescriptor, record, rid);

    memset(record, 0, 100);
    prepareTableRecord(2, 7, "Columns", 11, ColumnsFile, record);
    _rbf_manager->insertRecord(TablesHandle, tablesDescriptor, record, rid);


    for (int i = 0; i < tablesDescriptor.size(); i++)
    {
        memset(record, 0, 100);
        prepareColumnRecord(table_id, tablesDescriptor[i].name.size(), tablesDescriptor[i].name, tablesDescriptor[i].type, tablesDescriptor[i].length, i+1, record);
        _rbf_manager->insertRecord(ColumnsHandle, columnsDescriptor, record, rid);
    }
    table_id++;

    for (int i = 0; i < columnsDescriptor.size(); i++)
    {
        memset(record, 0, 100);
        prepareColumnRecord(table_id, columnsDescriptor[i].name.size(), columnsDescriptor[i].name, columnsDescriptor[i].type, columnsDescriptor[i].length, i+1, record);
        _rbf_manager->insertRecord(ColumnsHandle, columnsDescriptor, record, rid);
    }
    table_id++;

    // free
    free(record);

    _rbf_manager->closeFile(TablesHandle);
    _rbf_manager->closeFile(ColumnsHandle);

    return 0;
}

RC RelationManager::deleteCatalog()
{
    string TablesFile = "TablesFile";
    string ColumnsFile = "ColumnsFile";

    _rbf_manager->destroyFile(TablesFile);
    _rbf_manager->destroyFile(ColumnsFile);

    return 0;
}

RC RelationManager::createTable(const string &tableName, const vector<Attribute> &attrs)
{
    _rbf_manager->openFile("TablesFile", TablesHandle);
    _rbf_manager->openFile("ColumnsFile", ColumnsHandle);

    vector<Attribute> tablesDescriptor;
    vector<Attribute> columnsDescriptor;
    catalogDescripters(tablesDescriptor, columnsDescriptor);

    if (_rbf_manager->createFile(tableName))
        return RBFM_CREATE_FAILED;

    RID rid;
    void *record = calloc(100, 1);

    prepareTableRecord(table_id, tableName.size(), tableName, tableName.size(), tableName, record);
    _rbf_manager->insertRecord(TablesHandle, tablesDescriptor, record, rid);

    for (int i = 0; i < attrs.size(); i++)
    {
        memset(record, 0, 100);
        prepareColumnRecord(table_id, attrs[i].name.size(), attrs[i].name, attrs[i].type, attrs[i].length, i+1, record);
        _rbf_manager->insertRecord(ColumnsHandle, columnsDescriptor, record, rid);
    }
    table_id++;

    _rbf_manager->closeFile(TablesHandle);
    _rbf_manager->closeFile(ColumnsHandle);
    free(record);
    return 0;
}

RC RelationManager::deleteTable(const string &tableName)
{
    _rbf_manager->openFile("TablesFile", TablesHandle);
    _rbf_manager->openFile("ColumnsFile", ColumnsHandle);

    vector<Attribute> tablesDescriptor;
    vector<Attribute> columnsDescriptor;
    catalogDescripters(tablesDescriptor, columnsDescriptor);
    // Can't delete catalog files.
    if ((strcmp(tableName.c_str(), "Tables") == 0) || (strcmp(tableName.c_str(), "Columns") == 0))
        return -1;

    // Destroy the file.
    _rbf_manager->destroyFile(tableName);

    // Intialize the values for the Tables scan
    vector<string> tablesInfo;
    tablesInfo.push_back("table-id");
    RBFM_ScanIterator* tablesIter = (RBFM_ScanIterator*) malloc(sizeof(struct RBFM_ScanIterator));

    // Scan for tableName to get the table id
    _rbf_manager->scan(TablesHandle, tablesDescriptor, "table-name", EQ_OP, (void*) &tableName, tablesInfo, *tablesIter);

    // Grab that value or fail if not found
    RID rid;
    void *returnedData = malloc(100);
    uint32_t table_id;
    if (tablesIter->getNextRecord(rid, returnedData) != RM_EOF)
    {
        memcpy(&table_id, ((char*) returnedData + 1), sizeof(uint32_t));
        _rbf_manager->deleteRecord(TablesHandle, tablesDescriptor, rid);
    }

    // Scan for the table id to delete them.
    RBFM_ScanIterator* columnsIter = (RBFM_ScanIterator*) malloc(sizeof(struct RBFM_ScanIterator));
    //RBFM_ScanIterator columnsIter;
    _rbf_manager->scan(ColumnsHandle, columnsDescriptor, "table-id", EQ_OP, (void*) &table_id, tablesInfo, *columnsIter);
    
    memset(returnedData, 0, 100);
    while (columnsIter->getNextRecord(rid, returnedData) != RM_EOF)
    {
        _rbf_manager->deleteRecord(ColumnsHandle, columnsDescriptor, rid);

        // Reset the data.
        memset(returnedData, 0, 100);
    }   

    _rbf_manager->closeFile(TablesHandle);
    _rbf_manager->closeFile(ColumnsHandle);
    free(tablesIter);
    free(columnsIter);
    free(returnedData);
    return 0;
}

RC RelationManager::getAttributes(const string &tableName, vector<Attribute> &attrs)
{
    vector<Attribute> tablesDescriptor;
    vector<Attribute> columnsDescriptor;
    catalogDescripters(tablesDescriptor, columnsDescriptor);

    _rbf_manager->openFile("TablesFile", TablesHandle);
    _rbf_manager->openFile("ColumnsFile", ColumnsHandle);

    // All we want from Tables is the id of tableName
    vector<string> tablesInfo;
    tablesInfo.push_back("table-id");

    // Scan for tableName to get the table id
    RBFM_ScanIterator* tablesIter = (RBFM_ScanIterator*) malloc(sizeof(struct RBFM_ScanIterator));

    int newdoot = 1;
    
    _rbf_manager->scan(TablesHandle, tablesDescriptor, "table-name", EQ_OP, tableName.c_str(), tablesInfo, *tablesIter);

    // Grab that value or fail if not found
    RID rid;
    void *returnedData = malloc(PAGE_SIZE);
    uint32_t table_id;
    if (tablesIter->getNextRecord(rid, returnedData) != RM_EOF)
        memcpy(&table_id, ((char*) returnedData + 1), sizeof(uint32_t));
    else
        return -1;
    tablesIter->close();

    // We need the name, type, and length to make an Attribute.
    vector<string> columnsInfo;
    columnsInfo.push_back("column-name");
    columnsInfo.push_back("column-type");
    columnsInfo.push_back("column-length");

    // Scan for the table id to get the attributes of the table.
    RBFM_ScanIterator* columnsIter = (RBFM_ScanIterator*) malloc(sizeof(struct RBFM_ScanIterator));
    _rbf_manager->scan(ColumnsHandle, columnsDescriptor, "table-id", EQ_OP, (void*) &table_id, columnsInfo, *columnsIter);
    
    memset(returnedData, 0, 100);
    while (columnsIter->getNextRecord(rid, returnedData) != RM_EOF)
    {
        // Account for the null indicator.
        int offset = 1;

        //Edit data -> returnedData
        // Get the length of the name.
        uint32_t nameLength;
        memcpy(&nameLength, ((char*) returnedData + offset), sizeof(uint32_t));
        offset += sizeof(uint32_t);

        // Get the name.
        char column_name[nameLength + 1];
        column_name[nameLength] = '\0';
        memcpy(column_name, ((char*) returnedData + offset), nameLength);
        offset += nameLength;

        // Get the type.
        AttrType column_type;
        memcpy(&column_type, ((char*) returnedData + offset), sizeof(AttrType));
        offset += sizeof(AttrType);

        // Get the length.
        AttrLength column_length;
        memcpy(&column_length, ((char*) returnedData + offset), sizeof(AttrLength));
        offset += sizeof(AttrLength);

        // Make the Attribute and push it to the return value.
        Attribute attr;
        attr.name = column_name;
        attr.type = column_type;
        attr.length = column_length;
        attrs.push_back(attr);

        // Reset the data.
        memset(returnedData, 0, 100);
    }

    _rbf_manager->closeFile(TablesHandle);
    _rbf_manager->closeFile(ColumnsHandle);
    free(tablesIter);
    free(columnsIter);
    free(returnedData);
    return 0;
}

RC RelationManager::insertTuple(const string &tableName, const void *data, RID &rid)
{
    
     _rbf_manager->openFile("TablesFile", TablesHandle);
    _rbf_manager->openFile("ColumnsFile", ColumnsHandle);

    vector<Attribute> tablesDescriptor;
    vector<Attribute> columnsDescriptor;
    catalogDescripters(tablesDescriptor, columnsDescriptor);

    // Can't insert into catalog files.
    if ((strcmp(tableName.c_str(), "Tables") == 0) || (strcmp(tableName.c_str(), "Columns") == 0))
        return -1;

    // Get the record desriptor.
    vector<Attribute> recordDescriptor;
    getAttributes(tableName, recordDescriptor);

    // Open the file.
    FileHandle handle;
    if(_rbf_manager->openFile(tableName, handle))
            return -1;

    // Insert the record.
    RC rc = _rbf_manager->insertRecord(handle, recordDescriptor, data, rid);

    // Close the file.
    _rbf_manager->closeFile(handle);

    return rc;
}

RC RelationManager::deleteTuple(const string &tableName, const RID &rid)
{
     _rbf_manager->openFile("TablesFile", TablesHandle);
    _rbf_manager->openFile("ColumnsFile", ColumnsHandle);

    vector<Attribute> tablesDescriptor;
    vector<Attribute> columnsDescriptor;
    catalogDescripters(tablesDescriptor, columnsDescriptor);

    // Can't delete from catalog files.
    if ((strcmp(tableName.c_str(), "Tables") == 0) || (strcmp(tableName.c_str(), "Columns") == 0))
        return -1;

    // Get the record desriptor.
    vector<Attribute> recordDescriptor;
    getAttributes(tableName, recordDescriptor);

    // Open the file.
    FileHandle handle;
    if(_rbf_manager->openFile(tableName, handle))
            return -1;

    // Delete the record.
    RC rc = _rbf_manager->deleteRecord(handle, recordDescriptor, rid);

    // Close the file.
    _rbf_manager->closeFile(handle);

    _rbf_manager->closeFile(TablesHandle);
    _rbf_manager->closeFile(ColumnsHandle);

    return rc;
}

RC RelationManager::updateTuple(const string &tableName, const void *data, const RID &rid)
{
     _rbf_manager->openFile("TablesFile", TablesHandle);
    _rbf_manager->openFile("ColumnsFile", ColumnsHandle);

    vector<Attribute> tablesDescriptor;
    vector<Attribute> columnsDescriptor;
    catalogDescripters(tablesDescriptor, columnsDescriptor);

    // Can't update catalog files.
    if ((strcmp(tableName.c_str(), "Tables") == 0) || (strcmp(tableName.c_str(), "Columns") == 0))
        return -1;

    // Get the record desriptor.
    vector<Attribute> recordDescriptor;
    getAttributes(tableName, recordDescriptor);

    // Open the file.
    FileHandle handle;
    if(_rbf_manager->openFile(tableName, handle))
            return -1;

    // Update the record.
    RC rc = _rbf_manager->updateRecord(handle, recordDescriptor, data, rid);

    // Close the file.
    _rbf_manager->closeFile(handle);

    _rbf_manager->closeFile(TablesHandle);
    _rbf_manager->closeFile(ColumnsHandle);

    return rc;
}

RC RelationManager::readTuple(const string &tableName, const RID &rid, void *data)
{
     _rbf_manager->openFile("TablesFile", TablesHandle);
    _rbf_manager->openFile("ColumnsFile", ColumnsHandle);

    vector<Attribute> tablesDescriptor;
    vector<Attribute> columnsDescriptor;
    catalogDescripters(tablesDescriptor, columnsDescriptor);

    RC rc;
    // Already have catalog files open.
    if (strcmp(tableName.c_str(), "Tables") == 0)
    {
        rc = _rbf_manager->readRecord(TablesHandle, tablesDescriptor, rid, data);
        _rbf_manager->closeFile(TablesHandle);
        _rbf_manager->closeFile(ColumnsHandle);
        return rc;
    }
    else if (strcmp(tableName.c_str(), "Columns") == 0)
    {
        rc = _rbf_manager->readRecord(ColumnsHandle, columnsDescriptor, rid, data);
        _rbf_manager->closeFile(TablesHandle);
        _rbf_manager->closeFile(ColumnsHandle);
        return rc;
    }
    else
    {
        // Get the record desriptor.
        vector<Attribute> recordDescriptor;
        getAttributes(tableName, recordDescriptor);

        // Open the file.
        FileHandle handle;
        if(_rbf_manager->openFile(tableName, handle))
            return -1;

        // Read the record.
        rc = _rbf_manager->readRecord(handle, recordDescriptor, rid, data);

        // Close the file.
        _rbf_manager->closeFile(handle);

        _rbf_manager->closeFile(TablesHandle);
    _rbf_manager->closeFile(ColumnsHandle);

        return rc;
    }
}

RC RelationManager::printTuple(const vector<Attribute> &attrs, const void *data)
{
    // Just call rbf print
    RC rc = _rbf_manager->printRecord(attrs, data);
    return rc;
}

RC RelationManager::readAttribute(const string &tableName, const RID &rid, const string &attributeName, void *data)
{
    _rbf_manager->openFile("TablesFile", TablesHandle);
    _rbf_manager->openFile("ColumnsFile", ColumnsHandle);

    vector<Attribute> tablesDescriptor;
    vector<Attribute> columnsDescriptor;
    catalogDescripters(tablesDescriptor, columnsDescriptor);

    RC rc;
    // Already have catalog files open.
    if (strcmp(tableName.c_str(), "Tables") == 0)
    {
        rc = _rbf_manager->readRecord(TablesHandle, tablesDescriptor, rid, data);
        _rbf_manager->closeFile(TablesHandle);
        _rbf_manager->closeFile(ColumnsHandle);
        return rc;
    }
    else if (strcmp(tableName.c_str(), "Columns") == 0)
    {
        rc = _rbf_manager->readRecord(ColumnsHandle, columnsDescriptor, rid, data);
        _rbf_manager->closeFile(TablesHandle);
        _rbf_manager->closeFile(ColumnsHandle);
        return rc;
    }
    else
    {
        // Get the record desriptor.
        vector<Attribute> recordDescriptor;
        getAttributes(tableName, recordDescriptor);

        // Open the file.
        FileHandle handle;
        if(_rbf_manager->openFile(tableName, handle))
            return -1;

        //  read Attr the record.
        rc = _rbf_manager->readAttribute(handle, recordDescriptor, rid, attributeName, data);

        // Close the file.
        _rbf_manager->closeFile(handle);

        _rbf_manager->closeFile(TablesHandle);
        _rbf_manager->closeFile(ColumnsHandle);

        return rc;
    }
}

RC RelationManager::scan(const string &tableName,
    const string &conditionAttribute,
    const CompOp compOp,                  
    const void *value,                    
    const vector<string> &attributeNames,
    RM_ScanIterator &rm_ScanIterator)
{
    FileHandle fileHandle;
    if(_rbf_manager->openFile(tableName, fileHandle))
        return -1;

    vector<Attribute> recordDescriptor;
    getAttributes(tableName, recordDescriptor);

    rm_ScanIterator.scanner = new RBFM_ScanIterator(fileHandle, recordDescriptor, conditionAttribute, compOp, value, attributeNames);

    
    return SUCCESS;
}


void RelationManager::prepareTableRecord(const int id, const int tableNameLength, const string &tableName, const int fileNameLength, const string &fileName, void *buffer)
{

    // Offset starts at 1, since the null indicator is always one byte with no nulls.
    int offset = 1;

    // Beginning of the actual data
    // id
    memcpy((char *)buffer + offset, &id, sizeof(int));
    offset += sizeof(int);

    // tableName
    memcpy((char *)buffer + offset, &tableNameLength, sizeof(int));
    offset += sizeof(int);
    memcpy((char *)buffer + offset, tableName.c_str(), tableNameLength);
    offset += tableNameLength;

    // fileName
    memcpy((char *)buffer + offset, &fileNameLength, sizeof(int));
    offset += sizeof(int);
    memcpy((char *)buffer + offset, fileName.c_str(), fileNameLength);
    offset += fileNameLength;
}

void RelationManager::prepareColumnRecord(const int id, const int nameLength, const string &name, AttrType type, AttrLength length, const int position, void *buffer)
{
    // Offset starts at 1, since the null indicator is always one byte with no nulls.
    int offset = 1;

    // Beginning of the actual data
    // id
    memcpy((char *)buffer + offset, &id, sizeof(int));
    offset += sizeof(int);

    // name
    memcpy((char *)buffer + offset, &nameLength, sizeof(int));
    offset += sizeof(int);
    memcpy((char *)buffer + offset, name.c_str(), nameLength);
    offset += nameLength;

    // type
    memcpy((char *)buffer + offset, &type, sizeof(int));
    offset += sizeof(int);

    // length
    memcpy((char *)buffer + offset, &length, sizeof(int));
    offset += sizeof(int);

    // position
    memcpy((char *)buffer + offset, &position, sizeof(int));
    offset += sizeof(int);
}

void RelationManager::catalogDescripters(vector<Attribute> &tablesDescriptor, vector<Attribute> &columnsDescriptor){
    Attribute attr;
    attr.name = "table-id";
    attr.type = TypeInt;
    attr.length = (AttrLength)4;
    tablesDescriptor.push_back(attr);

    attr.name = "table-name";
    attr.type = TypeVarChar;
    attr.length = (AttrLength)50;
    tablesDescriptor.push_back(attr);

    attr.name = "file-name";
    attr.type = TypeVarChar;
    attr.length = (AttrLength)50;
    tablesDescriptor.push_back(attr);

    attr.name = "table-id";
    attr.type = TypeInt;
    attr.length = (AttrLength)4;
    columnsDescriptor.push_back(attr);

    attr.name = "column-name";
    attr.type = TypeVarChar;
    attr.length = (AttrLength)50;
    columnsDescriptor.push_back(attr);

    attr.name = "column-type";
    attr.type = TypeInt;
    attr.length = (AttrLength)4;
    columnsDescriptor.push_back(attr);

    attr.name = "column-length";
    attr.type = TypeInt;
    attr.length = (AttrLength)4;
    columnsDescriptor.push_back(attr);

    attr.name = "column-position";
    attr.type = TypeInt;
    attr.length = (AttrLength)4;
    columnsDescriptor.push_back(attr);


}

