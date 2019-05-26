// author: arthur li

#include <stdio.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include  <pthread.h>

#include <arpa/inet.h>

#include <sys/ioctl.h>
#include <sys/time.h>
#include <signal.h>

#if 0
#include "crc/crc/crc16.h"
#endif

const char *Serial_Dev = "/dev/ttyACM0";

struct image_msg *global_image = NULL;
struct qrcode_msg *global_qrcode = NULL;

struct scannerdev {
	int dev;           // scaner port fd
	int work_mode;     // 0 - scan, 1 - photo
	int busy;          // 0 - idel, 1 - busy
	int light_status;  // 0 - off,  1 - on
	int is_sleep;      // 0 - sleep, 1 - wakeup
};

struct scannermsg {
    uint16_t head; // pc: 0xAADD; device: 0xBBDD
#define QRCODE_DATA  1
#define IMAGE_DATA   2
#define SCAN_MODE    3
#define PHOTO_MODE   4
#define SLEEP        5
#define WAKEUP       6
#define LIGHTON      7
#define LIGHTOFF     8
    uint8_t type;
} __attribute__((packed));

struct scannermsg_ext {
	struct scannermsg head;
    uint8_t id;
    uint32_t len;
    uint16_t crc;
    uint8_t buffer[];
} __attribute__((packed));

struct barcode_msg {
    uint8_t id;
    uint8_t barcode[1024];
    uint32_t size;
};

struct image_msg {
    uint8_t id;
    uint8_t image[1024*1024*5];
    uint16_t crc;
    uint32_t size;
    uint32_t act_size;
    uint8_t *pos;
	struct timeval tv_start;
	struct timeval tv_end;
};

int scanner_open(struct scannerdev *scanner)
{
    int fd;
    char dev[20];

    for (int i = 0; i < 1000; i++) {
        sprintf(dev, "/dev/ttyACM%d", i);
        fd = open(dev, O_RDWR | O_NOCTTY | O_NDELAY);
        printf("fd : %d\n", fd);
		if (fd > 0) {
			scanner->dev = fd;
			return fd;
		}
    }

    return -1;
}

static int scanner_close(struct scannerdev *scanner)
{
	close(scanner->dev);
}

static inline FILE * file_open()
{
	struct timeval tv;
    gettimeofday(&tv, NULL);

    char name[50];
    sprintf(name, "./image_%ld.jpg", tv.tv_sec*1000 + tv.tv_usec);
    FILE *fp = fopen(name, "w+");
    if (fp == NULL)
        perror("fopen");

    return fp;
}

static inline size_t file_write(void *ptr, size_t size, FILE *stream)
{
    return fwrite(ptr, 1, size, stream);
    fflush(stream);
}

void file_close(FILE *stream)
{
    fflush(stream);
    fclose(stream);
}
struct image_msg *image_create(uint16_t crc, size_t size)
{
    struct image_msg * msg = (struct image_msg *)malloc(sizeof(struct image_msg));
    memset(msg, '\0', sizeof(struct image_msg));
    msg->pos = msg->image;
    msg->size = size;
    msg->crc = crc;
    printf("image: size: %u\n", msg->size);
    printf("image: crc: %u\n", msg->crc);
    gettimeofday(&msg->tv_start, NULL);
    return msg;
};

void image_save(struct image_msg *image);
void image_update(struct image_msg *image, uint8_t *buf, size_t len)
{
    if (image == NULL) {
        printf("image NULL\n");
        exit(0);
    }
    memcpy(image->pos, buf, len);
    image->pos += len;
    image->act_size += len;
    if (image->act_size == image->size)
        image_save(image);
    printf("image size: %u, msg_size: %u\n", image->size, image->act_size);
}

int image_crc(struct image_msg *image)
{
    if (image->size != image->act_size) {
        printf("image error\n");
        return -1;
    }

    uint16_t c /*= crc16(image->image, image->size) */;
    printf("image crc:　%X, msg_crc: %X\n", c, image->crc);
    if (image->crc == c)
        return 0;
    return -1;
}

void image_save(struct image_msg *image)
{
    if (image == NULL) {
	printf("image freeed\n");
    	exit(-1);
    }
    if (image->size != image->act_size) {
        printf("image error\n");
        return;
    }
    FILE *fp = file_open();
    if (fp == NULL) {
        printf("image create failed\n");
        free((void *)image);
        return;
    }

    file_write(image->image, image->size, fp);
    free((void *)image);
    file_close(fp);
    global_image = NULL;
    gettimeofday(&image->tv_end, NULL);

    printf("image size: %uk, %u, timestamp: %ld ms \n", image->size /1024, image->size, (image->tv_end.tv_sec * 1000 + image->tv_end.tv_usec / 1000) - (image->tv_start.tv_sec * 1000 + image->tv_start.tv_usec / 1000));
}

/* send message to scanner */
static int scanner_send(struct scannerdev *scanner, void *msg, int len)
{
	return write(scanner->dev, msg, len);
}

/* receive message from scanner */
static int scanner_receive(struct scannerdev *scanner, void *msg, int len)
{
	return read(scanner->dev, msg, len);
}


