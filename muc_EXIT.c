#include "stm32f10x.h"   
#include "muc_EXIT.h"
//#include "muc_LED.h"

void muc_EXIT_Init(void) 
{
  NVIC_InitTypeDef NVIC_InitStructure;
	EXTI_InitTypeDef EXTI_InitStructure;
	
	RCC->APB2ENR |= 0x3D;	               /* Enable GPIOA gpioc GPIOD afio clock            */
	
	GPIOA->CRL&=0XFFFFFFF0;	//PA0设置成输入	  
	GPIOA->CRL|=0X00000008;   
	GPIOC->CRL&=0XFF0FFFFF;	//Pc5设置成输入	  
	GPIOC->CRL|=0X00800000;
	GPIOA->CRH&=0X0FFFFFFF;	//PA15,13设置成输入	  
	GPIOA->CRH|=0X80000000; 
	GPIOC->ODR|=1<<5;	
	GPIOA->ODR|=1<<15;	   	//PA13,15上拉,PA0默认下拉
	
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);/* 配置NVIC为优先级组2 */  
	//SCB->AIRCR=0XFA050500;  //设置分组	  

  NVIC_InitStructure.NVIC_IRQChannel = EXTI0_IRQn;  /* 配置中断源：按键1 */
  NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;  /* 配置抢占优先级 */
  NVIC_InitStructure.NVIC_IRQChannelSubPriority = 2;  /* 配置子优先级 */
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;  /* 使能中断通道 */
  NVIC_Init(&NVIC_InitStructure);      //a0
	//NVIC->ISER0=0x0000 0040;    NVIC->IPR1=0x00A0 0000;
   
	NVIC_InitStructure.NVIC_IRQChannel = EXTI9_5_IRQn; /* 配置中断源：*/  
  NVIC_InitStructure.NVIC_IRQChannelSubPriority =1;  /* 配置子优先级 */
  NVIC_Init(&NVIC_InitStructure);	
	
  NVIC_InitStructure.NVIC_IRQChannel = EXTI15_10_IRQn; /* 配置中断源：按键2，其他使用上面相关配置 */  
  NVIC_InitStructure.NVIC_IRQChannelSubPriority =0;  /* 配置子优先级 */
  NVIC_Init(&NVIC_InitStructure);	
	//NVIC->ISER1=0x0000 0100;			NVIC->IPR10=0x0000 0080;
	
  GPIO_EXTILineConfig(GPIO_PortSourceGPIOA, GPIO_PinSource0); /* 选择EXTI的信号源 */ //wakeup=a0
	//AFIO->EXTICR1=0;
  EXTI_InitStructure.EXTI_Line = EXTI_Line0;	
  EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;	/* EXTI为中断模式 */
  EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;	/* 上升沿中断 */  
  EXTI_InitStructure.EXTI_LineCmd = ENABLE;/* 使能中断 */	
  EXTI_Init(&EXTI_InitStructure);
  
	GPIO_EXTILineConfig(GPIO_PortSourceGPIOC, GPIO_PinSource5); 
  EXTI_InitStructure.EXTI_Line = EXTI_Line5;	
  EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Falling;	/* 下降沿中断 */  
  EXTI_Init(&EXTI_InitStructure);
	
	GPIO_EXTILineConfig(GPIO_PortSourceGPIOA, GPIO_PinSource15); //key1=a15
  EXTI_InitStructure.EXTI_Line = EXTI_Line15;	
  EXTI_Init(&EXTI_InitStructure);
	//EXTI->IMR=0x0000 A001;
}

//void EXTI0_IRQHandler(void)
//{
//  //确保是否产生了EXTI Line中断  wakeup key0
//	if(EXTI_GetITStatus(EXTI_Line0) != RESET) 
//	{	
//		//LED_Off();
//		EXTI_ClearITPendingBit(EXTI_Line0);         //清除中断标志位
//	}  
//}

//void EXTI9_5_IRQHandler(void)
//{
//	//确保是否产生了EXTI Line中断
//		if(EXTI_GetITStatus(EXTI_Line5) != RESET) 
//		{	
//			GPIOA->ODR |= 1<<8;
//			EXTI_ClearITPendingBit(EXTI_Line15);         //清除中断标志位
//		}  
//}
//	
//	
//void EXTI15_10_IRQHandler(void)
//{
//  
//	//确保是否产生了EXTI Line中断 key1
//	if(EXTI_GetITStatus(EXTI_Line15) != RESET) 
//	{	
//		GPIOD->ODR |= 1<<2;
//		EXTI_ClearITPendingBit(EXTI_Line15);         //清除中断标志位
//	}  
//}

