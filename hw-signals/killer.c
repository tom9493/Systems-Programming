#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/wait.h>

void sigint_handler(int signum) {
	// send SIGKILL to all processes in group, so this process and children
	// will terminate.  Use SIGKILL because SIGTERM and SIGINT (among
	// others) are overridden in the child.
	kill(-getpgid(0), SIGKILL);
}

int main(int argc, char *argv[]) {
	char *scenario = argv[1];
	int pid = atoi(argv[2]);

	struct sigaction sigact;

	// Explicitly set flags
	sigact.sa_flags = SA_RESTART;
	sigact.sa_handler = sigint_handler;
	// Override SIGINT, so that interrupting this process sends SIGKILL to
	// this one and, more importantly, to the child.
	sigaction(SIGINT, &sigact, NULL);
// SIGHUP AND SIGINT PRINT 1 AND THEN 2
// SIGQUIT PRINTS 8, 1, SLEEPS 4 SECONDS, PRINTS 2, SLEEPS 4 SECONDS, THEN PRINTS 9
// SIGTERM PRINTS THE VALUE OF FOO, ORIGINALLY -1 
// 30 SETS FOO TO 6 IF FOO > 0, LIKELY IF FORK HAS BEEN CALLED, WHICH IS THE ONE BELOW THIS
// 10 MAKES FOO EQUAL THE PID. The child exits with code 7
// 16 (HANDLER 6) cALLS WAITPID -1 TO SEE IF ANY CHILD PROCESS CHANGED STATE AND WAITS IF IT HAS. IF NOT PRINT ERROR MESSAG
// 31 (HANDLER 7) TOGGLES BLOCK 
// 12 (HANDLER 8) RETURNS SIGTERM TO THE ORIGINAL BEHAVIOR, TERMINATE THE PROGRAM
// SIGCHLD (HANDLER 9) WAITS FOR A CHILD PROCESS TO TERMINATE AND PRINTS ITS EXIT STATUS NUMBER
	switch (scenario[0]) {
	case '0':
		kill(pid, SIGINT);
		sleep(1);
		break;
	case '1':
		kill(pid, 12);
		sleep(1);
		kill(pid, SIGTERM);
		break;
	case '2':
		kill(pid, SIGINT);
		sleep(5);
		kill(pid,12);
		sleep(1);
		kill(pid, SIGTERM);
		break;
	case '3':
		kill(pid, SIGINT);
		sleep(5);
		kill(pid, SIGINT);
		sleep(5);
		kill(pid, 12);
		sleep(1);
		kill(pid, SIGTERM);
		break;
	case '4':
		kill(pid, SIGINT);
		sleep(1);
		kill(pid, SIGHUP);
		sleep(5);
		kill(pid, 12);
		sleep(1);
		kill(pid, SIGTERM);
		break;
	case '5':
		kill(pid, SIGINT);
		kill(pid, 12);
		sleep(1);
		kill(pid, SIGTERM);
		break;
	case '6':       
		kill(pid, SIGHUP);
		sleep(5);
		kill(pid, 10);
		sleep(1);
		kill(pid, 12);
		sleep(1);
		kill(pid, 16);
		sleep(1);
		kill(pid, SIGTERM);
		break;
	case '7':
		kill(pid, SIGHUP);
		sleep(5);
		kill(pid, 10);
		sleep(1);
		kill(pid, 12);
		sleep(1);
		kill(pid, SIGTERM);
		break;
	case '8':               /// Fix this one to print 1, 2, 6
		kill(pid, SIGHUP); // Prints the 1, 2
		sleep(5);
		kill(pid, 31);
		sleep(1);
		kill(pid, 10); // Forks so foo > 0 but prints out a 7 so fast, how to stop that? 
		sleep(1);
		kill(pid, 30); // if foo > 0, foo = 6
		sleep(1);
		kill(pid, SIGTERM); // Prints foo if not default
		sleep(1);
		kill(pid, 12); // Sets SIGTERM to default behavior
		sleep(1);
		kill(pid, SIGTERM); // Terminates
		break;
	case '9':
	       	kill(pid, 31); // toggles block
	       	sleep(1);
		kill(pid, SIGQUIT); // 8, 1, 2, 9 rotation, but 1, 2 should be blocked 
 		sleep(5);
		kill(pid, 31); // unblocks sigint
		sleep(1);
		kill(pid, 12);
		sleep(1);
		kill(pid, SIGTERM);		
		break;

	}
	waitpid(pid, NULL, 0);
}
