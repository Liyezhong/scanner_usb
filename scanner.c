#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/hidraw.h>
#include <sys/ioctl.h>
#include <stdint.h>

#define  DEVNAME "/dev/hidraw2"

struct scannermsg {
    uchar head[2];
    uchar msg;
    uchar tail;
} __attribute__((packed));

int main(int argc, char** argv)
{
    unsigned char rbuf[1024*1024*5];
    int rxlen;
    int fd;
    int i;
    int ret;
        printf("argc: %d.\n", argc);

    if (argc < 1)
        fd = open(DEVNAME, O_RDWR);
    else
        fd = open(argv[1], O_RDWR);
    if (fd < 0) {
        printf("Can not Open Device.\n");
        exit(1);
    }

    // 获取设备信息，主要是pid,vid
    struct hidraw_devinfo devinfo;

    ret = ioctl(fd, HIDIOCGRAWINFO, &devinfo);

    if (ret < 0)
        perror("HIDIOCGRAWINFO");

    printf("VID = %04x, PID = %04X\n", (unsigned short)devinfo.vendor, (unsigned short)devinfo.product);

    while (1) {
        rxlen = read(fd, rbuf, sizeof(rbuf));
        printf("\nsize: %u\n\n", rxlen);
        for (i = 0; i < rxlen; i++)
             printf(" %02X", rbuf[i]);
        printf("\n");
    }

    close(fd);

    return 0;
}
