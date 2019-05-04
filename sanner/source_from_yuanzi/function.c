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


u32 *jpeg_data_buf;						//JPEG���ݻ���buf 
u8 ovx_mode=RGB565_MODE;							//bit0:0,RGB565ģʽ;1,JPEGģʽ 
volatile u32 jpeg_data_len=0; 			//buf�е�JPEG��Ч���ݳ��� 
volatile u8 jpeg_data_ok=0;				//JPEG���ݲɼ���ɱ�־ 
										//0,����û�вɼ���;
										//1,���ݲɼ�����,���ǻ�û����;
										//2,�����Ѿ����������,���Կ�ʼ��һ֡����
u16 jpeg_current_line=0;							//����ͷ�������,��ǰ�б��
u16 jpeg_yoffset=0;							//y�����ƫ����



u32 *dcmi_line_buf[2];		//����ͷ����һ��һ�ж�ȡ,�����л���  
//uint8_t photo_QRcode_mode = QRCODE_MODE; //��ǰ����ģʽ�����ջ��Ƕ�ά��ɨ��.����Ĭ�϶�ά��ɨ��ģʽ.
uint8_t ov5640_flash_light_flag = 0;//�����������Ƿ񿪻�����.


//����JPEG����
//���ɼ���һ֡JPEG���ݺ�,���ô˺���,�л�JPEG BUF.��ʼ��һ֡�ɼ�.
void jpeg_data_process(void)
{
	u16 i;
	u16 rlen;			//ʣ�����ݳ���
	u32 *pbuf;
//	printf("jpeg data ok:%d.\n",jpeg_data_ok); //�ƺ���һ���Ӱ�����Ч��;����ʾ���յ���JPEG���ݲ�����;
	jpeg_current_line=jpeg_yoffset;	//������λ
	if(ovx_mode&JPEG_MODE)//ֻ����JPEG��ʽ��,����Ҫ������.
	{
		if(jpeg_data_ok==0)	//jpeg���ݻ�δ�ɼ���?
		{
      __HAL_DMA_DISABLE(&DMADMCI_Handler);//�ر�DMA
//      while(DMA2_Stream1->CR&0X01);	//�ȴ�DMA2_Stream1������ 
			rlen=jpeg_line_size-__HAL_DMA_GET_COUNTER(&DMADMCI_Handler);//�õ�ʣ�����ݳ���	
			pbuf=jpeg_data_buf+jpeg_data_len;//ƫ�Ƶ���Ч����ĩβ,�������
			if(DMADMCI_Handler.Instance->CR&(1<<19))
				for(i=0;i<rlen;i++)
			    pbuf[i]=dcmi_line_buf[1][i];//��ȡbuf1�����ʣ������
			else 
				for(i=0;i<rlen;i++)
			    pbuf[i]=dcmi_line_buf[0][i];//��ȡbuf0�����ʣ������ 
			jpeg_data_len+=rlen;			//����ʣ�೤��
			jpeg_data_ok=1; 				//���JPEG���ݲɼ��갴��,�ȴ�������������
			printf("all the jpeg data transfered.\n");
		}
//		if(jpeg_data_ok==2)	//��һ�ε�jpeg�����Ѿ���������
//		{
//      __HAL_DMA_SET_COUNTER(&DMADMCI_Handler,jpeg_line_size);	//���䳤��Ϊjpeg_buf_size*4�ֽ�
//			__HAL_DMA_ENABLE(&DMADMCI_Handler); //��DMA
//			jpeg_data_ok=0;					    //�������δ�ɼ�
//			jpeg_data_len=0;				    //�������¿�ʼ
//			printf("jpeg data ok = 2.\n");

//		}
	}	
}


