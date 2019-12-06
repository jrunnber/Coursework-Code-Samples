/*
 FUSE: Filesystem in Userspace
 Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
 
 This program can be distributed under the terms of the GNU GPL.
 See the file COPYING.
 
 gcc -Wall hello.c `pkg-config fuse --cflags --libs` -o hello
 */

#define FUSE_USE_VERSION 26
#define BLOCK_SIZE  4096 //4KB
#define BLOCK_TOTAL (BLOCK_SIZE<<3)
#define FS_SIZE BLOCK_TOTAL*BLOCK_SIZE
#define NAME_LEN 256
#define BYTE_SIZE 8
#define BYTE_MASK 0b11111111
#define BIT_MASK  0b10000000
#define DATA_SIZE      3720
#define FILENAME_SIZE  256
#define STAT_SIZE 120
#define META_DATA_SIZE 376
#define MAGIC_SIZE 4
#define BITMAP_SIZE (BLOCK_SIZE - MAGIC_SIZE)
#define START  0
#define EXTEND 1


//Block offsets
#define STAT_OFFSET 0
#define FILENAME_OFFSET 120
#define DATA_OFFSET 376


#include "fuse.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <utime.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/uio.h>
#include <sys/queue.h>

static int FS_FILE;
char* aofs_path;

void ClearPathBuffer(void);
uint32_t AllocateBlock(void);
uint32_t GetBlockLocation(uint16_t byte_offset, uint16_t bit_offset);
uint32_t LocateFile(const char * path);

//Finds the location of a block in a file system
//Takes in a byte and bit offset
uint32_t GetBlockLocation(uint16_t byte_offset, uint16_t bit_offset){
    return (byte_offset*BYTE_SIZE+bit_offset)*BLOCK_SIZE;
}

uint32_t LocateFile(const char * path){
    uint8_t bitmap[BITMAP_SIZE];
    uint16_t byte_offset;
    uint8_t bit_offset;
    uint32_t location;
    struct stat fstat;
    char filename[FILENAME_SIZE];
    
    //Get first block from the FS_FILE
    //First block should be the bitmap
    pread(FS_FILE,bitmap,BITMAP_SIZE,MAGIC_SIZE);
    
    printf("LocateFile: %s\n", path);
    //Get attributes for added files
    //Check for used blocks 1 byte at a time
    for(int i = 0; i < BITMAP_SIZE; i++){
        if((bitmap[i] & BYTE_MASK)){
            //skip if just super block
            if(i == 0 && bitmap[i] == BIT_MASK)
                continue;
            //Used slot found search blocks
            byte_offset = i;
            for(int j = 0; j < BYTE_SIZE; j++){
                bit_offset = j;
                if((bitmap[byte_offset] & (BIT_MASK>>bit_offset))){
                    location = GetBlockLocation(byte_offset, bit_offset);
                    //Check for superblock
                    if(location == 0 && byte_offset == 0)
                        continue;
                    pread(FS_FILE, &fstat, STAT_SIZE, location + STAT_OFFSET);
                    printf("LocateFile byte_offset: %d bit_offset: %d location %d flag %d\n",byte_offset, bit_offset, location, fstat.st_flags);
                    //Check to see if its a START or EXTEND
                    if(fstat.st_flags == START){
                        pread(FS_FILE,filename,FILENAME_SIZE, location + FILENAME_OFFSET);
                        //Check if file is found
                        if(!strcmp(path, filename)){
                            printf("LocateFile: File located %s byte_offset: %d bit_offset %d location %d\n", filename, byte_offset, bit_offset, location);
                            return location;
                        }
                    }
                }
            }
        }
    }
    return 0;
}

//Finds the next free block by checking the bitmap
//Returns the location of a free block in bytes offset from zero
//Updates bitmap to reflect usage.
uint32_t AllocateBlock(){
    uint8_t bitmap[BITMAP_SIZE];
    
    uint16_t byte_offset;
    uint8_t bit_offset;
    uint32_t location;
    
    //Get first block from the file
    //First block should be the bitmap
    pread(FS_FILE,bitmap,BITMAP_SIZE,MAGIC_SIZE);
    
    //Check for free block one byte at time
    for(int i = 0; i < BITMAP_SIZE; i++){
        if((bitmap[i] & BYTE_MASK) != BYTE_MASK){
            //Free slot found search for bit
            byte_offset = i;
            for(int j = 0; j < BYTE_SIZE; j++){
                bit_offset = j;
                if((bitmap[byte_offset] & (BIT_MASK>>bit_offset)) == 0){
                    break;
                }
            }
            break;
        }
    }
    location = GetBlockLocation(byte_offset, bit_offset);
    printf("Block Allocated: byte_offset: %d, bit_offset: %d, location: %d\n", byte_offset, bit_offset, location);
    
    if(location > 0){
        uint8_t block_used_mask;
        block_used_mask = bitmap[byte_offset];
        block_used_mask |= (BIT_MASK>>bit_offset);
        //Set location to bitmap
        pwrite(FS_FILE, &block_used_mask, 1, (byte_offset)+MAGIC_SIZE);
    }
    
    return location;
}