void scanner_message_handle(struct scannerdev *scanner)
{
    int n=0;
    int ret;
    uint8_t buffer[1024*1024*5];

    size_t sum = 0;
	struct timeval tv;

    size_t count = 0;
	fd_set rdfs;

	for (;;) {
		FD_ZERO(&rdfs);
        FD_SET(scanner->dev, &rdfs);
        tv.tv_sec  = 3;
        tv.tv_usec = 500000;

		if ((ret = select(scanner->dev + 1, &rdfs, NULL, NULL, &tv)) < 0) {
			perror("select");
            break;
		}

        if (ret == 0)
            continue;
        if (FD_ISSET(scanner->dev, &rdfs)) {

		ret = scanner_receive(scanner, buffer, sizeof(buffer));
        if (ret == 0)
            continue;
        if (ret < 0) {
            perror("scanner read");
            continue;
        }

        int len = ret;
        sum += len;
        if (1) {
            printf("len = %d\n", ret);
            int i;
            for (i = 0; i < ret; i++) {
                 printf(" %02X", buffer[i]);
            }
            printf("\n");
        }
        continue;

#if 0
            if (ret >= 3) {
                struct scannermsg *msg = (struct scannermsg *)buffer;
                msg->head = ntohs(msg->head);
                msg->len  = ntohl(msg->len);
                msg->crc  = ntohs(msg->crc);
                if (msg->head != 0xBBDD) {
                    if (global_image == NULL) {
                        printf("unkonwn msg...\n");
                        continue;
                    } else {
                        image_update(global_image, buffer, ret);
                        continue;
                    }
                }
                switch (msg->type) {
                case QRCODE_DATA:
                    if (ret < (sizeof(struct scannermsg) + msg->len)) {
                            printf("barcode error, len: %u\n", msg->len);
                            continue;
                    }
                        msg->buffer[msg->len] = '\0';
                        uint16_t c /*= crc16(msg->buffer, msg->len)*/;
                        printf("barcode crc:　%X, msg_crc: %X\n", c, msg->crc);
                        #if 0
                        if (crc16(msg->buffer, msg->len) == msg->crc)
                            printf("barcode: %s\n", msg->buffer);
                        else
                            printf("barcode error, len: %u\n", msg->len);
                        #endif
                    break;
                case SCAN_MODE:
                        printf("change mode to qrcode.\n");
                    break;
                case IMAGE_DATA:
                    global_image = image_create(msg->crc, msg->len);
#if 0
                    image_update(global_image, msg->buffer, ret - sizeof(struct scannermsg));
                    if (global_image->size <= global_image->act_size)
                        image_save(global_image);
#endif
                    break;
                case PHOTO_MODE:
                        printf("change mode to image.\n");
                    break;
                case SLEEP:
                        printf("scanner sleep.\n");
                    break;
                case WAKEUP:
                        printf("scanner wakup.\n");
                    break;
                default:
                    break;
                }
            }
#endif

    }
    }
}

int scanner_init(struct scannerdev *scanner)
{
    struct termios tty;

    memset(&tty, '\0', sizeof(tty));

    cfsetispeed(&tty, B115200);
    cfsetospeed(&tty, B115200);

    tty.c_cflag |= CLOCAL | CREAD;
    tty.c_lflag &= ~ICANON;
    tty.c_cflag &= ~CSTOPB;
    /*tty.c_cc[VTIME] = 1;*/
    /*tty.c_cc[VMIN] = 0;*/

    cfmakeraw(&tty);

    tcflush(scanner->dev, TCIFLUSH);

    if (tcsetattr(scanner->dev, TCSANOW, &tty)) {
        perror("scanner set TCSANOW error");
        return -1;
    }

    return 0;
}

void signal_handle(int signal_number)
{
    printf("signal: %d\n", signal_number);
}

#define PATTERN_MATCH(a, b) !strncasecmp(a, b, strlen(b))

void *scanner_cmd(void *args)
{
	struct scannerdev *scanner = (struct scannerdev *)args;
	char cmd[100];
    struct scannermsg msg;
    msg.head = ntohs(0xAADD);
    for (;;) {
		fgets(cmd, sizeof(cmd), stdin);
        if (cmd[0] == '\n')
			continue;
		if (PATTERN_MATCH(cmd, "scan")) {
			msg.type = SCAN_MODE;
		} else if (PATTERN_MATCH(cmd, "photo")) {
			msg.type = PHOTO_MODE;
		} else if (PATTERN_MATCH(cmd, "sleep")) {
			msg.type = SLEEP;
		} else if (PATTERN_MATCH(cmd, "wakeup")) {
			msg.type = WAKEUP;
		} else if (PATTERN_MATCH(cmd, "lighton")) {
			msg.type = LIGHTON;
		} else if (PATTERN_MATCH(cmd, "lightoff")) {
			msg.type = LIGHTOFF;
		} else {
			printf("unkonwn cmd: %s\n", cmd);
			continue;
		}

		if (scanner_send(scanner, &msg, sizeof(msg)) < 0) {
			perror("scanner send");
			exit(1);
		}

		printf("cmd: %s \n", cmd);
	}
}

int main()
{
    pthread_t thread;

    int fd;
    signal(SIGHUP, signal_handle);

	struct scannerdev scanner;
	memset(&scanner, '\0', sizeof(scanner));

    if (scanner_open(&scanner) < 0)
        exit(-1);

    if (scanner_init(&scanner) < 0)
        exit(-1);

    if (pthread_create(&thread, NULL, scanner_cmd, &scanner) == -1) {
        printf("create error!\n");
		exit(-1);
     }

    scanner_message_handle(&scanner);

    pthread_kill(thread, SIGKILL);
    pthread_join(thread, NULL);
    scanner_close(&scanner);

    return 0;
}
