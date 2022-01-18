/* 
1.	1,2,3
2.	2
3.	
	#include <sys/types.h>
	#include <sys/stat.h>
	#include <fcntl.h>
4.	2, 7
5.	SO_ACCEPTCONN
6.	1
7.	Null-terminated
8.	An integer greater than zero

I completed the TMUX exercise from Part 2

*/
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

int main(int argc, char *argv[]) {
	FILE *f;
	char s[100] = "hi";
	char *env;
	if (getenv("CATMATCH_PATTERN")) { env = getenv("CATMATCH_PATTERN"); }

	fprintf(stderr, "%d\n\n", getpid());
	f = fopen(argv[1], "r");
	fgets(s, 100, f);
		
	while ( !feof(f) ) {   /* !feof(f) */
		if (getenv("CATMATCH_PATTERN")) { if (strstr(s, env)) { printf("1 "); }}
		else { printf("0 "); }
		printf("%s\n", s);
		fgets(s, 100, f);	
	}
}

