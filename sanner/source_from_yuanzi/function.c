#include "function.h"
#include "main.h"
#include "sys.h"
#include "delay.h"
#include "usart.h"
//#include "led.h"
#include "key.h"
#include "ILI9341.h"
#include "ltdc.h"
#include "sdram.h"
#include "malloc.h"
#include "ov5640.h" 
#include "dcmi.h"  
#include "pcf8574.h" 

#include "usb_device.h"
#include "usbd_customhid.h"

#include "GUI.h"
#include "scannerDLG.h"
#include "uploadDLG.h"
#include "uploadsucessDLG.h"


u32 *jpeg_data_buf;						//JPEG数据缓存buf 
u8 ovx_mode=RGB565_MODE;							//bit0:0,RGB565模式;1,JPEG模式 
volatile u32 jpeg_data_len=0; 			//buf中的JPEG有效数据长度 
volatile u8 jpeg_data_ok=0;				//JPEG数据采集完成标志 
										//0,数据没有采集完;
										//1,数据采集完了,但是还没处理;
										//2,数据已经处理完成了,可以开始下一帧接收
u16 jpeg_current_line=0;							//摄像头输出数据,当前行编号
u16 jpeg_yoffset=0;							//y方向的偏移量



u32 *dcmi_line_buf[2];		//摄像头采用一行一行读取,定义行缓存  
//uint8_t photo_QRcode_mode = QRCODE_MODE; //当前工作模式是拍照还是二维码扫描.开机默认二维码扫描模式.
uint8_t ov5640_flash_light_flag = 0;//照相机闪光灯是否开机参数.


//处理JPEG数据
//当采集完一帧JPEG数据后,调用此函数,切换JPEG BUF.开始下一帧采集.
void jpeg_data_process(void)
{
	u16 i;
	u16 rlen;			//剩余数据长度
	u32 *pbuf;
//	printf("jpeg data ok:%d.\n",jpeg_data_ok); //似乎这一句会影响接收效率;会提示接收到的JPEG数据不完整;
	jpeg_current_line=jpeg_yoffset;	//行数复位
	if(ovx_mode&JPEG_MODE)//只有在JPEG格式下,才需要做处理.
	{
		if(jpeg_data_ok==0)	//jpeg数据还未采集完?
		{
      __HAL_DMA_DISABLE(&DMADMCI_Handler);//关闭DMA
//      while(DMA2_Stream1->CR&0X01);	//等待DMA2_Stream1可配置 
			rlen=jpeg_line_size-__HAL_DMA_GET_COUNTER(&DMADMCI_Handler);//得到剩余数据长度	
			pbuf=jpeg_data_buf+jpeg_data_len;//偏移到有效数据末尾,继续添加
			if(DMADMCI_Handler.Instance->CR&(1<<19))
				for(i=0;i<rlen;i++)
			    pbuf[i]=dcmi_line_buf[1][i];//读取buf1里面的剩余数据
			else 
				for(i=0;i<rlen;i++)
			    pbuf[i]=dcmi_line_buf[0][i];//读取buf0里面的剩余数据 
			jpeg_data_len+=rlen;			//加上剩余长度
			jpeg_data_ok=1; 				//标记JPEG数据采集完按成,等待其他函数处理
			printf("all the jpeg data transfered.\n");
		}
//		if(jpeg_data_ok==2)	//上一次的jpeg数据已经被处理了
//		{
//      __HAL_DMA_SET_COUNTER(&DMADMCI_Handler,jpeg_line_size);	//传输长度为jpeg_buf_size*4字节
//			__HAL_DMA_ENABLE(&DMADMCI_Handler); //打开DMA
//			jpeg_data_ok=0;					    //标记数据未采集
//			jpeg_data_len=0;				    //数据重新开始
//			printf("jpeg data ok = 2.\n");

//		}
	}	
}


//jpeg数据接收回调函数
void jpeg_dcmi_rx_callback(void)
{  
	u16 i;
	u32 *pbuf;
	pbuf=jpeg_data_buf+jpeg_data_len;//偏移到有效数据末尾
	if(DMADMCI_Handler.Instance->CR&(1<<19))//buf0已满,正常处理buf1
	{ 
		for(i=0;i<jpeg_line_size;i++)pbuf[i]=dcmi_line_buf[0][i];//读取buf0里面的数据
		jpeg_data_len+=jpeg_line_size;//偏移
	}else //buf1已满,正常处理buf0
	{
		for(i=0;i<jpeg_line_size;i++)pbuf[i]=dcmi_line_buf[1][i];//读取buf1里面的数据
		jpeg_data_len+=jpeg_line_size;//偏移 
	} 
}