static int aofs_getattr(const char *path, struct stat *stbuf)
{
    int res = 0;
    
    uint8_t bitmap[BITMAP_SIZE];
    uint32_t location;
    struct stat fstat;
    
    //Get first block from the FS_FILE
    //First block should be the bitmap
    pread(FS_FILE,bitmap,BITMAP_SIZE,MAGIC_SIZE);
    
    memset(stbuf, 0, sizeof(struct stat));
    
    
    location = LocateFile(path);
    printf("getattr: %s Location: %d\n",path, location);
    if(location > 0){
        pread(FS_FILE, &fstat, STAT_SIZE, location + STAT_OFFSET);
        *stbuf = fstat;
        //Setting to zero just incase its being used
        stbuf->st_lspare = 0;
        stbuf->st_flags = 0;
        return res;
    }
    
    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
    }
    else
        res = -ENOENT;
    
    return res;
}

//Locates files before looking for attributes
static int aofs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
    (void) offset;
    (void) fi;
    
    uint8_t bitmap[BITMAP_SIZE];
    uint16_t byte_offset;
    uint8_t bit_offset;
    uint32_t location;
    char filename[FILENAME_SIZE];
    struct stat fstat;
    
    //only read from root dir
    if (strcmp(path, "/") != 0)
        return -ENOENT;
    
    //If file is found add to ls
    pread(FS_FILE,bitmap,BITMAP_SIZE,MAGIC_SIZE);
    
    
    //Get attributes for added files
    //Check for used blocks 1 byte at a time
    for(int i = 0; i < BITMAP_SIZE; i++){
        if(bitmap[i] & BYTE_MASK){
            //Used slot found search blocks
            byte_offset = i;
            for(int j = 0; j < BYTE_SIZE; j++){
                bit_offset = j;
                if((bitmap[byte_offset] & (BIT_MASK>>bit_offset))){
                    location = GetBlockLocation(byte_offset, bit_offset);
                    //Check for superblock
                    if(byte_offset == 0 && location == 0)
                        continue;
                    pread(FS_FILE, &fstat, STAT_SIZE, location + STAT_OFFSET);
                    //Check to see if its a START or EXTEND
                    if(fstat.st_flags == START){
                        pread(FS_FILE, filename, FILENAME_SIZE, location + FILENAME_OFFSET);
                        printf("readdir: File Located: %s\n",filename);
                        filler(buf, filename + 1, NULL, 0);
                    }
                }
            }
        }
    }
    
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    
    return 0;
}

static int aofs_open(const char *path, struct fuse_file_info *fi)
{
    uint32_t location;
    
    location = LocateFile(path);
    
    if(location > 0){
        return 0;
    }
    
    if (strcmp(path, aofs_path) != 0)
        return -ENOENT;
    
    if ((fi->flags & 3) != O_RDONLY)
        return -EACCES;
    
    return 0;
}

static int aofs_read(const char *path, char *buf, size_t size, off_t offset,
                     struct fuse_file_info *fi)
{
    uint32_t location;
    struct stat fstat;
    struct timespec current_time;
    uint64_t block_cnt;
    uint64_t block_start;
    uint64_t byte_start;
    uint64_t bytes_read = 0;
    uint64_t bytes_left;
    uint64_t bytes_needed;
    
    (void) fi;

    location = LocateFile(path);
    
    if(!location)
        return -ENOENT;
    
    //Compute starting point
    block_start = (int) (offset/DATA_SIZE) + 1;
    bytes_left = (block_start*DATA_SIZE)-(block_start-1)*BLOCK_SIZE;
    byte_start = DATA_SIZE - bytes_left;
    bytes_needed = size;
    block_cnt = 1;
    
