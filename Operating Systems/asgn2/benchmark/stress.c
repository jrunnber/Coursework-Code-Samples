#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <fcntl.h>
#include <time.h>

#define FALSE 0
#define TRUE 1
#define ERROR -1
#define STDIN 0
#define STDOUT 1
#define STDERROR 2
#define CWD_BUFF_SIZE 1024
#define NUM_PROC 100
#define TIME 240
#define STDOUT 1



void forkProcess();

int checkForOperator(char * arg);
static int count = 0;
static char * args[] =  {"nice", "-n", "10", "./benchmark", NULL};
static char * args2[] = {"dmesg", NULL};
static char ** exeArgs;
static char snum[10];
static uint32_t numProc = 0;
static int outFd;


time_t currentTime;
time_t waitTime;

int main(int argc, char *argv[])
{
    outFd = open("data.csv",O_CREAT | O_RDWR | O_TRUNC, 0666 );
    
    
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
    currentTime = time(NULL);
    forkProcess();
    wait(NULL);
}

void forkProcess(){
    
    pid_t pid;
    pid_t dmesgpid;
    int         status;
    int         nice;
    static int  record = 0;
    
    
    if((pid = fork())<0){
        perror("Fork Failed");
        exit(-1);
    }else{
        //Child
        if(pid == 0){
            nice = (numProc - count) % 21;
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
            if(count < numProc)
                forkProcess();
            while(difftime(time(NULL),currentTime) < TIME){}
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
                }
            }
            kill(pid, SIGINT);
        }
    }
}

