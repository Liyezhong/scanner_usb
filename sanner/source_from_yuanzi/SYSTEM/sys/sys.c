#include "sys.h"


//////////////////////////////////////////////////////////////////////////////////	 
//本程序只供学习使用，未经作者许可，不得用于其它任何用途
//ALIENTEK STM32F429开发板
//系统时钟初始化	
//包括时钟设置/中断管理/GPIO设置等
//正点原子@ALIENTEK
//技术论坛:www.openedv.com
//创建日期:2016/1/5
//版本：V1.0
//版权所有，盗版必究。
//Copyright(C) 广州市星翼电子科技有限公司 2014-2024
//All rights reserved
//********************************************************************************
//修改说明
//无
////////////////////////////////////////////////////////////////////////////////// 


//uint8_t Sys_Clock_Set(uint32_t plln,uint32_t pllm,uint32_t pllp,uint32_t pllq)
//{ 
//	uint16_t retry=0;
//	uint8_t status=0;
//	RCC->CR|=1<<16;				//HSE 开启 
//	while(((RCC->CR&(1<<17))==0)&&(retry<0X1FFF))retry++;//等待HSE RDY
//	if(retry==0X1FFF)status=1;	//HSE无法就绪
//	else   
//	{
//		RCC->APB1ENR|=1<<28;	//电源接口时钟使能
//		PWR->CR|=3<<14; 		//高性能模式,时钟可到180Mhz
//		RCC->CFGR|=(0<<4)|(5<<10)|(4<<13);//HCLK 不分频;APB1 4分频;APB2 2分频. 
//		RCC->CR&=~(1<<24);	//关闭主PLL
//		RCC->PLLCFGR=pllm|(plln<<6)|(((pllp>>1)-1)<<16)|(pllq<<24)|(1<<22);//配置主PLL,PLL时钟源来自HSE
//		RCC->CR|=1<<24;			//打开主PLL
//		while((RCC->CR&(1<<25))==0);//等待PLL准备好 
//		FLASH->ACR|=1<<8;		//指令预取使能.
//		FLASH->ACR|=1<<9;		//指令cache使能.
//		FLASH->ACR|=1<<10;		//数据cache使能.
//		FLASH->ACR|=5<<0;		//5个CPU等待周期. 
//		RCC->CFGR&=~(3<<0);		//清零
//		RCC->CFGR|=2<<0;		//选择主PLL作为系统时钟	 
//		while((RCC->CFGR&(3<<2))!=(2<<2));//等待主PLL作为系统时钟成功. 
//	} 
//	return status;
//} 

//时钟系统配置函数
//Fvco=Fs*(plln/pllm);
//SYSCLK=Fvco/pllp=Fs*(plln/(pllm*pllp));
//Fusb=Fvco/pllq=Fs*(plln/(pllm*pllq));

//Fvco:VCO频率
//SYSCLK:系统时钟频率
//Fusb:USB,SDIO,RNG等的时钟频率
//Fs:PLL输入时钟频率,可以是HSI,HSE等. 
//plln:主PLL倍频系数(PLL倍频),取值范围:64~432.
//pllm:主PLL和音频PLL分频系数(PLL之前的分频),取值范围:2~63.
//pllp:系统时钟的主PLL分频系数(PLL之后的分频),取值范围:2,4,6,8.(仅限这4个值!)
//pllq:USB/SDIO/随机数产生器等的主PLL分频系数(PLL之后的分频),取值范围:2~15.

