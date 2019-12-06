# include "stdlib.h"
# include "stdio.h"
# include "time.h"
# include "stdint.h"

# define BILLION 1000000000
# define NUMFILES 99

int main(){
	srand(7);

	FILE* f;
	int32_t bytesW = 3000;
	char fileName[16] = {'m', 'n', 't', '/', 'f', 'i', 'l', 'e', '0', '0', '0', '.', 't', 'x', 't', '\0'};
	char c;

	struct timespec tpSta, tpFin;
	long sec, nano;
	clock_gettime(CLOCK_REALTIME, &tpSta);

	for(int i = 1; i < NUMFILES; i++){
		f = fopen(fileName, "w+");
		for(int j = 0; j < bytesW; j++)
			fprintf(f, "%c", rand()%26 + 97);
	
		if(i%100 == 0){
			fileName[10] -= 9;
			fileName[9]  -= 9;	
			fileName[8]++;
		} else if(i%10 == 0){
			fileName[10] -= 9;
			fileName[9]++;
		} else {
			fileName[10]++;
		}

		if(i%33 == 0)
			bytesW *= 2;

		fclose(f);
	}

	clock_gettime(CLOCK_REALTIME, &tpFin);
	sec  = tpFin.tv_sec  - tpSta.tv_sec;
	nano = tpFin.tv_nsec - tpSta.tv_nsec;
	if(nano < 0){
		sec--;
		nano += BILLION;
	}

	printf("Time to create and write to files:\n");
	printf("%ld seconds and %ld nanoseconds\n", sec, nano);

	fileName[10] = fileName[9] = '0';

	clock_gettime(CLOCK_REALTIME, &tpSta);

	for(int i = 1; i < NUMFILES; i++){
		f = fopen(fileName, "r+");
		
		int count = 0;
		while(fscanf(f, "%c", &c) == 1){ count++; }
		//printf("Count[%d] = %d", i - 1, count);
		
		if(i%10 == 0){
			fileName[10] -= 9;
			fileName[9]++;
		} else {
			fileName[10]++;
		}

		fclose(f);
	}

	clock_gettime(CLOCK_REALTIME, &tpFin);
	sec  = tpFin.tv_sec  - tpSta.tv_sec;
	nano = tpFin.tv_nsec - tpSta.tv_nsec;
	if(nano < 0){
		sec--;
		nano += BILLION;
	}

	printf("Time to read files:\n");
	printf("%ld seconds and %ld nanoseconds\n", sec, nano);

}