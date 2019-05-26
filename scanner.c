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
#include <errno.h>

#if 0
#include "crc/crc/crc16.h"
#endif


struct scannerdev {
	int dev;           // scaner port fd
	int work_mode;     // 0 - scan, 1 - photo
	int busy;          // 0 - idel, 1 - busy
	int light_status;  // 0 - off,  1 - on
	int is_sleep;      // 0 - sleep, 1 - wakeup
#define COMPLETE    (0)
#define INCOMPLETE  (1)
    uint8_t msg_is_incomplete; // for protocol exchange, 0 - complete, 1 - incomplete
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

// for scan and photo
struct scannermsg_ext {
	struct scannermsg head;
    uint32_t len;
    uint16_t crc;
    uint8_t id;
    uint8_t buffer[];
} __attribute__((packed));

struct barcode_msg {
    uint8_t id;
    uint8_t barcode[1024];
    uint16_t crc;
    uint32_t size;
    uint32_t act_size;
};

struct image_msg {
    uint8_t id;
    uint8_t image[1024*1024*5];
    uint16_t crc;
    uint32_t size;
    uint32_t act_size;
	struct timeval tv_start;
	struct timeval tv_end;
};

struct image_msg *current_image = NULL;
struct barcode_msg *current_qrcode = NULL;

static int scanner_open(struct scannerdev *scanner)
{
    int fd;
    char dev[20];

    for (int i = 0; i < 100; i++) {
        sprintf(dev, "/dev/ttyACM%d", i);
        fd = open(dev, O_RDWR | O_NOCTTY | O_NDELAY);
		if (fd > 0) {
			scanner->dev = fd;
            printf("scanner->dev : %d\n", fd);
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

    char name[50] = {'\0'};
    sprintf(name, "./image_%ld.jpg", tv.tv_sec*1000 + tv.tv_usec);
    FILE *fp = fopen(name, "w+");
    if (fp == NULL)
        perror("fopen");

    return fp;
}

static inline size_t file_write(void *ptr, size_t size, FILE *stream)
{
    return fwrite(ptr, 1, size, stream);
}

void file_close(FILE *stream)
{
    fflush(stream);
    fclose(stream);
}

struct image_msg *image_create(uint16_t crc, size_t size)
{
};

void image_save(struct image_msg *image);
void image_update(struct image_msg *image, uint8_t *buf, size_t len)
{
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
        return;
    }

    file_write(image->image, image->size, fp);
    file_close(fp);
    gettimeofday(&image->tv_end, NULL);

    printf("image size: %uk, %u, timestamp: %ld ms \n", image->size /1024, image->size, (image->tv_end.tv_sec * 1000 + image->tv_end.tv_usec / 1000) - (image->tv_start.tv_sec * 1000 + image->tv_start.tv_usec / 1000));
}

/* send message to scanner */
static int scanner_send(struct scannerdev *scanner, void *msg, int len)
{
	int ret;
retry:
    ret = write(scanner->dev, msg, len);
	if ((ret < 0) && ((errno == EAGAIN) || (errno == EINTR)))
		goto retry;
    return ret;
}

/* receive message from scanner */
static int scanner_receive(struct scannerdev *scanner, void *msg, int len)
{
	int ret;
retry:
    ret = read(scanner->dev, msg, len);
	if ((ret < 0) && ((errno == EAGAIN) || (errno == EINTR)))
		goto retry;
    return ret;
}

static void scanner_message_handle(struct scannerdev *scanner)
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

        if (!ret) {
            /*printf("select timeout, retry again\n");*/
            continue;
        }

        if (FD_ISSET(scanner->dev, &rdfs)) {
		ret = scanner_receive(scanner, buffer, sizeof(buffer));
        if (ret == 0)
            continue;
        if (ret < 0) {
            perror("scanner read");
            printf("errno: %d\n", errno);
            continue;
        }

        int len = ret;
        if (1) {
            printf("len = %d\n", len);
            int i;
            for (i = 0; i < len; i++) {
                 printf(" %02X", buffer[i]);
            }
            printf("\n");
        }

        if (scanner->msg_is_incomplete == COMPLETE) {
            struct scannermsg *msg = (struct scannermsg *)buffer;
            struct scannermsg_ext *msg_ext = (struct scannermsg_ext *)buffer;
            msg->head = ntohs(msg->head);
            if ((len != sizeof(struct scannermsg) && len < sizeof(struct scannermsg_ext))
                 || msg->head != 0xBBDD) {
                printf("unkonwn msg...\n");
                int i;
                for (i = 0; i < len; i++) {
                     printf(" %02X", buffer[i]);
                }
                continue;
            }

            switch (msg->type) {
            case QRCODE_DATA:
                scanner->work_mode = 0;
                msg_ext->len  = ntohl(msg_ext->len);
                msg_ext->crc  = ntohs(msg_ext->crc);
                /** message example:
                 *  len = 10
                 *   BB DD 01 00 00 00 0D 0C 50 0D
                 *   len = 13
                 *   39 37 38 37 35 31 31 30 33 34 33 31 31
                 * */
                if (current_qrcode == NULL)
                    current_qrcode = malloc(sizeof(struct barcode_msg));
                current_qrcode->id = msg_ext->id;
                current_qrcode->size = msg_ext->len;
                current_qrcode->act_size = 0;
                current_qrcode->crc = msg_ext->crc;
                current_qrcode->barcode[msg_ext->len] = '\0';
                if (len == sizeof(struct scannermsg_ext)) {
                    scanner->msg_is_incomplete = INCOMPLETE;
                    scanner->busy = 1;
                } else if (len = (sizeof(struct scannermsg_ext) + msg_ext->len)) {
                    scanner->msg_is_incomplete = COMPLETE;
                    scanner->busy = 0;
                    memcpy(current_qrcode->barcode, msg_ext->buffer, msg_ext->len);
                    current_qrcode->act_size = msg_ext->len;
                } else if (len > sizeof(struct scannermsg_ext)
                        && (len < sizeof(struct scannermsg_ext) + msg_ext->len)) {
                    scanner->msg_is_incomplete = INCOMPLETE;
                    scanner->busy = 1;
                    current_qrcode->act_size = len - sizeof(struct scannermsg_ext);
                    memcpy(current_qrcode->barcode, msg_ext->buffer, current_qrcode->act_size);
                } else {
                    printf("message invalid!\n");
                }

                break;
            case IMAGE_DATA:
                scanner->work_mode = 1;
                msg_ext->len  = ntohl(msg_ext->len);
                msg_ext->crc  = ntohs(msg_ext->crc);
                printf("image->len: %u\n", msg_ext->len);
                if (current_image == NULL)
                    current_image = malloc(sizeof(struct image_msg));
                gettimeofday(&current_image->tv_start, NULL);
                current_image->id = msg_ext->id;
                current_image->size = msg_ext->len;
                current_image->act_size = 0;
                current_image->crc = msg_ext->crc;
                if (len == sizeof(struct scannermsg_ext)) {
                    scanner->msg_is_incomplete = INCOMPLETE;
                    scanner->busy = 1;
                } else if (len = (sizeof(struct scannermsg_ext) + msg_ext->len)) {
                    scanner->msg_is_incomplete = COMPLETE;
                    scanner->busy = 0;
                    memcpy(current_image->image, msg_ext->buffer, msg_ext->len);
                    current_image->act_size = msg_ext->len;
                    image_save(current_image);
                } else if (len > sizeof(struct scannermsg_ext)
                        && (len < sizeof(struct scannermsg_ext) + msg_ext->len)) {
                    scanner->msg_is_incomplete = INCOMPLETE;
                    scanner->busy = 1;
                    current_image->act_size = len - sizeof(struct scannermsg_ext);
                    memcpy(current_image->image, msg_ext->buffer, current_image->act_size);
                } else {
                    printf("message invalid!\n");
                }
                break;
            case SCAN_MODE:
                printf("scanner is under scan mode.\n");
                scanner->work_mode = 0;
                break;
            case PHOTO_MODE:
                scanner->work_mode = 1;
                printf("scanner is under photo mode.\n");
                break;
            case SLEEP:
                printf("scanner is sleeping.\n");
                break;
            case WAKEUP:
                printf("scanner is waked up.\n");
                break;
            case LIGHTON:
                printf("scanner light on.\n");
                break;
            case LIGHTOFF:
                printf("scanner light off.\n");
                break;
            default:
                break;
            }
        } else if (scanner->msg_is_incomplete == INCOMPLETE) {
            if (scanner->work_mode == 0) {
            // scan mode
            //    current_qrcode->act_size
            //    current_qrcode->size
            //    len
            //    act_size + len == size
                if (current_qrcode->act_size + len > current_qrcode->size) {
                    printf("invalid message!\n");
                    scanner->msg_is_incomplete = COMPLETE;
                    continue;
                }
                memcpy(current_qrcode->barcode + current_qrcode->act_size, buffer, len);
                current_qrcode->act_size += len;
                if (current_qrcode->act_size == current_qrcode->size) {
                    scanner->msg_is_incomplete = COMPLETE;
                    printf("barcode: %s\n", current_qrcode->barcode);
                }
            } else {
            // photo mode
                if (current_image->act_size + len > current_image->size) {
                    printf("invalid message!\n");
                    scanner->msg_is_incomplete = COMPLETE;
                    continue;
                }
                memcpy(current_image->image + current_image->act_size, buffer, len);
                current_image->act_size += len;
                printf("current_image->act_size: %u\n", current_image->act_size);
                printf("current_image->size: %u\n", current_image->size);
                if (current_image->act_size == current_image->size) {
                    scanner->msg_is_incomplete = COMPLETE;
                    image_save(current_image);
                } else if (current_image->image[current_image->act_size - 1] == 0xD9
                        && current_image->image[current_image->act_size - 2] == 0xFF) {
                    current_image->size = current_image->act_size;
                    scanner->msg_is_incomplete = COMPLETE;
                    image_save(current_image);
                }
            }
        }

#if 0
            if (ret >= 3) {
                struct scannermsg *msg = (struct scannermsg *)buffer;
                msg->head = ntohs(msg->head);
                msg->len  = ntohl(msg->len);
                msg->crc  = ntohs(msg->crc);
                if (msg->head != 0xBBDD) {
                    if (current_image == NULL) {
                        printf("unkonwn msg...\n");
                        continue;
                    } else {
                        image_update(current_image, buffer, ret);
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
                    current_image = image_create(msg->crc, msg->len);
#if 0
                    image_update(current_image, msg->buffer, ret - sizeof(struct scannermsg));
                    if (current_image->size <= current_image->act_size)
                        image_save(current_image);
#endif
                    break;
                }
            }
#endif

    }
    }
}

static int scanner_init(struct scannerdev *scanner)
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