    //Go to starting point
    while(block_cnt != block_start){
        if(!location)
            return -EFAULT;
        //load stat
        pread(FS_FILE, &fstat, STAT_SIZE, location + STAT_OFFSET);
        location = fstat.st_lspare;
        block_cnt++;
    }

    while(bytes_read < size){
        bytes_left = DATA_SIZE - byte_start;
        //Load data
        if(bytes_needed > bytes_left){
            pread(FS_FILE, buf+bytes_read, bytes_left, location +  DATA_OFFSET + byte_start);
            bytes_needed -= bytes_left;
            bytes_read += bytes_left;
            bytes_left = 0;
        }
        else{
            pread(FS_FILE, buf+bytes_read, bytes_needed, location +  DATA_OFFSET + byte_start);
            bytes_left -= bytes_needed;
            bytes_read += bytes_needed;
            bytes_needed = 0;
        }
        
        //byte will always start at
        //the beginning of data block
        //if loop continues
        byte_start = 0;
        
        //Update stat
        pread(FS_FILE, &fstat, STAT_SIZE, location + STAT_OFFSET);
        fstat.st_atimespec = current_time;
        pwrite(FS_FILE, &fstat, STAT_SIZE, location + STAT_OFFSET);
        
        //Load next pointer
        location = fstat.st_lspare;
    }

    return (int) bytes_read;
}


static int aofs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    uint32_t        location;
    struct stat     fstat;                   //current block stats
    struct timespec current_time;
    uint16_t        blocks_needed;
    uint16_t        byte_offset;
    uint16_t        bytes_left;
    printf("write: bytes_needed %ld\n", size);
    (void) fi;
    
    //Find file
    location = LocateFile(path);
    
    //No file located
    if(!location){
        return -ENOENT;
    }
    clock_gettime(CLOCK_REALTIME, &current_time);
    
    //Go to starting point
    uint16_t block_cnt = 1;
    uint16_t block_start = (int) (offset/DATA_SIZE) + 1;
    
    while(block_cnt < block_start){
        pread(FS_FILE, &fstat, STAT_SIZE, location + STAT_OFFSET);
        location = fstat.st_lspare;
        //Update Stats
        if(block_cnt > 1)
            fstat.st_flags = EXTEND;
        else
            fstat.st_flags = START;
        fstat.st_atimespec = current_time;
        fstat.st_size = offset + size;
        fstat.st_blocks = (int)(fstat.st_size / DATA_SIZE) + 1;
        pwrite(FS_FILE, &fstat, STAT_SIZE, location + STAT_OFFSET);
        
        block_cnt++;
    }
    

    byte_offset = offset % DATA_SIZE;
    bytes_left = DATA_SIZE - byte_offset;
    
    
    //read stat and write first block
    pread(FS_FILE, &fstat, STAT_SIZE, location + STAT_OFFSET);
    pwrite(FS_FILE, buf, bytes_left, location + DATA_OFFSET + byte_offset);
    
    //Compute number of blocks needed
    blocks_needed = (int)(size/DATA_SIZE) + 1;
    
    //update stats
    fstat.st_mtimespec = current_time;
    fstat.st_blocks = blocks_needed;
    fstat.st_size = size;
    pwrite(FS_FILE, &fstat, STAT_SIZE, location + STAT_OFFSET);
    
    
    //Chain required pages
    for(int i = 1; i < blocks_needed; i++){
        if(!fstat.st_lspare){
            fstat.st_lspare = AllocateBlock();
            printf("write: New Block Space %d\n", fstat.st_lspare);
            if(!fstat.st_lspare){
                printf("write: Cannot allocate memory\n");
                return -ENOMEM;
            }
        }
        
        //update previous block stat
        pwrite(FS_FILE, &fstat, STAT_SIZE, location + STAT_OFFSET);
        
        //update current location write next block
        location = fstat.st_lspare;
        pwrite(FS_FILE, bytes_left + buf+(i*DATA_SIZE), DATA_SIZE, location + DATA_OFFSET);
        
        
        
        //update fstat for next loop
        fstat.st_lspare = 0;
        fstat.st_flags = EXTEND;
    }
    
    //update stats for last block
    if(blocks_needed > 1)
        fstat.st_flags = EXTEND;
    fstat.st_lspare = 0;
    pwrite(FS_FILE, &fstat, STAT_SIZE, location + STAT_OFFSET);
    
    return (int) size;
    
