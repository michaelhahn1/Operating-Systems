#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <dirent.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <string.h>
#include<errno.h>

typedef struct queue_of_directories{
	struct node* first;
	struct node* last;
}queue_of_directories;

typedef struct node{
	struct node* ahead_of_me;
	struct node* behind_me;
	char dir_name[500];
}node;

queue_of_directories* my_queue = NULL;
pthread_mutex_t mutex;
pthread_cond_t  new_entry;
char* expression;
int expressions_found_counter = 0;
int number_of_closed_threads = 0;
int number_of_waiting_threads = 0;
int amount_of_threads;
int finish_code=0; //1 for error in every thread, 0 otherwise
bool sigint_send = false;
pthread_t* threads;
int threads_exited_with_error_counter = 0;

/*
 * deletes the linked list
 */
void delete_queue(){
	if (my_queue == NULL){
		return;
	}
	node* curr_node = my_queue->first;
	node* next;
	if (curr_node==NULL){
		free(my_queue);
		return;
	}
	while(curr_node!=NULL){
		next = curr_node->behind_me;
		free(curr_node);
		curr_node = next;
	}
	free(my_queue);
}

/*
 * changes sigint_send to true and brodcasts. the search_first_direcotry_in_queue and
 * main_thread_function functions will check at specific points if sigint_send == ture and if
 * so they will exit gracefully.
 */
void sig_handler(int signum,siginfo_t* info,void* ptr){
	sigint_send = true;
	pthread_cond_broadcast(&new_entry);
}

/*
 * iterates over all files in a directory, and adds them to the queue or print them if needed.
 */
void search_first_direcotry_in_queue(long index){
	bool ignore_file = false;
	node* curr_node = NULL;
	int is_dir= 0;
	curr_node = my_queue->first;
	char path_for_current_file[500];
	int rc;
	if (my_queue->first->behind_me!=NULL){
		my_queue->first = my_queue->first->behind_me;
		my_queue->first->ahead_of_me = NULL;
	}
	else{
		my_queue->first = NULL;
		my_queue->last = NULL;
	}
	rc = pthread_mutex_unlock(&mutex);
	if (rc!=0){
		fprintf(stderr,"couldn't unlock thread %ld\n",index);
		__sync_fetch_and_add(&threads_exited_with_error_counter,1);
		__sync_fetch_and_add(&number_of_closed_threads,1);
		pthread_exit(NULL);
	}
	strcpy(path_for_current_file,curr_node->dir_name);
	DIR* current_dir = opendir(curr_node->dir_name);
	if (current_dir == NULL){
		fprintf(stderr,"couldn't open dir %s for thread %ld\n",curr_node->dir_name,index);
		__sync_fetch_and_add(&threads_exited_with_error_counter,1);
		__sync_fetch_and_add(&number_of_closed_threads,1);
		pthread_exit(NULL);
	}
	struct dirent *current_file = readdir(current_dir);
	//iterating over all files in this directory
	while(current_file!=NULL){
		strcpy(path_for_current_file,curr_node->dir_name);

		if (strcmp(current_file->d_name,".")==0){
			ignore_file = true;
		}
		else if (strcmp(current_file->d_name,"..")==0){
			ignore_file = true;
		}
		if (ignore_file==false){ //file is not . or ..
			strcat(path_for_current_file,"/");
			strcat(path_for_current_file,current_file->d_name);
			struct stat stats_for_current_file;
			stat(path_for_current_file,&stats_for_current_file);
			if(__S_IFDIR == (stats_for_current_file.st_mode&__S_IFMT)){
				is_dir = 1;
			}
			else{
				is_dir = 0;
			}
			if (is_dir==0){ //file is not a directory
				if(strstr(current_file->d_name,expression)!=NULL){
					__sync_fetch_and_add(&expressions_found_counter,1);
					printf("%s\n",path_for_current_file);
				}
			}
			else{  //file is a directory
				rc = pthread_mutex_lock(&mutex);
				if (rc!=0){
					fprintf(stderr,"couldn't lock thread %ld\n",index);
					__sync_fetch_and_add(&threads_exited_with_error_counter,1);
					__sync_fetch_and_add(&number_of_closed_threads,1);
					pthread_exit(NULL);
				}
				node* new_node = (node*)malloc(sizeof(node)*1);
				new_node->ahead_of_me = my_queue->last;
				new_node->behind_me = NULL;
				strcpy(new_node->dir_name,path_for_current_file);
				if(my_queue->first==NULL){
					my_queue->first = new_node;
					my_queue->last = new_node;
				}
				else{
					my_queue->last->behind_me = new_node;
					my_queue->last = new_node;
				}
				rc = pthread_mutex_unlock(&mutex);
				if (rc!=0){
					fprintf(stderr,"couldn't unlock thread %ld\n",index);
					__sync_fetch_and_add(&threads_exited_with_error_counter,1);
					__sync_fetch_and_add(&number_of_closed_threads,1);
					pthread_exit(NULL);
				}
				pthread_cond_signal(&new_entry);
			}
		}
		current_file = readdir(current_dir);
		ignore_file = false;
	}
	int success = closedir(current_dir);
	if (success==-1){ //couldn't close directory
		fprintf(stderr,"thread %ld couldn't close directory\n",index);
		__sync_fetch_and_add(&threads_exited_with_error_counter,1);
		__sync_fetch_and_add(&number_of_closed_threads,1);
		pthread_exit(NULL);
	}
	ignore_file = false;
	if (sigint_send == true){
		pthread_exit(NULL);
	}
	rc = pthread_mutex_lock(&mutex);
	if (rc!=0){
		fprintf(stderr,"couldn't lock thread %ld\n",index);
		__sync_fetch_and_add(&threads_exited_with_error_counter,1);
		__sync_fetch_and_add(&number_of_closed_threads,1);
		pthread_exit(NULL);
	}
}

