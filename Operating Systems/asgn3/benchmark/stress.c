#include <unistd.h>
#include <sys/types.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <fcntl.h>
#include <time.h>
#include <string.h>
#include <stdio.h>

#define FALSE 0
#define TRUE 1
#define ERROR -1
#define STDIN 0
#define STDOUT 1
#define STDERROR 2
#define CWD_BUFF_SIZE 1024
#define NUM_PROC 11
#define TIME 180
#define STDOUT 1
#define INFILE "rawdata.txt"
#define OUTFILE "data.txt"



void forkProcess();
int checkForOperator(char * arg);
int formatData();

static int count = 0;
static char * args[] =  {"nice", "-n", "10", "./benchmark", NULL};
static char * args2[] = {"dmesg", NULL};
static char ** exeArgs;
static char snum[10];
static uint32_t numProc = 0;
static int outFd;

int main(int argc, char *argv[])
{
    exeArgs = (char**) malloc(sizeof(char*)*5);
    exeArgs[0] = args[0];
    exeArgs[1] = args[1];
    exeArgs[3] = args[3];
    
    
    if(argc == 1)
        numProc = NUM_PROC;
    else if(argc == 2){
        numProc = atoi(argv[1]);
    }else{
        printf("use: ./argshell [number of threads]\n");
        exit(-1);
    }
    
    forkProcess();
    wait(NULL);
    formatData();
}

void forkProcess(){
    
    pid_t pid;
    pid_t dmesgpid;
    int         status;
    int         nice;
    static int  record = 0;
    time_t currentTime;
    time_t waitTime;
    
    
    if((pid = fork())<0){
        perror("Fork Failed");
        exit(-1);
    }else{
        //Child
        if(pid == 0){
            nice = 0;
            sprintf(snum,"%d",nice);
            exeArgs[2] = snum;
            printf("Starting Process with Nice: %d\n", nice);
            status = execvp(exeArgs[0], exeArgs);
            if(status == -1){
                printf("Errno: %d\nCommand: %s\n", errno, exeArgs[0]);
                perror("Error");
                exit(-1);
            }
        }
        //Parent
        else{
            count++;
            if(count < numProc){
                currentTime = time(NULL);
                //while(difftime(time(NULL),currentTime) < TIME){}
                forkProcess();
            }
            else{
                currentTime = time(NULL);
                while(difftime(time(NULL),currentTime) < TIME){}
                outFd = open(INFILE,O_CREAT | O_RDWR | O_TRUNC, 0666 );
                waitTime = time(NULL);
                if((dmesgpid = fork())<0){
                    perror("Fork Failed");
                    exit(-1);
                }else{
                    //Child
                    if(dmesgpid == 0){
                        if (record == 0) {
                            close(STDOUT);
                            dup(outFd);
                            record = 1;
                            execvp(args2[0], args2);
                        }
                    }
                    //parent
                    else{
                        while(difftime(time(NULL),waitTime) < 1){}
                        close(outFd);
                    }
                }
                
            }
            kill(pid, SIGINT);
            
        }
    }
}

int formatData(){
    FILE* fin;
    FILE* fout;
    char *buff = NULL;
    size_t size = 0;
    
    fin = fopen(INFILE, "r");
    if(fin == NULL){
        printf("Unable to open file: %s", INFILE);
        return -1;
    }
    fout = fopen(OUTFILE,"w");
    if(fout == NULL){
        printf("Unable to open file: %s", OUTFILE);
        return -1;
    }
    
    while(getline(&buff, &size, fin)!= -1){
        if( buff[0] == 'P' && buff[1] == 'a'){
            fprintf(fout,"%s",buff);
        }
    }
    return 1;
}

