
#undef __KERNEL__
#define __KERNEL__
#undef MODULE
#define MODULE

#include <linux/kernel.h>   /* We're doing kernel work */
#include <linux/module.h>   /* Specifically, a module */
#include <linux/fs.h>       /* for register_chrdev */
#include <linux/uaccess.h>  /* for get_user and put_user */
#include <linux/string.h>   /* for memset. NOTE - not string.h!*/
#include <linux/slab.h>
#include <linux/errno.h>
#include "message_slot.h"

MODULE_LICENSE("GPL");

typedef struct channels_for_current_minor{
	struct channels_for_current_minor* next_channel;
	struct channels_for_current_minor* previous_channel;
	unsigned long channel_number;
	char buffer[128];
	int message_length;
	int valid_message;
}channels_for_current_minor;


typedef struct minors_for_current_device{
	int minor_number;
	struct channels_for_current_minor* first_channel;
	struct minors_for_current_device* next_minor;
	struct minors_for_current_device* previous_minor;
}minors_for_current_device;

static minors_for_current_device* head = NULL;
static int current_minor_number;

minors_for_current_device* find_current_minor(void){
	minors_for_current_device* curr_node = head;
	if (curr_node==NULL){
		return 0;
	}
	else{
		while(curr_node!=NULL){
			if (curr_node->minor_number == current_minor_number){
				return curr_node;
			}
			curr_node = curr_node->next_minor;
		}
	}
	return NULL;
}

void initialize_minor(minors_for_current_device* minor){
	minor->first_channel = NULL;
	minor->minor_number = current_minor_number;
	minor->next_minor = NULL;
	minor->previous_minor = NULL;
}

void initialize_channel(channels_for_current_minor* channel,unsigned long ioctl_param){
	channel->channel_number = ioctl_param;
	channel->next_channel = NULL;
	channel->previous_channel = NULL;
	channel->message_length = 0;
	channel->valid_message = 0;
}


void free_all_channels(minors_for_current_device* minor){
	channels_for_current_minor* head = minor->first_channel;
	channels_for_current_minor* next;
	if (head==NULL){
		return;
	}
	next = head->next_channel;
	while(next!=NULL){
		kfree(head);
		head = next;
		next = next->next_channel;
	}
	kfree(head);
}

void free_all_minors(void){
	minors_for_current_device* next;
	if (head==NULL){
		return;
	}
	next = head->next_minor;
	while(next!=NULL){
		free_all_minors();
		kfree(head);
		head = next;
		next = next->next_minor;
	}
	free_all_channels(head);
	kfree(head);
}

//================== DEVICE FUNCTIONS ===========================
static int device_open( struct inode* inode,
		struct file*  file )
{
	current_minor_number = iminor(inode);
	if(find_current_minor()!=NULL){
		return 0;
	}
	if (head==NULL){
		head = (minors_for_current_device*) kmalloc(sizeof(minors_for_current_device),GFP_KERNEL);
		if (!head){
			return -ENOMEM;
		}
		initialize_minor(head);

	}
	else{
		minors_for_current_device* temp = (minors_for_current_device*)
												kmalloc(sizeof(minors_for_current_device),GFP_KERNEL);
		if(!temp){
			return -ENOMEM;
		}
		initialize_minor(temp);
		head->previous_minor = temp;
		temp->next_minor = head;
		head = temp;
	}
	return 0;
}

static long device_ioctl(struct file *file,unsigned int ioctl_num,unsigned long ioctl_param){
		int channel_was_found = 0;
	minors_for_current_device* temp;
	channels_for_current_minor* current_channel;
	if(ioctl_num!=MSG_SLOT_CHANNEL || ioctl_param==0){
		return -EINVAL;
	}
	current_minor_number =  iminor(file->f_path.dentry->d_inode);
	temp = find_current_minor();
	if (temp==NULL){
		return -EINVAL;
	}
	current_channel = temp->first_channel;
	while(current_channel!=NULL){
		if (current_channel->channel_number == ioctl_param){
			channel_was_found = 1;
			file->private_data = (void*)current_channel;
			break;
		}
		current_channel = current_channel->next_channel;
	}
	if(channel_was_found == 0){
		channels_for_current_minor* new_channel = (channels_for_current_minor*)kmalloc
				(sizeof(channels_for_current_minor),GFP_KERNEL);
		if(!new_channel){
			return -ENOMEM;
		}
		initialize_channel(new_channel,ioctl_param);
		if (temp->first_channel==NULL){
			temp->first_channel = new_channel;
		}
		else{
			new_channel->next_channel = temp->first_channel;
			temp->first_channel->previous_channel = new_channel;
			temp->first_channel = new_channel;
		}
		file->private_data = (void*)new_channel;
	}
	return 1;
}

