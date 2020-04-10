#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>

/* closes all child processes how turned zombies. the while loop
 * continues until no zombies childs exists.
 */
void my_zombies_handler( int signum,siginfo_t* info,void* ptr){
	int zombies_exists = 1;
	int value;
	int status;
	while (zombies_exists){
		value = waitpid(-1,&status,WNOHANG);
		if (value==0 || value==-1){
			zombies_exists = 0;
		}
	}
}

void my_ignore_handler( int signum,siginfo_t* info,void* ptr){
}

/* returns which operation the shell needs to perform 
retruns 1 if operation is executing commands
returns 2 if opertaion is executing commands in the background
returns 3 if operation is single piping
 */
int what_operation(int count, char** arglist){
	if (strcmp(arglist[count-1],"&")==0){
		return 2;
	}
	for (int i=0;i<count;i++){
		if (strcmp(arglist[i],"|")==0){
			return 3;
		}
	}
	return 1;
}

void single_piping(int count, char** arglist){
	int status;
	int successfull_execvp = 0;
	int pipe_success;
	for (int i=0;i<count;i++){
		if (strcmp(arglist[i],"|")==0){  /* finding the pipe symbol location*/
			int pfds[2];
			pipe_success = pipe(pfds);
			if (pipe_success == -1){
				printf("%s\n",strerror(errno));
				exit(1);
			}
			int pipe_pid1 = fork();
			if (pipe_pid1<0){
				printf("%s\n",strerror(errno));
				exit(1);
			}
			/*first son - the son that receives messages from the second son*/
			else if (pipe_pid1==0){
				struct sigaction def_handler;
				memset(&def_handler,0,sizeof(def_handler));
				def_handler.sa_handler = SIG_DFL;
				if (0!= sigaction(SIGINT,&def_handler,NULL)){
					printf("Signal handle registration " "failed. %s\n",strerror(errno));
					exit(1);
				}
				close(pfds[1]);
				dup2(pfds[0],0);
				char** arglist_for_first_son = arglist+i+1;
				successfull_execvp = execvp(arglist_for_first_son[0],arglist_for_first_son);
				if (successfull_execvp==-1){
					printf("%s\n",strerror(errno));
					exit(1);
				}
			}
			else{
				int pipe_pid2 = fork();
				if (pipe_pid2<0){
					printf("%s\n",strerror(errno));
					exit(1);
				}
				/*second son - the son that sends messages from the first son*/
				else if (pipe_pid2==0){
					struct sigaction def_handler;
					memset(&def_handler,0,sizeof(def_handler));
					def_handler.sa_handler = SIG_DFL;
					if (0!= sigaction(SIGINT,&def_handler,NULL)){
						printf("Signal handle registration " "failed. %s\n",strerror(errno));
						exit(1);
					}
					close(pfds[0]);
					dup2(pfds[1],1);
					char** arglist_for_second_son = arglist;
					arglist_for_second_son[i] = NULL;
					successfull_execvp = execvp(arglist_for_second_son[0],arglist_for_second_son);
					if (successfull_execvp==-1){
						printf("%s\n",strerror(errno));
						exit(1);
					}
				}
				/* father code*/
				else{
					close(pfds[1]);
					close(pfds[0]);
					/* waiting for the son that sends to end and then to the son that receives*/
					waitpid(pipe_pid2,&status,0);
					waitpid(pipe_pid1,&status,0);
				}
			}
		}
	}
}

void executing_commands(int count, char** arglist) {
	int successfull_execvp;
	int status;
	int command_pid = fork();
	if (command_pid<0){
		printf("%s\n",strerror(errno));
		exit(1);
	}
	else if (command_pid == 0){

	    struct sigaction def_handler;
		memset(&def_handler,0,sizeof(def_handler));
		def_handler.sa_handler = SIG_DFL;
		if (0!= sigaction(SIGINT,&def_handler,NULL)){
			printf("Signal handle registration " "failed. %s\n",strerror(errno));
		}
		successfull_execvp = execvp(arglist[0],arglist);
		if (successfull_execvp==-1){
			printf("%s\n",strerror(errno));
			exit(1);
		}
	}
	else{
		waitpid(command_pid,&status,0);
	}
}

void executing_commands_in_the_background(int count, char** arglist){
	int successfull_execvp;
	int background_pid = fork();
	if (background_pid<0){
		printf("%s\n",strerror(errno));
		exit(1);
	}
	else if (background_pid==0){
		/* signal handler for background processes. ignores SIGINT*/
	    struct sigaction background_handler;
		memset(&background_handler,0,sizeof(background_handler));
		background_handler.sa_handler = SIG_IGN;
		if (0!= sigaction(SIGINT,&background_handler,NULL)){
			printf("Signal handle registration " "failed. %s\n",strerror(errno));
		}
		arglist[count-1] = NULL;
		successfull_execvp = execvp(arglist[0],arglist);
		if (successfull_execvp==-1){
			printf("%s\n",strerror(errno));
			exit(1);
		}
	}
	else{
	}
}

int process_arglist(int count, char** arglist)
{
	/* signal handler for the shell process*/
	struct sigaction main_handler;
	memset(&main_handler,0,sizeof(main_handler));
	main_handler.sa_sigaction = my_ignore_handler;
	main_handler.sa_flags = SA_SIGINFO | SA_RESTART;
	if (0!= sigaction(SIGINT,&main_handler,NULL)){
		printf("Signal handle registration " "failed. %s\n",strerror(errno));
		return 0;
	}
	/* signal handler for SIGCHLD signals */
	struct sigaction child_handler;
	memset(&child_handler,0,sizeof(child_handler));
	child_handler.sa_sigaction = my_zombies_handler;
	child_handler.sa_flags = SA_SIGINFO | SA_RESTART;
	if (0!= sigaction(SIGCHLD,&child_handler,NULL)){
		printf("Signal handle registration " "failed. %s\n",strerror(errno));
		return 0;
	}
	int command_number = what_operation(count,arglist);
	if (command_number==1){
		executing_commands(count,arglist);
	}
	else if (command_number == 2){
		executing_commands_in_the_background(count,arglist);
	}
	else{
		single_piping(count,arglist);
	}
	return 1;
}

int prepare(void){
	return 0;
}
int finalize(void){
	return 0;
}
