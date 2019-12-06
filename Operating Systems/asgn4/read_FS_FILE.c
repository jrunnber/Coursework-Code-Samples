
#define aofs_path "/usr/home/bwong20/bwong20/asgn4/FS_FILE"
#define NUM_BYTE 6

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

int main(int argc, char *argv[])
{
    int FS_FILE;
    uint8_t temp[NUM_BYTE];
    FS_FILE = open(aofs_path, O_RDONLY);
    
    
    if(FS_FILE != -1)
        printf("File Opened\n");
    
    pread(FS_FILE, temp, NUM_BYTE, 0);
    for(int i = 0; i < NUM_BYTE; i++){
        printf("%X",temp[i]);
    }
    printf("\n");
    close(FS_FILE);
    
    
}
