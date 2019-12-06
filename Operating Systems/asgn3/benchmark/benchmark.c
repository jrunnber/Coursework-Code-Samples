 #include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <fcntl.h>

#define STDIN 0
#define STDOUT 1

int main(){
    unsigned long int ram;
    int size;
    
    ram = sysconf(_SC_PHYS_PAGES) * sysconf(_SC_PAGESIZE);
    size = ram/20;
    char * a;
    char * b;

    a = (char*)malloc(sizeof(char)*size);
    b = (char*)malloc(sizeof(char)*size);
    srand(time(NULL));

    for(unsigned int i = 0; i < size; i++){
        a[i] = i;
        b[i] = i;
    }

    while(1){
        int r = rand() % (size + 1);

        if(a[r] == b[r]){
            r = 2+5;
        }else{
            printf("FAILURE!!!!!!!\n");
        }
    }
    return 1;
}
