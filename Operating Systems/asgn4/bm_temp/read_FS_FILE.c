
#define aofs_path "/usr/home/jrunnber/test/FS_FILE"
#define NUM_BYTE 48 //be divisible by 4

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
    
    pread(FS_FILE, temp, NUM_BYTE + 4, 0);
    
    for(int i = 4; i < NUM_BYTE; i+=4){
        for(int j = 0; j < 4; j++){
            uint8_t shiftMe = temp[i + j];
            //printf("%d: ", shiftMe);
            for(int b = 0; b < 8; b++){
                if(shiftMe >= 128){
                    printf("1");
                } else {
                    printf("0");
                }
                shiftMe = shiftMe << 1;
            }
            printf("   ");
        }
        printf("\n");
    }


    /*
    for(int i = 0; i < NUM_BYTE; i++){
        printf("%X",temp[i]);
    }*/
    printf("\n");
    close(FS_FILE);
    
    
}
