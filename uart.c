
#include <stdio.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include  <pthread.h>

#include <arpa/inet.h>

#include <sys/ioctl.h>
#include <sys/time.h>

#include "crc/crc/crc16.h"

const char *Serial_Dev = "/dev/ttyACM0";

int fd;

struct image_msg *global_image = NULL;
struct qrcode_msg *global_qrcode = NULL;

int scanner_open(int n)
{
    int fd;
    int ret;
    char scanner_dev[20] = {0};

    sprintf(scanner_dev, "/dev/ttyACM%d", n);
    fd = open(scanner_dev, O_RDWR |  O_NOCTTY);
    if (fd < 0) {
        if (n > 1000)
            return fd;
        return scanner_open(++n);
    }
    return fd;
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

struct scanmsg {
    uint16_t head; // pc: 0xAADD; device: 0xBBDD
#define QRCODE_DATA  1
#define IMAGE_DATA   2
#define QRCODE_MODE  3
#define IMAGE_MODE   4
#define SLEEP        5
#define WAKEUP       6
    uint8_t type;
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

struct image_msg *image_create(uint16_t crc, size_t size)
{
    struct image_msg * msg = (struct image_msg *)malloc(sizeof(struct image_msg));
    memset(msg, '\0', sizeof(struct image_msg));
    msg->pos = msg->image;
    msg->size = size;
    msg->crc = crc;
    printf("image: size: %lu\n", msg->size);
    printf("image: crc: %lu\n", msg->crc);
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

    uint16_t c = crc16(image->image, image->size);
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

int set_opt(int fd,int nSpeed,int nBits,char nEvent,int nStop);

void * Pthread_Serial()
{
    int n=0;
    int ret;
    struct termios oldstdio;
    unsigned char Rx_Data[1024*1024*5];
    unsigned char *rbuf = Rx_Data;

    fd = scanner_open(0);

    if( -1==fd )
        exit(-1);

    ret = set_opt(fd,115200,8,'N',1);
    if(ret == -1)
    {
        exit(-1);
    }

    size_t sum = 0;
	struct timeval tv;

    size_t count = 0;
	fd_set rdfs;

    int flag = fcntl(fd, F_GETFL, 0);
    flag |= O_NONBLOCK;
    fcntl(fd, F_SETFL, flag);
	for (;;) {
		FD_ZERO(&rdfs);
        FD_SET(fd, &rdfs);
        tv.tv_sec  = 3;
        tv.tv_usec = 500000;

		if ((ret = select(fd + 1, &rdfs, NULL, NULL, &tv)) < 0) {
			perror("select");
            break;
		}

        if (ret == 0)
            continue;
        ret = read( fd, rbuf, 1024*1024*5);
        if (FD_ISSET(fd, &rdfs)) {


            sum += len;
            if (0) {
                printf("len = %lu\n", ret);
                int i;
                for (i = 0; i < ret; i++) {
                     printf(" %02X", rbuf[i]);
                }
                printf("\n");
            }


            if (ret >= 3) {
                struct scanmsg *msg = (struct scanmsg *)rbuf;
                msg->head = ntohs(msg->head);
                msg->len  = ntohl(msg->len);
                msg->crc  = ntohs(msg->crc);
                if (msg->head != 0xBBDD) {
                    if (global_image == NULL) {
                        printf("unkonwn msg...\n");
                        continue;
                    } else {
                        image_update(global_image, rbuf, ret);
                        continue;
                    }
                }
                switch (msg->type) {
                case QRCODE_DATA:
                    if (ret < (sizeof(struct scanmsg) + msg->len)) {
                            printf("barcode error, len: %u\n", msg->len);
                            continue;
                    }
                        msg->buffer[msg->len] = '\0';
                        uint16_t c = crc16(msg->buffer, msg->len);
                        printf("barcode crc:　%X, msg_crc: %X\n", c, msg->crc);
                        if (crc16(msg->buffer, msg->len) == msg->crc)
                            printf("barcode: %s\n", msg->buffer);
                        else
                            printf("barcode error, len: %u\n", msg->len);
                    break;
                case QRCODE_MODE:
                        printf("change mode to qrcode.\n");
                    break;
                case IMAGE_DATA:
                    global_image = image_create(msg->crc, msg->len);
#if 0
                    image_update(global_image, msg->buffer, ret - sizeof(struct scanmsg));
                    if (global_image->size <= global_image->act_size)
                        image_save(global_image);
#endif
                    break;
                case IMAGE_MODE:
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

    }
    }
}

int main()
{
    //Create a thread
    Pthread_Serial();

    return 0;
}

int set_opt(int fd,int nSpeed,int nBits,char nEvent,int nStop)
{
    struct termios newtio,oldtio;
    if(tcgetattr(fd,&oldtio)!=0)
    {
        perror("error:SetupSerial 3\n");
        return -1;
    }
    bzero(&newtio,sizeof(newtio));
    //使能串口接收
    newtio.c_cflag |= CLOCAL | CREAD;
    newtio.c_cflag &= ~CSIZE;

    newtio.c_lflag &=~ICANON;//原始模式

    //newtio.c_lflag |=ICANON; //标准模式

    //设置串口数据位
    switch(nBits)
    {
        case 7:
            newtio.c_cflag |= CS7;
            break;
        case 8:
            newtio.c_cflag |=CS8;
            break;
    }
    //设置奇偶校验位
    switch(nEvent)

    {
        case 'O':
            newtio.c_cflag |= PARENB;
            newtio.c_cflag |= PARODD;
            newtio.c_iflag |= (INPCK | ISTRIP);
            break;
        case 'E':
            newtio.c_iflag |= (INPCK | ISTRIP);
            newtio.c_cflag |= PARENB;
            newtio.c_cflag &= ~PARODD;
            break;
        case 'N':
            newtio.c_cflag &=~PARENB;
            break;
    }
    //设置串口波特率
    switch(nSpeed)
    {
        case 2400:
            cfsetispeed(&newtio,B2400);
            cfsetospeed(&newtio,B2400);
            break;
        case 4800:
            cfsetispeed(&newtio,B4800);
            cfsetospeed(&newtio,B4800);
            break;
        case 9600:
            cfsetispeed(&newtio,B9600);
            cfsetospeed(&newtio,B9600);
            break;
        case 115200:
            cfsetispeed(&newtio,B115200);
            cfsetospeed(&newtio,B115200);
            break;
        case 460800:
            cfsetispeed(&newtio,B460800);
            cfsetospeed(&newtio,B460800);
            break;
        default:
            cfsetispeed(&newtio,B9600);
            cfsetospeed(&newtio,B9600);
            break;
    }
    //设置停止位
    if(nStop == 1)
        newtio.c_cflag &= ~CSTOPB;
    else if(nStop == 2)
        newtio.c_cflag |= CSTOPB;
    newtio.c_cc[VTIME] = 1;
    newtio.c_cc[VMIN] = 0;
    tcflush(fd, TCIFLUSH);

    if(tcsetattr(fd,TCSANOW,&newtio)!=0)
    {
        perror("com set error\n");
        return -1;
    }
    return 0;
}