//jpeg���ݽ��ջص�����
void jpeg_dcmi_rx_callback(void)
{  
	u16 i;
	u32 *pbuf;
	pbuf=jpeg_data_buf+jpeg_data_len;//ƫ�Ƶ���Ч����ĩβ
	if(DMADMCI_Handler.Instance->CR&(1<<19))//buf0����,��������buf1
	{ 
		for(i=0;i<jpeg_line_size;i++)pbuf[i]=dcmi_line_buf[0][i];//��ȡbuf0���������
		jpeg_data_len+=jpeg_line_size;//ƫ��
	}else //buf1����,��������buf0
	{
		for(i=0;i<jpeg_line_size;i++)pbuf[i]=dcmi_line_buf[1][i];//��ȡbuf1���������
		jpeg_data_len+=jpeg_line_size;//ƫ�� 
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
//	sw_ov5640_mode();						//�л�ΪOV5640ģʽ 
	OV5640_JPEG_Mode();						//JPEGģʽ  
	OV5640_OutSize_Set(16,4,2592,1944);		//��������ߴ�(500W)  
//	OV5640_OutSize_Set(16,4,400,300);
//	OV5640_OutSize_Set(16,4,1000,800);
	dcmi_rx_callback=jpeg_dcmi_rx_callback;	//JPEG�������ݻص�����
	DCMI_DMA_Init((u32)dcmi_line_buf[0],(u32)dcmi_line_buf[1],jpeg_line_size,DMA_MDATAALIGN_WORD,DMA_MINC_ENABLE);//DCMI DMA����     
	DCMI_Start(); 			//�������� 
	DCMI->IER |= DCMI_IT_FRAME ; //ʹ��frame�ж�, ����hardware fault.
	while(jpeg_data_ok!=1)	//�ȴ���һ֡ͼƬ�ɼ���
	{delay_ms(100);}
//	jpeg_data_ok=2;			//���Ա�֡ͼƬ,������һ֡�ɼ� 
//	while(jpeg_data_ok!=1)	//�ȴ��ڶ�֡ͼƬ�ɼ���,�ڶ�֡,�ű��浽SD��ȥ. 
//	{delay_ms(100);}
	DCMI_Stop(); 			//ֹͣDMA����

		printf("jpeg data size:%d\r\n",jpeg_data_len*4);//���ڴ�ӡJPEG�ļ���С
		pbuf=(u8*)jpeg_data_buf;
		ovx_mode=RGB565_MODE; 
		jpglen=0;	//����jpg�ļ���СΪ0
		headok=0;	//���jpgͷ���
		for(i=0;i<jpeg_data_len*4;i++)//����0XFF,0XD8��0XFF,0XD9,��ȡjpg�ļ���С
		{
			if((pbuf[i]==0XFF)&&(pbuf[i+1]==0XD8))//�ҵ�FF D8
			{
				jpgstart=i;
				headok=1;	//����ҵ�jpgͷ(FF D8)
				printf("found head.\n");
			}
			if((pbuf[i]==0XFF)&&(pbuf[i+1]==0XD9)&&headok)//�ҵ�ͷ�Ժ�,����FF D9
			{
				jpglen=i-jpgstart+2;
				printf("found tail.\n");
				break;
			}
		}
		if(jpglen)			//������jpeg���� 
		{ 
			Createupload();    //��������ϴ�����
			GUI_Exec();
			pbuf+=jpgstart;	//ƫ�Ƶ�0XFF,0XD8��
			/*send by UART begin*/
			for(i=0;i < jpglen;i++)
			{  while((USART1->SR&0X40)==0);//ѭ������,ֱ���������   
					 USART1->DR =pbuf[i] ;		
			}
			/*send by UART end*/
			/*send by USB begin */
//			ppbuf = pbuf;
//      while(jpglen)		
//			{				
////				printf("jpglen=%d.\n",jpglen);//����ӡ̫Ӱ��Ч��;
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
      Createuploadsucess();//����ϴ��ɹ����棻		
      GUI_Exec();
      HAL_Delay(500);		
//      GUI_Clear();			
		}else 
		{
			res=0XFD;
      printf("incomplete data.\n");			
	  }
		
      __HAL_DMA_SET_COUNTER(&DMADMCI_Handler,jpeg_line_size);	//���䳤��Ϊjpeg_buf_size*4�ֽ�
			__HAL_DMA_ENABLE(&DMADMCI_Handler); //��DMA
			jpeg_data_ok=0;					    //�������δ�ɼ�
			jpeg_data_len=0;				    //�������¿�ʼ	
		
//	jpeg_data_len=0;
	OV5640_RGB565_Mode();	//RGB565ģʽ  
	DCMI_DMA_Init((u32)&TFTLCD->LCD_RAM,0,1,DMA_MDATAALIGN_HALFWORD,DMA_MINC_ENABLE);	//DCMI DMA����,MCU��,����
	OV5640_OutSize_Set(16,4,lcddev.width,lcddev.height);//�������պ�������ָ���ʾ.
	return res;
}  

void take_photo()
{
	 printf("picture took.\n"); 
	 DCMI_Stop();//�Լ���;
   ov5640_jpg_photo();
	 delay_ms(1000);		//�ȴ�1����	
	 DCMI_Start();		//������ʹ��dcmi,Ȼ�������ر�DCMI,�����ٿ���DCMI,���Է�ֹRGB���Ĳ�������.
//				DCMI_Stop();	
//				DCMI_Start();//��ʼ��ʾ     
}

void take_photo_init()
{
	  	
		printf("current mode is photo.\n");
	  DCMI_Stop();
//	  atk_qr_destroy();//�ͷ��㷨�ڴ�
	  
	  printf("3SRAM IN:%d\r\n",my_mem_perused(SRAMIN));
	  printf("3SRAM EX:%d\r\n",my_mem_perused(SRAMEX));
	  printf("3SRAM CCM:%d\r\n",my_mem_perused(SRAMCCM)); 	
//		LCD_Clear(YELLOW);						
		dcmi_line_buf[0]=mymalloc(SRAMIN,jpeg_line_size*4);	//Ϊjpeg dma���������ڴ�	
		dcmi_line_buf[1]=mymalloc(SRAMIN,jpeg_line_size*4);	//Ϊjpeg dma���������ڴ�	
		jpeg_data_buf=mymalloc(SRAMEX,jpeg_buf_size);		//Ϊjpeg�ļ������ڴ�(���4MB)
		DCMI_DMA_Init((u32)&TFTLCD->LCD_RAM,0,1,DMA_MDATAALIGN_HALFWORD,DMA_MINC_ENABLE);			//DCMI DMA����,MCU��,����
		jpeg_yoffset=0;
		jpeg_current_line=jpeg_yoffset;		//������λ
		OV5640_OutSize_Set(16,4,lcddev.width,lcddev.height);	//����������ʾ
		DCMI_Start(); 			//��������
}

extern UART_HandleTypeDef UART1_Handler; //UART���;
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
			 delay_ms(100);	//��ʱ��������ղ�ȫ;		 
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
	
	my_mem_init(SRAMIN);		//��ʼ���ڲ��ڴ��
	my_mem_init(SRAMEX);		//��ʼ���ⲿ�ڴ��
	my_mem_init(SRAMCCM);		//��ʼ��CCM�ڴ��    	
	
	DCMI->CR|=DCMI_CR_ENABLE;
//	DCMI->CR|=DCMI_CR_CAPTURE;          //DCMI����ʹ��
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
				  
	GUI_SetBkColor(GUI_GRAY);		//���ñ�����ɫ
	GUI_Clear();					//����
	GUI_SetFont(&GUI_Font32B_ASCII); //��������
	GUI_SetColor(GUI_DARKCYAN);       //����ǰ��ɫ(���ı������ߵ���ɫ)
	GUI_GotoXY(10,10);// Set text position (in pixels)
//	GUI_DispString("Initing... ");  
//	HAL_Delay(1000);
//	GUI_Clear();					//����
	
//	GUI_SetFont(&GUI_Font32B_1); //��������
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