u8 ov5640_jpg_photo()
{
	u8 res=0,headok=0;
	u32 i,jpgstart,jpglen;
	u8* pbuf;
	u8 * ppbuf;
	ovx_mode=JPEG_MODE;
	jpeg_data_ok=0;
//	sw_ov5640_mode();						//切换为OV5640模式 
	OV5640_JPEG_Mode();						//JPEG模式  
	OV5640_OutSize_Set(16,4,2592,1944);		//设置输出尺寸(500W)  
//	OV5640_OutSize_Set(16,4,400,300);
//	OV5640_OutSize_Set(16,4,1000,800);
	dcmi_rx_callback=jpeg_dcmi_rx_callback;	//JPEG接收数据回调函数
	DCMI_DMA_Init((u32)dcmi_line_buf[0],(u32)dcmi_line_buf[1],jpeg_line_size,DMA_MDATAALIGN_WORD,DMA_MINC_ENABLE);//DCMI DMA配置     
	DCMI_Start(); 			//启动传输 
	DCMI->IER |= DCMI_IT_FRAME ; //使能frame中断, 否则hardware fault.
	while(jpeg_data_ok!=1)	//等待第一帧图片采集完
	{delay_ms(100);}
//	jpeg_data_ok=2;			//忽略本帧图片,启动下一帧采集 
//	while(jpeg_data_ok!=1)	//等待第二帧图片采集完,第二帧,才保存到SD卡去. 
//	{delay_ms(100);}
	DCMI_Stop(); 			//停止DMA搬运

		printf("jpeg data size:%d\r\n",jpeg_data_len*4);//串口打印JPEG文件大小
		pbuf=(u8*)jpeg_data_buf;
		ovx_mode=RGB565_MODE; 
		jpglen=0;	//设置jpg文件大小为0
		headok=0;	//清除jpg头标记
		for(i=0;i<jpeg_data_len*4;i++)//查找0XFF,0XD8和0XFF,0XD9,获取jpg文件大小
		{
			if((pbuf[i]==0XFF)&&(pbuf[i+1]==0XD8))//找到FF D8
			{
				jpgstart=i;
				headok=1;	//标记找到jpg头(FF D8)
				printf("found head.\n");
			}
			if((pbuf[i]==0XFF)&&(pbuf[i+1]==0XD9)&&headok)//找到头以后,再找FF D9
			{
				jpglen=i-jpgstart+2;
				printf("found tail.\n");
				break;
			}
		}
		if(jpglen)			//正常的jpeg数据 
		{ 
			Createupload();    //添加正在上传界面
			GUI_Exec();
			pbuf+=jpgstart;	//偏移到0XFF,0XD8处
			/*send by UART begin*/
			for(i=0;i < jpglen;i++)
			{  while((USART1->SR&0X40)==0);//循环发送,直到发送完毕   
					 USART1->DR =pbuf[i] ;		
			}
			/*send by UART end*/
			/*send by USB begin */
//			ppbuf = pbuf;
//      while(jpglen)		
//			{				
////				printf("jpglen=%d.\n",jpglen);//这句打印太影响效率;
//				 
//			   if(jpglen>64)
//			   {   				
//					   USBD_CUSTOM_HID_SendReport(&hUsbDeviceFS,ppbuf,64);
//		         jpglen = jpglen - 64;	
//           	 ppbuf = ppbuf+64;					 
//				 }
//				 else
//				 {
//				     USBD_CUSTOM_HID_SendReport(&hUsbDeviceFS,ppbuf,jpglen);
//					   break;
//				 }
//		  }		
			/*send by USB end*/
      printf("JPEG sent to PC.\n");
      Createuploadsucess();//添加上传成功界面；		
      GUI_Exec();
      HAL_Delay(500);		
//      GUI_Clear();			
		}else 
		{
			res=0XFD;
      printf("incomplete data.\n");			
	  }
		
      __HAL_DMA_SET_COUNTER(&DMADMCI_Handler,jpeg_line_size);	//传输长度为jpeg_buf_size*4字节
			__HAL_DMA_ENABLE(&DMADMCI_Handler); //打开DMA
			jpeg_data_ok=0;					    //标记数据未采集
			jpeg_data_len=0;				    //数据重新开始	
		
//	jpeg_data_len=0;
	OV5640_RGB565_Mode();	//RGB565模式  
	DCMI_DMA_Init((u32)&TFTLCD->LCD_RAM,0,1,DMA_MDATAALIGN_HALFWORD,DMA_MINC_ENABLE);	//DCMI DMA配置,MCU屏,竖屏
	OV5640_OutSize_Set(16,4,lcddev.width,lcddev.height);//结束拍照后屏立马恢复显示.
	return res;
}  

void take_photo()
{
	 printf("picture took.\n"); 
	 DCMI_Stop();//自己加;
   ov5640_jpg_photo();
	 delay_ms(1000);		//等待1秒钟	
	 DCMI_Start();		//这里先使能dcmi,然后立即关闭DCMI,后面再开启DCMI,可以防止RGB屏的侧移问题.
//				DCMI_Stop();	
//				DCMI_Start();//开始显示     
}