//    //legacy
//    uint32_t        location;
//    struct stat     fstat;                   //current block stats
//    struct timespec current_time;
//    uint16_t        blocks_needed;
//
//    printf("write: bytes_needed %ld\n", size);
//    (void) fi;
//
//    //Find file
//    location = LocateFile(path);
//
//    //No file located
//    if(!location){
//        return -ENOENT;
//    }
//
//    //read stat and write first block
//    pread(FS_FILE, &fstat, STAT_SIZE, location + STAT_OFFSET);
//    pwrite(FS_FILE, buf, DATA_SIZE, location + DATA_OFFSET);
//
//    //Compute number of blocks needed
//    blocks_needed = (int)(size/DATA_SIZE) + 1;
//
//    //update stats
//    clock_gettime(CLOCK_REALTIME, &current_time);
//    fstat.st_mtimespec = current_time;
//    fstat.st_blocks = blocks_needed;
//    fstat.st_size = size;
//    pwrite(FS_FILE, &fstat, STAT_SIZE, location + STAT_OFFSET);
//
//
//    //Chain required pages
//    for(int i = 1; i < blocks_needed; i++){
//        if(!fstat.st_lspare){
//            fstat.st_lspare = AllocateBlock();
//            printf("write: New Block Space %d\n", fstat.st_lspare);
//            if(!fstat.st_lspare){
//                printf("write: Cannot allocate memory\n");
//                return -ENOMEM;
//            }
//        }
//
//        //update previous block stat
//        pwrite(FS_FILE, &fstat, STAT_SIZE, location + STAT_OFFSET);
//
//        //update current location write next block
//        location = fstat.st_lspare;
//        pwrite(FS_FILE, buf+(i*DATA_SIZE), DATA_SIZE, location + DATA_OFFSET);
//
//
//
//        //update fstat for next loop
//        fstat.st_lspare = 0;
//        fstat.st_flags = EXTEND;
//    }
//
//    //update stats for last block
//    if(blocks_needed > 1)
//        fstat.st_flags = EXTEND;
//    fstat.st_lspare = 0;
//    pwrite(FS_FILE, &fstat, STAT_SIZE, location + STAT_OFFSET);
//
//    return (int) size;

}

//Creates and initializes file
//Sets file name and stat structure.
static int aofs_create(const char *path, mode_t mode, struct fuse_file_info *fi){
    uint32_t location;
    size_t name_length;
    struct stat fstat;
    ssize_t res = 0;
    struct timespec current_time;
    
    location = AllocateBlock();
    
    if(!location){
        printf("Unable to allocate block\n");
        return -ENOMEM;
    }
    
    clock_gettime(CLOCK_REALTIME, &current_time);   //Get current time
    fstat.st_blocks = 1;                            //Sets blockcount to 1
    fstat.st_birthtimespec = current_time;          //Sets Time of creation
    fstat.st_atimespec = current_time;              //Time of last access
    fstat.st_mtimespec = current_time;              //Time of last modification
    fstat.st_blksize = BLOCK_SIZE;                  //Block size
    fstat.st_flags = START;                         //Indicates start of file
    fstat.st_size = 0;                              //File size intialized to zero bytes
    fstat.st_mode = S_IFREG | 0666;                 //Sets as regular file with 0666
    fstat.st_nlink = 1;                             //Hardlinks
    fstat.st_lspare = 0;                            //pointer to next block as an offset
    fstat.st_gid = 0;                               //Group ID "wheel"
    fstat.st_uid = 1001;                            //User ID
    
    res = pwrite(FS_FILE, &fstat, STAT_SIZE, location + STAT_OFFSET);
    
    if(res < 0){
        printf("Unable to write metadata");
        return -errno;
    }
    
    name_length = strlen(path);
    if(strlen(path) >  FILENAME_MAX){
        printf("\"%s\" Filename Too Long\n",path+1);
        return -ENAMETOOLONG;
    }
    
    res = pwrite(FS_FILE, path, FILENAME_SIZE, location + FILENAME_OFFSET);
    
    if(res < 0){
        printf("Error writing to file");
        return -errno;
    }
    printf("create: %s\n",path);
    return 0;
}

