#define FS_SIZE 4096*4096*8
#define MAGIC_0 0xFA
#define MAGIC_1 0x19
#define MAGIC_2 0x28
#define MAGIC_3 0x3E
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

char* aofs_path;

int main(int argc, char *argv[])
{
    char aofs_path_cwd[1000];
    getwd(aofs_path_cwd);
    aofs_path = malloc(strlen(aofs_path_cwd + 9));
    strcpy(aofs_path, aofs_path_cwd);
    strcat(aofs_path, "/FS_FILE\0");

    int FS_FILE;
    char buf[FS_SIZE];
    int res;
    uint8_t temp[4];
    size_t nbytes = FS_SIZE;
    FS_FILE = open(aofs_path, O_CREAT|O_RDWR|O_TRUNC,0666);
    
    
    if(FS_FILE != -1)
        printf("File Opened\n");
    
    memset(&buf,0,nbytes);
    buf[0] = MAGIC_0;
    buf[1] = MAGIC_1;
    buf[2] = MAGIC_2;
    buf[3] = MAGIC_3;
    buf[4] = 0b10000000;
    printf("memset\n");
    res = pwrite(FS_FILE,buf,nbytes, 0);
    
    if(res == -1){
        perror("FS_FILE");
        printf("Unable to format FS_FILE\n");
    }
    read(FS_FILE,temp,NUM_BYTE);
    for(int i = 0; i < NUM_BYTE; i++){
        printf("%X",temp[i]);
    }
    printf("\n");
    close(FS_FILE);
    
    
}
