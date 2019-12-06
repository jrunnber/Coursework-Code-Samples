# include <stdio.h>
# include <stdlib.h>
# include <unistd.h>

# define numMB 250
# define sleepN 5

int main()
{
	srand(7);
	char** data = malloc(sizeof(char*)*numMB);

	for(int i = 0; i < numMB; i++){
		data[i] = malloc(1000*1024);
		if(data[i] == NULL){
			printf("Failed on i = %d", i);
			exit(1);
		}
		for(int j; j  < 1000*1024; j++){
			char temp = rand()%26 + 97;
			char* tString = data[i];
			tString[j] = temp;
			//if(i == j)
				//printf("Char: %c\n", temp);
		}
	}

	char* tString = data[0];
	for(int i = 0; i  < 20; i++)
		printf("%c", tString[i]);

	int w = sleepN;
	sleep(w);

	return 0;
}