static ssize_t device_write( struct file*       file,
		const char __user* buffer,
		size_t             length,
		loff_t*            offset)
{
	int message_length = 0;
	minors_for_current_device* temp;
	channels_for_current_minor* saved_channel;
	int saved_channel_number ;
	int found_channel = 0;
	channels_for_current_minor* current_channel;
	int i;
	char check;
	if(length>128 || length<=0){
		return -EMSGSIZE;
	}
	current_minor_number =  iminor(file->f_path.dentry->d_inode);
	temp = find_current_minor();
	saved_channel = file->private_data;
	if (saved_channel==NULL){
		return -EINVAL;
	}
	saved_channel_number = saved_channel->channel_number;
	found_channel = 0;
	if (temp->first_channel==NULL){
		return -EINVAL;
	}
	current_channel = temp->first_channel;
	while(current_channel!=NULL){
		if (current_channel->channel_number == saved_channel_number){
			found_channel = 1;
			break;
		}
		current_channel = current_channel->next_channel;
	}
	if (found_channel == 0){
		return -EINVAL;
	}
	for( i = 0; i < length; i++ )
	{
		if(get_user(check, &buffer[i])!=0){
			return -EINVAL;
		}
	}
	for( i = 0; i < length; i++ )
	{
		get_user(current_channel->buffer[i], &buffer[i]);
		message_length+=1;
	}
	current_channel->message_length = message_length;
	current_channel->valid_message = 1;
	return message_length;
}

static ssize_t device_read( struct file* file,
		char __user* buffer,
		size_t       length,
		loff_t*      offset )
{
	minors_for_current_device* temp;
	channels_for_current_minor* saved_channel;
	int saved_channel_number ;
	int found_channel;
	channels_for_current_minor* current_channel;
	int i;
	current_minor_number =  iminor(file->f_path.dentry->d_inode);
	temp = find_current_minor();
	saved_channel = file->private_data;
	saved_channel_number = saved_channel->channel_number;
	if (saved_channel==NULL){
		return -EINVAL;
	}
	found_channel = 0;
	if (temp->first_channel==NULL){
		return -EINVAL;
	}
	current_channel = temp->first_channel;
	while(current_channel!=NULL){
		if (current_channel->channel_number == saved_channel_number){
			found_channel = 1;
			break;
		}
		current_channel = current_channel->next_channel;
	}
	if (found_channel == 0){
		return -EINVAL;
	}
	if(current_channel->message_length == 0){
		return -EWOULDBLOCK;
	}
	if(current_channel->valid_message==0){
		return -EINVAL;
	}
	for (i=0;i<current_channel->message_length;i++){
		put_user(current_channel->buffer[i],&buffer[i]);
	}
	return current_channel->message_length;
}

static int device_release( struct inode* inode,
		struct file*  file)
{
	return 1;
}

//==================== DEVICE SETUP =============================

		// This structure will hold the functions to be called
// when a process does something to the device we created
struct file_operations Fops =
{
		.owner			= THIS_MODULE,
		.read           = device_read,
		.write          = device_write,
		.open           = device_open,
		.unlocked_ioctl = device_ioctl,
		.release        = device_release,
};

// Initialize the module - Register the character device
static int __init simple_init(void)
{
	int rc = -1;
	// Register driver capabilities. Obtain major num
	rc = register_chrdev( MAJOR_NUM, DEVICE_FILE_NAME, &Fops );
	// Negative values signify an error
	if( rc < 0 )
	{
		printk( KERN_ERR " registraion failed" );
		return rc;
	}
	printk( "Registeration is successful. ");
	return 0;
}

	static void __exit simple_cleanup(void)
	{
		free_all_minors();
		// Unregister the device
		// Should always succeed
		unregister_chrdev(MAJOR_NUM, DEVICE_FILE_NAME);
	}

	//---------------------------------------------------------------
	module_init(simple_init);
	module_exit(simple_cleanup);

	//========================= END OF FILE =========================