void take_photo_init()
{
	  	
		printf("current mode is photo.\n");
	  DCMI_Stop();
//	  atk_qr_destroy();//释放算法内存
	  
	  printf("3SRAM IN:%d\r\n",my_mem_perused(SRAMIN));
	  printf("3SRAM EX:%d\r\n",my_mem_perused(SRAMEX));
	  printf("3SRAM CCM:%d\r\n",my_mem_perused(SRAMCCM)); 	
//		LCD_Clear(YELLOW);						
		dcmi_line_buf[0]=mymalloc(SRAMIN,jpeg_line_size*4);	//为jpeg dma接收申请内存	
		dcmi_line_buf[1]=mymalloc(SRAMIN,jpeg_line_size*4);	//为jpeg dma接收申请内存	
		jpeg_data_buf=mymalloc(SRAMEX,jpeg_buf_size);		//为jpeg文件申请内存(最大4MB)
		DCMI_DMA_Init((u32)&TFTLCD->LCD_RAM,0,1,DMA_MDATAALIGN_HALFWORD,DMA_MINC_ENABLE);			//DCMI DMA配置,MCU屏,竖屏
		jpeg_yoffset=0;
		jpeg_current_line=jpeg_yoffset;		//行数复位
		OV5640_OutSize_Set(16,4,lcddev.width,lcddev.height);	//满屏缩放显示
		DCMI_Start(); 			//启动传输
}

extern UART_HandleTypeDef UART1_Handler; //UART句柄;
extern UART_HandleTypeDef huart5;
extern uint8_t rx_buffer_uart5[BARCODE_MAXIM_BYTE];
extern uint8_t rx_len_uart5;
extern uint8_t recv_end_flag_uart5;
extern char barcode[BARCODE_MAXIM_BYTE];

void scan_barcode()
{
	 uint32_t i;
    
	  if(recv_end_flag_uart5 == 1)		
		{
			 beep_scanning_QR_OK();
			 delay_ms(100);	//延时不够会接收不全;		 
			 HAL_UART_Transmit(&UART1_Handler,rx_buffer_uart5,rx_len_uart5,5000);
       strcpy(barcode,rx_buffer_uart5);
       CreateScanner();
			 GUI_Exec();
//       LCD_ShowString(40,40,600,600,16,&rx_len_uart3);
			 for(i=0;i<BARCODE_MAXIM_BYTE;i++)
			 {
 			   rx_buffer_uart5[i]=0;
			   barcode[i]=0;
		   }
			 recv_end_flag_uart5 = 0;
			 rx_len_uart5 = 0;				 				 
		}
}

void open_camera()
{
	
	my_mem_init(SRAMIN);		//初始化内部内存池
	my_mem_init(SRAMEX);		//初始化外部内存池
	my_mem_init(SRAMCCM);		//初始化CCM内存池    	
	
	DCMI->CR|=DCMI_CR_ENABLE;
//	DCMI->CR|=DCMI_CR_CAPTURE;          //DCMI捕获使能
  take_photo_init();	
	DCMI->IER |=DCMI_IT_FRAME;   
}

void close_camera()
{
  DCMI_Stop();
}

//#define USB_MAXIM_DATA_TO_BE_SEND 64

//void send_JPEG_by_USB(uint32_t length, uint8_t *data_to_send)
//{
//    if(length>USB_MAXIM_DATA_TO_BE_SEND)
//			USBD_CUSTOM_HID_SendReport(&hUsbDeviceFS, "1234567890", 10); 
//}

extern GUI_CONST_STORAGE GUI_BITMAP bmemwin_demo;
extern GUI_CONST_STORAGE GUI_BITMAP bmLeicalogo;

void emwin_demo()
{
				  
	GUI_SetBkColor(GUI_GRAY);		//设置背景颜色
	GUI_Clear();					//清屏
	GUI_SetFont(&GUI_Font32B_ASCII); //设置字体
	GUI_SetColor(GUI_DARKCYAN);       //设置前景色(如文本，画线等颜色)
	GUI_GotoXY(10,10);// Set text position (in pixels)
//	GUI_DispString("Initing... ");  
//	HAL_Delay(1000);
//	GUI_Clear();					//清屏
	
//	GUI_SetFont(&GUI_Font32B_1); //设置字体
//	GUI_SetColor(GUI_DARKRED); 
//	GUI_GotoXY(0,0);// Set text position (in pixels)
//	GUI_DispString("beach I love you so much."); 
//	
//  GUI_DispStringInRectEx("beach I love you so much.",&Rect,GUI_TA_VCENTER | GUI_TA_HCENTER,30,GUI_ROTATE_0);

	GUI_DrawBitmap(&bmLeicalogo,0,0);
	
	HAL_Delay(2000);

	CreateScanner();
	
	GUI_Exec();
	
//	while(1); //debug
}
