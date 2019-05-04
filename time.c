#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <arpa/inet.h>

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


int main()
{
    char *buf = "\xaa\xbb\x01\x01\x00\x00\x00\x04\x00\x00\x31\x32\x33\x34";
    //unsigned int a = htonl(*(unsigned int *)buf);
    //printf("%X\n", a);

    struct scanmsg *msg = (struct scanmsg *)buf;
    printf("barcode: %s\n", msg->buffer);
    printf("head %X\n", ntohs(msg->head));
    printf("len %u\n", ntohl(msg->len));

    bool b = true;

    exit(0);
	struct timeval tv;
	struct timezone tz;

	struct timeval ts;
	int i = 0;

	gettimeofday(&tv, &tz);
	printf("tv_sec %ld\n", tv.tv_sec);
	printf("tv_usec %ld\n", tv.tv_usec);
	printf("tz_minuteswest %d\n", tz.tz_minuteswest);
	printf("tz_dsttime %d\n",tz.tz_dsttime);

	gettimeofday(&tv, &tz);
	printf("%ld.%ld \n", tv.tv_sec, tv.tv_usec / 1000);

	//timestamp
	gettimeofday(&tv, NULL);
	printf("%ld.%ld \n", tv.tv_sec, tv.tv_usec / 1000);
	usleep(30000);
	gettimeofday(&ts, NULL);
	printf("%ld.%ld \n", ts.tv_sec, ts.tv_usec / 1000);
	printf("timestamp: %ld ms \n", (ts.tv_sec * 1000 + ts.tv_usec / 1000) - (tv.tv_sec * 1000 + tv.tv_usec / 1000));

	return 0;
}
