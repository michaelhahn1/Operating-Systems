
#include <fcntl.h>      /* open */ 
#include <unistd.h>     /* exit */
#include <sys/ioctl.h>  /* ioctl */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "message_slot.h"

int main(int argc,char *argv[]){
	int file_desc,channel,ret_val;
	if(argc!=4){
		perror("Wrong number of arguments");
		return 1;
	
	}
	file_desc = open(argv[1],O_RDWR);
	if (file_desc<0){
		perror("Couldn't open the driver");
		return 1;
	}
	channel = atoi(argv[2]);
	ret_val = ioctl(file_desc,MSG_SLOT_CHANNEL,channel);
	if (ret_val<0){
		perror("Couldn't execute ioctl function");
		return 1;
	}
	ret_val = write(file_desc,argv[3],strlen(argv[3]));
	if (ret_val<0){
		perror("Couldn't write to driver");
		return 1;
	}
	close(file_desc);
	return 0;
}
