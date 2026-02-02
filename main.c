#include "stm32f10x.h"                  // Device header
#include "Appcfg.h"
//#include "adc.h"
#include "LCD.h"
#include "stdio.h"                  // Device header


int main(void)
{	     
	SystemInit();	
	freertos_App(); 	
 
}   