//外部晶振为25M的时候,推荐值:plln=360,pllm=25,pllp=2,pllq=8.
//得到:Fvco=25*(360/25)=360Mhz
//     SYSCLK=360/2=180Mhz
//     Fusb=360/8=45Mhz
//返回值:0,成功;1,失败
void Stm32_Clock_Init(uint32_t plln,uint32_t pllm,uint32_t pllp,uint32_t pllq)
{
    HAL_StatusTypeDef ret = HAL_OK;
    RCC_OscInitTypeDef RCC_OscInitStructure; 
    RCC_ClkInitTypeDef RCC_ClkInitStructure;
    
    __HAL_RCC_PWR_CLK_ENABLE(); //使能PWR时钟
    
    //下面这个设置用来设置调压器输出电压级别，以便在器件未以最大频率工作
    //时使性能与功耗实现平衡，此功能只有STM32F42xx和STM32F43xx器件有，
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);//设置调压器输出电压级别1
    
    RCC_OscInitStructure.OscillatorType=RCC_OSCILLATORTYPE_HSE;    //时钟源为HSE
    RCC_OscInitStructure.HSEState=RCC_HSE_ON;                      //打开HSE
    RCC_OscInitStructure.PLL.PLLState=RCC_PLL_ON;//打开PLL
    RCC_OscInitStructure.PLL.PLLSource=RCC_PLLSOURCE_HSE;//PLL时钟源选择HSE
    RCC_OscInitStructure.PLL.PLLM=pllm; //主PLL和音频PLL分频系数(PLL之前的分频),取值范围:2~63.
    RCC_OscInitStructure.PLL.PLLN=plln; //主PLL倍频系数(PLL倍频),取值范围:64~432.  
    RCC_OscInitStructure.PLL.PLLP=pllp; //系统时钟的主PLL分频系数(PLL之后的分频),取值范围:2,4,6,8.(仅限这4个值!)
    RCC_OscInitStructure.PLL.PLLQ=pllq; //USB/SDIO/随机数产生器等的主PLL分频系数(PLL之后的分频),取值范围:2~15.
    ret=HAL_RCC_OscConfig(&RCC_OscInitStructure);//初始化
	
    if(ret!=HAL_OK) while(1);
    
    ret=HAL_PWREx_EnableOverDrive(); //开启Over-Driver功能
    if(ret!=HAL_OK) while(1);
    
    //选中PLL作为系统时钟源并且配置HCLK,PCLK1和PCLK2
    RCC_ClkInitStructure.ClockType=(RCC_CLOCKTYPE_SYSCLK|RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2);
    RCC_ClkInitStructure.SYSCLKSource=RCC_SYSCLKSOURCE_PLLCLK;//设置系统时钟时钟源为PLL
    RCC_ClkInitStructure.AHBCLKDivider=RCC_SYSCLK_DIV1;//AHB分频系数为1
    RCC_ClkInitStructure.APB1CLKDivider=RCC_HCLK_DIV4; //APB1分频系数为4
    RCC_ClkInitStructure.APB2CLKDivider=RCC_HCLK_DIV2; //APB2分频系数为2
    ret=HAL_RCC_ClockConfig(&RCC_ClkInitStructure,FLASH_LATENCY_5);//同时设置FLASH延时周期为5WS，也就是6个CPU周期。
		
    if(ret!=HAL_OK) while(1);

//	RCC->CR|=0x00000001;		//设置HISON,开启内部高速RC振荡
//	RCC->CFGR=0x00000000;		//CFGR清零 
//	RCC->CR&=0xFEF6FFFF;		//HSEON,CSSON,PLLON清零 
//	RCC->PLLCFGR=0x24003010;	//PLLCFGR恢复复位值 
//	RCC->CR&=~(1<<18);			//HSEBYP清零,外部晶振不旁路
//	RCC->CIR=0x00000000;		//禁止RCC时钟中断 
//	Sys_Clock_Set(plln,pllm,pllp,pllq);//设置时钟 
}

#ifdef  USE_FULL_ASSERT
//当编译提示出错的时候此函数用来报告错误的文件和所在行
//file：指向源文件
//line：指向在文件中的行数
void assert_failed(uint8_t* file, uint32_t line)
{ 
	while (1)
	{
	}
}
#endif

//THUMB指令不支持汇编内联
//采用如下方法实现执行汇编指令WFI  
__asm void WFI_SET(void)
{
	WFI;		  
}
//关闭所有中断(但是不包括fault和NMI中断)
__asm void INTX_DISABLE(void)
{
	CPSID   I
	BX      LR	  
}
//开启所有中断
__asm void INTX_ENABLE(void)
{
	CPSIE   I
	BX      LR  
}
//设置栈顶地址
//addr:栈顶地址
__asm void MSR_MSP(u32 addr) 
{
	MSR MSP, r0 			//set Main Stack value
	BX r14
}
