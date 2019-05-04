#ifndef _FUNCTION_H_
#define _FUNCTION_H_

#include "sys.h"
#include "delay.h"
#include "usart.h"

#define BARCODE_MAXIM_BYTE 512

#define jpeg_buf_size   4*1024*1024		//定义JPEG数据缓存jpeg_buf的大小(4M字节)
#define jpeg_line_size	2*1024			//定义DMA接收数据时,一行数据的最大值
#define RGB565_MODE 0
#define JPEG_MODE 1

void jpeg_data_process(void);
void jpeg_dcmi_rx_callback(void);
u8 ov5640_jpg_photo(void);
void take_photo(void);
void take_photo_init(void);
void scan_barcode(void);
void open_camera(void);
void close_camera(void);
void emwin_demo(void);



#endif
