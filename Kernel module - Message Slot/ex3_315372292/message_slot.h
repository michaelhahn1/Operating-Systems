

#ifndef MESSAGE_SLOT_H_
#define MESSAGE_SLOT_H_

#include <linux/ioctl.h>

#define MAJOR_NUM 240
#define DEVICE_FILE_NAME "message_slot"
#define MSG_SLOT_CHANNEL _IOW(MAJOR_NUM,0,unsigned long)


#endif /* MESSAGE_SLOT_H_ */