int aofs_fgetattr (const char *path, struct stat *stbuf, struct fuse_file_info * fi){
    int res = 0;
    uint32_t location;
    struct stat fstat;
    
    memset(stbuf, 0, sizeof(struct stat));
    
    //Get attributes for added files
    location = LocateFile(path);
    printf("fgetattr: %s Location: %d\n",path, location);
    if(location > 0){
        pread(FS_FILE, &fstat, STAT_SIZE, location + STAT_OFFSET);
        *stbuf = fstat;
        //Setting to zero just incase its being used
        stbuf->st_lspare = 0;
        stbuf->st_flags = 0;
        return res;
    }
    
    return -ENOENT;
}

//Truncate
//increase or decrease data size to size parameter
int aofs_truncate(const char *path, off_t size)
{
    uint32_t        location;
    char            data[size % DATA_SIZE];
    char            block[BLOCK_SIZE];
    uint8_t         bitmap[BITMAP_SIZE];
    uint16_t        bit_offset;
    uint16_t        byte_offset;
    struct stat     fstat;
    int             blocks_needed;
    int             block_cnt;
    int             res;
    uint16_t        remainder;
    uint32_t        bptr;      //pointer used to find deletion
    uint16_t        block_num;
    
    printf("truncate: %ld\n", size);
    
    location = LocateFile(path);
    
    if(!location)
        return -ENOENT;
    
    //Read in current bitmap
    pread(FS_FILE, bitmap, BITMAP_SIZE, MAGIC_SIZE);
    memset(block, 0, BLOCK_SIZE);
    
    //Grab stats info on file
    pread(FS_FILE, &fstat, STAT_SIZE, location + STAT_OFFSET);
    
    blocks_needed = (int)(size/DATA_SIZE) + 1;
    block_cnt = (int)fstat.st_blocks;
    
    fstat.st_blocks = blocks_needed;
    fstat.st_size   = size;
    pwrite(FS_FILE, &fstat, STAT_SIZE, location + STAT_OFFSET);
    
    block_num = 1;
    printf("truncate: block_cnt %d, blocks_needed %d\n", block_cnt, blocks_needed);
    //Need to delete extra blocks
    if(block_cnt > blocks_needed){
        printf("truncate: Need to delete %d blocks\n", block_cnt - blocks_needed);
        //Traverse to last needed block
        while(block_num != blocks_needed){
            
            //update stats
            fstat.st_blocks = blocks_needed;
            fstat.st_size   = size;
            pwrite(FS_FILE, &fstat, STAT_SIZE, location + STAT_OFFSET);
            
            //Set location to next pointer
            location = fstat.st_lspare;
            
            //Load next block stat
            pread(FS_FILE, &fstat, STAT_SIZE, location + STAT_OFFSET);
            block_num++;
        }
        
        //Start clearing blocks that are ahead
        bptr = fstat.st_lspare;
        fstat.st_lspare = 0;
        
        while(bptr){
            pread(FS_FILE, &fstat, STAT_SIZE, bptr + STAT_OFFSET);
            //Clear data
            pwrite(FS_FILE, block, BLOCK_SIZE, bptr);
            
            //Clear bitmap bit
            uint8_t block_used_mask;
            bit_offset = (bptr>>12) % BYTE_SIZE;
            byte_offset = (bptr>>15);
            block_used_mask = bitmap[byte_offset];
            block_used_mask &= ~(BIT_MASK>>bit_offset);
            bitmap[byte_offset] = block_used_mask;
            printf("truncate: clearing byte_offset %d, bit_offset %d\n", byte_offset, bit_offset);
            //Go to next block
            bptr = fstat.st_lspare;
        }
        //Write changes back
        pwrite(FS_FILE, bitmap, BITMAP_SIZE, MAGIC_SIZE);
    }
    //Need to allocate more blocks
    if(block_cnt < blocks_needed){
        //ignoring resizing up
        //Write should take care of that
        //return 0;
        //printf("truncate: block_cnt %d, blocks_needed %d\n", block_cnt, blocks_needed);
        printf("truncate: Need to allocate %d blocks\n", blocks_needed - block_cnt);
        
        //traverse to last allocated block
        bptr = fstat.st_lspare;
        while(bptr){
            //update current stats
            fstat.st_blocks = blocks_needed;
            fstat.st_size   = size;
            pwrite(FS_FILE, &fstat, STAT_SIZE, location + STAT_OFFSET);
            
            //Grab next stat
            pread(FS_FILE, &fstat, STAT_SIZE, bptr + STAT_OFFSET);
            block_num++;
            location = bptr;
            bptr = fstat.st_lspare;
            printf("bptr %d\n", bptr);
        }
        printf("We made it out\n");
        //allocate needed blocks
        while(block_num < blocks_needed){
            //allocate new block
            fstat.st_lspare = AllocateBlock();
            
            //writeback current stat
            pwrite(FS_FILE, &fstat, STAT_SIZE, location + STAT_OFFSET);
            
            //setup for next loop
            location = fstat.st_lspare;
            fstat.st_lspare = 0;
            fstat.st_flags = EXTEND;
            block_num++;
        }
    }
    
    //Traverse to last block
    while(block_num != blocks_needed){
        //update stats
        fstat.st_blocks = blocks_needed;
        fstat.st_size   = size;
        pwrite(FS_FILE, &fstat, STAT_SIZE, location + STAT_OFFSET);

        //Set location to next pointer
        location = fstat.st_lspare;

        //Load next block stat
        pread(FS_FILE, &fstat, STAT_SIZE, location + STAT_OFFSET);
        block_num++;
    }
    
    //update stats of last block
    fstat.st_blocks = blocks_needed;
    fstat.st_size = size;
    fstat.st_lspare = 0;
    printf("truncate: block_num %d\n", block_num);
    if(block_num > 1)
        fstat.st_flags = EXTEND;
    else
        fstat.st_flags = START;
    pwrite(FS_FILE, &fstat, STAT_SIZE, location + STAT_OFFSET);
    
    //adjust last block to correct size
    remainder = size % DATA_SIZE;
    memset(data, 0, sizeof(data));
    if(location)
        //Clear unsed portions of the file
        res = (int) pwrite(FS_FILE, data, DATA_SIZE-size, location+DATA_OFFSET+size);
    
    if (res == -1)
        return -ENOENT;
    
    return 0;
}

