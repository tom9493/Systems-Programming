#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<sys/types.h>
#include<sys/wait.h>

int main(int argc, char *argv[]) {
	int pid;
	int fds[2];

	printf("Starting program; process has pid %d\n", getpid());
	FILE *fp = fopen("fork-output.txt", "w");
	fprintf(fp, "BEFORE FORK\n");
	fflush(fp);

	if (pipe(fds) == -1) {
		printf("An error occured with opening the pipe\n");
		return 1;
	}

	if ((pid = fork()) < 0) {
		fprintf(stderr, "Could not fork()");
		exit(1);
	}

	/* BEGIN SECTION A */

	fprintf(fp, "SECTION A\n");
	fflush(fp);
	printf("Section A;  pid %d\n", getpid());
	//sleep(30);
	
	/* END SECTION A */
	if (pid == 0) {
		/* BEGIN SECTION B */

		printf("Section B\n");
		fprintf(fp, "SECTION B\n");
		fflush(fp);
		close(fds[0]);
		FILE* fp2 = fdopen(fds[1], "w");
		fputs("hello from section B\n", fp2);
		
	//	sleep(30);
	//	sleep(30);
	//	printf("Section B done sleeping\n");


		char *newenviron[] = { NULL };

		printf("Program \"%s\" has pid %d. Sleeping.\n", argv[0], getpid());
	//      sleep(30);

	        if (argc <= 1) {
	                 printf("No program to exec.  Exiting...\n");
	                 exit(0);
		}

		printf("Running exec of \"%s\"\n", argv[1]);
		int fpfd = fileno(fp);
		dup2(fpfd, STDOUT_FILENO);
		execve(argv[1], &argv[1], newenviron);
		printf("End of program \"%s\".\n", argv[0]);
								 
		exit(0);

		/* END SECTION B */
	} else {
		/* BEGIN SECTION C */

		printf("Section C\n");
		fprintf(fp, "SECTION C\n");
		fflush(fp);
		//sleep(30);
		waitpid(0, NULL, 0);

		close(fds[1]);
		FILE* fd2 = fdopen(fds[0], "r");
		char str[1000];
		fgets(str, sizeof str, fd2);
		fprintf(stdout, "%s\n", str);

		//printf("Section C done sleeping\n");

		exit(0);

		/* END SECTION C */
	}
	/* BEGIN SECTION D */

	fprintf(fp, "SECTION D\n");
	fflush(fp);
	printf("Section D\n");
	//sleep(30);

	/* END SECTION D */
	fclose(fp);
}