/*
 * the main thread function. as long as the queue is not empty it calls
 * the function search_first_direcotry_in_queue and when the queue is empty
 * the thread waits until a signal arrives.
 * a thread will exit this function if sigint_send == true (happens when sigint is send),
 * or when all threads are sleeping
 */
void *main_thread_function(void *t){
	long index = (long)t;
	int rc;
	if (sigint_send == true){
		pthread_exit(NULL);
	}
	rc = pthread_mutex_lock(&mutex);
	if (rc!=0){
		fprintf(stderr,"couldn't lock thread %ld\n",index);
		__sync_fetch_and_add(&threads_exited_with_error_counter,1);
		__sync_fetch_and_add(&number_of_closed_threads,1);
		pthread_exit(NULL);
	}
	while (true){
		while (my_queue->first!=NULL){ //there are directories in the queue
			search_first_direcotry_in_queue(index);
		}
		rc = pthread_mutex_unlock(&mutex);
		if (rc!=0){
			fprintf(stderr,"couldn't unlock thread %ld\n",index);
			__sync_fetch_and_add(&threads_exited_with_error_counter,1);
			__sync_fetch_and_add(&number_of_closed_threads,1);
			pthread_exit(NULL);
		}
		__sync_fetch_and_add(&number_of_waiting_threads,1);
		if(number_of_waiting_threads == amount_of_threads-number_of_closed_threads){ //all threads are inactive
			pthread_cond_broadcast(&new_entry);
			pthread_exit(NULL);
		}
		if(sigint_send == true){
			pthread_exit(NULL);
		}
		rc = pthread_mutex_lock(&mutex);
		if (rc!=0){
			fprintf(stderr,"couldn't lock thread %ld\n",index);
			__sync_fetch_and_add(&threads_exited_with_error_counter,1);
			__sync_fetch_and_add(&number_of_closed_threads,1);
			pthread_exit(NULL);
		}
		pthread_cond_wait(&new_entry, &mutex);
		__sync_fetch_and_sub(&number_of_waiting_threads,1);
		if (sigint_send == true){
			pthread_mutex_unlock(&mutex);
			pthread_exit(NULL);
		}
	}
}

int main ( int argc, char** argv )
{
	int rc;
	int success = 0;
	if (argc!=4){
		fprintf(stderr,"wrong number of arguments\n");
		exit(1);
	}
	expression = argv[2];
	struct sigaction new_action;
	memset(&new_action,0,sizeof(new_action));
	new_action.sa_sigaction = &sig_handler;
	new_action.sa_flags = SA_SIGINFO| SA_RESTART;
	success = sigaction(SIGINT,&new_action,NULL);
	if (success!= 0){
		fprintf(stderr," couldn't register signal handler\n");
		return 1;
	}
	//Initialising the queue
	node* root = (node*) malloc(sizeof(node)*1);
	root->ahead_of_me = NULL;
	root->behind_me = NULL;
	strcpy(root->dir_name,argv[1]);
	my_queue = (queue_of_directories*) malloc(sizeof(queue_of_directories)*1);
	my_queue->first = root;
	my_queue->last = root;
	amount_of_threads= atoi(argv[3]);
	threads = (pthread_t*)malloc(sizeof(pthread_t) * amount_of_threads);
	pthread_mutex_init(&mutex, NULL);
	pthread_cond_init (&new_entry, NULL);
	long t;
	for( t=0;t<amount_of_threads;t++){
		rc = pthread_create( &threads[t],
				NULL,
				main_thread_function,
				(void*)t );
		if( rc )
		{
			printf("ERROR in pthread_create(): "
					"%s\n", strerror(rc));
			exit(1);
		}
	}
	for (int i=0;i<amount_of_threads;i++){
		pthread_join(threads[i],NULL);
	}
	free(threads);
	delete_queue();
	if(sigint_send == 1){ //sigint
		printf("Search stopped, found %d files\n",expressions_found_counter);
	}
	else{
		printf("Done searching, found %d files\n",expressions_found_counter);
	}
	if (threads_exited_with_error_counter == amount_of_threads){
		exit(1);
	}
	exit(0);
}
