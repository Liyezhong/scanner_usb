#include <stdio.h>
#include <fstream>
#include <iostream>
#include <libusb-1.0/libusb.h>
#include <sys/types.h>
#include <string.h>

typedef unsigned char byte;
using namespace std;
unsigned char photo_flag = 0;
unsigned char barcode_flag = 0;
byte tmp[32 * 1024 * 1024];

#define min(a, b) ((a) > (b) ? (b) : (a))

void *fun1(void *) {
unsigned char key;
while (1)
{
key = getchar();
if (key == 'b')
{
barcode_flag = 1;
}
if (key == 'p')
{
photo_flag = 1;
}
}
}

int main()
{
	int a = 0;
	int r;
	int actual_length;
	libusb_device_handle *handle;

    pthread_t thread;

    if (pthread_create(&thread, NULL, fun1, NULL) == -1) {
        printf("create error!\n");
		exit(-1);
     }


	r = libusb_init(NULL);
	if (r < 0)
		return r;

	handle = libusb_open_device_with_vid_pid(NULL, 0x0483, 0x5740);
	if (handle == nullptr)
		return -1;

	libusb_detach_kernel_driver(handle, 0);

	r = libusb_set_configuration(handle, 1);
	if (r < 0)
		return r;

	r = libusb_claim_interface(handle, 1);
	if (r < 0)
		return r;

	putchar('\n');

	while (1)
	{
		byte cmd[10] = {0,0,0,0,0,0,0,0,0,0};
		byte ack1[3] = { 0xaa, 0xdd, 0x02 };
		byte ack2[3] = { 0xaa, 0xdd, 0x01 };
		byte barcode_cmd[3] = { 0xaa, 0xdd, 0x03 };
		byte photo_cmd[3] = { 0xaa, 0xdd, 0x04 };
		long len, crc, id, count;
		int ii;
		char filename[20];

		do
		{
			if (photo_flag)
			{
				r = libusb_bulk_transfer(handle, 0x01, photo_cmd, 3, &actual_length, 0);
				photo_flag = 0;
			}
			if (barcode_flag)
			{
				r = libusb_bulk_transfer(handle, 0x01, barcode_cmd, 3, &actual_length, 0);
				barcode_flag = 0;
			}
			r = libusb_bulk_transfer(handle, 0x81, cmd, 3, &actual_length, 500);
			if (r == 0) printf("recv cmd: 0x%.2x 0x%.2x 0x%.2x\n", cmd[0], cmd[1], cmd[2]);
		}
		while ((cmd[2] != 0x02)&& (cmd[2] != 0x01));

		if (cmd[2] == 0x02)
		{
			ofstream fout;
			r = libusb_bulk_transfer(handle, 0x81, cmd + 3, 7, &actual_length, 0);
			len = cmd[3] * 256 * 256 * 256 + cmd[4] * 256 * 256 + cmd[5] * 256 + cmd[6];
			crc = cmd[7] * 256 + cmd[8];
			id = cmd[9];
			count = 0;
			if (r == 0) printf("recv pic header: len = %d bytes, crc = 0x%.4x , id = %d\n", len, crc, id );
			r = libusb_bulk_transfer(handle, 0x01, ack1, 3, &actual_length, 0);
			ii = 0;
			do
			{
				r = libusb_bulk_transfer(handle, 0x81, tmp + count, min(len, 504), &actual_length, 200);
				len = len - actual_length;
				count = count + actual_length;
				ii++;
				//if (r == 0) libusb_bulk_transfer(handle, 0x01, ack1, 3, &actual_length, 0);
			} while (len > 0);//((r == 0));//(actual_length == 512)&&
			if (r == 0) libusb_bulk_transfer(handle, 0x01, ack2, 3, &actual_length, 0);
			sprintf(filename, "%d.jpg", a);
			fout.open(filename, std::ios::binary);
			fout.write((char*)tmp, count);
			fout.close();
			a++;
		}
		if (cmd[2] == 0x01)
		{
			r = libusb_bulk_transfer(handle, 0x81, cmd + 3, 7, &actual_length, 0);
			len = cmd[3] * 256 * 256 * 256 + cmd[4] * 256 * 256 + cmd[5] * 256 + cmd[6];
			crc = cmd[7] * 256 + cmd[8];
			id = cmd[9];
			count = 0;
			if (r == 0) printf("recv barcode header: len = %d bytes, crc = 0x%.4x , id = %d\n", len, crc, id);
			ii = 0;
			do
			{
				r = libusb_bulk_transfer(handle, 0x81, tmp + count, min(len, 504), &actual_length, 200);
				len = len - actual_length;
				count = count + actual_length;
				ii++;
			} while (len > 0);//((r == 0));//(actual_length == 512)&&
			if (r == 0) libusb_bulk_transfer(handle, 0x01, ack2, 3, &actual_length, 0);
			printf("recv barcode:");
			int i = 0;
			for (i = 0; i < count; i++) printf("0x%.2x ", tmp[i]);
			printf("\n");
			a++;
		}
	}

	return 0;
}