    scanner->msg_is_incomplete = COMPLETE;

    return 0;
}

void signal_handle(int signal_number)
{
    printf("signal: %d\n", signal_number);
}

#define CMD_MATCH(a, b) !strncasecmp(a, b, strlen(b))

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
		if (CMD_MATCH(cmd, "scan")) {
			msg.type = SCAN_MODE;
		} else if (CMD_MATCH(cmd, "photo")) {
			msg.type = PHOTO_MODE;
		} else if (CMD_MATCH(cmd, "sleep")) {
			msg.type = SLEEP;
		} else if (CMD_MATCH(cmd, "wakeup")) {
			msg.type = WAKEUP;
		} else if (CMD_MATCH(cmd, "lighton")) {
			msg.type = LIGHTON;
		} else if (CMD_MATCH(cmd, "lightoff")) {
			msg.type = LIGHTOFF;
		} else {
			printf("unkonwn cmd: %s\n", cmd);
			continue;
		}

		if (scanner_send(scanner, &msg, sizeof(msg)) < 0) {
			perror("scanner send");
			exit(1);
		}

		printf("cmd: %s\n", cmd);
	}
}

int main()
{
    signal(SIGHUP, signal_handle);

	struct scannerdev scanner;
	memset(&scanner, '\0', sizeof(scanner));

    if (scanner_open(&scanner) < 0) {
        printf("scanner open failed!\n");
        exit(-1);
    }

    if (scanner_init(&scanner) < 0)
        exit(-1);

    pthread_t thread;
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