/** Remove a file */
int aofs_unlink(const char *path){
    
    uint64_t location;
    char block[BLOCK_SIZE];
    uint8_t bitmap[BITMAP_SIZE];
    uint8_t bit_offset;
    uint8_t byte_offset;
    struct stat fstat;
    
    location = LocateFile(path);
    
    if(!location){
        return -ENOENT;
    }
    
    //setup clear block
    memset(block, 0, BLOCK_SIZE);
    
    //Load bitmap
    pread(FS_FILE, bitmap, BITMAP_SIZE, MAGIC_SIZE);
    
    while(location){
        //Get stat
        pread(FS_FILE, &fstat, STAT_SIZE, location + STAT_OFFSET);
        //clear block
        pwrite(FS_FILE, block, BLOCK_SIZE, location);
        
        //Calculate bitmap location
        bit_offset = (location>>12) % BYTE_SIZE;
        byte_offset = (location>>15);
        
        //Clear bitmap bit
        uint8_t block_used_mask;
        block_used_mask = bitmap[byte_offset];
        block_used_mask &= ~(BIT_MASK>>bit_offset);
        bitmap[byte_offset] = block_used_mask;
        printf("unlink: Cleared byte_offset %d bit_offset %d\n", byte_offset, bit_offset);
        
        //Assign pointer
        location = fstat.st_lspare;
    }
    
    //Update bitmap
    pwrite(FS_FILE, bitmap, BITMAP_SIZE, MAGIC_SIZE);
    printf("unlink: %s\n", path);
    
    return 0;
}


static struct fuse_operations aofs_oper = {
    .getattr    = aofs_getattr,
    .readdir    = aofs_readdir,
    .open        = aofs_open,
    .read        = aofs_read,
    .write      = aofs_write,
    .create     = aofs_create,
    .fgetattr   = aofs_fgetattr,
    .truncate   = aofs_truncate,
    .unlink     = aofs_unlink,
};

int main(int argc, char *argv[])
{
    char aofs_path_cwd[1000];
    getwd(aofs_path_cwd);
    //"/usr/home/bwong20/bwong20/asgn4/FS_FILE";
    aofs_path = malloc(strlen(aofs_path_cwd + 9));
    strcpy(aofs_path, aofs_path_cwd);
    strcat(aofs_path, "/FS_FILE\0");

    FS_FILE = open(aofs_path, O_RDWR);
    
    if(FS_FILE < 0){
        printf("Unable to Mount AOFS");
        return -errno;
    }
    printf("AOFS Mounted\n");
    printf("Block Size %uB Blocks Total %u FS Size %uB\n", BLOCK_SIZE, BLOCK_TOTAL, FS_SIZE);
    return fuse_main(argc, argv, &aofs_oper, NULL);
}

