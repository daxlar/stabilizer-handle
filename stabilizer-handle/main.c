// Lab6.c
// Runs on LM4F120/TM4C123
// Real Time Operating System for Lab 6

// Jonathan W. Valvano 3/29/17, valvano@mail.utexas.edu
// Andreas Gerstlauer 3/1/16, gerstl@ece.utexas.edu
// EE445M/EE380L.6 
// You may use, edit, run or distribute this file 
// You are free to change the syntax/organization of this file

// LED outputs to logic analyzer for use by OS profile 
// PF1 is preemptive thread switch
// PF2 is first periodic background task (if any)
// PF3 is second periodic background task (if any)
// PC4 is PF4 button touch (SW1 task)

// Outputs for task profiling
// PD0 is idle task
// PD1 is button task

// Button inputs
// PF0 is SW2 task
// PF4 is SW1 button input

// Analog inputs
// PE3 Ain0 sampled at 2kHz, sequencer 3, by Interpreter, using software start

#include <stdint.h>
#include <stdbool.h> 
#include <stdio.h> 
#include <string.h>
#include <stdlib.h>
#include "../inc/tm4c123gh6pm.h"
#include "../inc/CortexM.h"
#include "../inc/LaunchPad.h"
#include "../inc/PLL.h"
#include "../inc/LPF.h"
#include "../RTOS_Labs_common/UART0int.h"
#include "../RTOS_Labs_common/ADC.h"
#include "../RTOS_Labs_common/OS.h"
#include "../RTOS_Labs_common/heap.h"
#include "../RTOS_Labs_common/Interpreter.h"
#include "../RTOS_Labs_common/ST7735.h"
#include "../RTOS_Labs_common/mpu6050.h"
#include "../RTOS_Labs_common/digitalServo.h"



#define PD0  (*((volatile uint32_t *)0x40007004))
#define PD1  (*((volatile uint32_t *)0x40007008))
#define PD2  (*((volatile uint32_t *)0x40007010))
#define PD3  (*((volatile uint32_t *)0x40007020))
	
int NumCreated = 0;
// for compilation purposes
short PID_stm32(short Error, short *Coeff);
short IntTerm;     // accumulated error, RPM-sec
short PrevError;   // previous error, RPM
int serverClientStatus = 0;

void PortD_Init(void){ 
  SYSCTL_RCGCGPIO_R |= 0x08;       // activate port D
  while((SYSCTL_RCGCGPIO_R&0x08)==0){};      
  GPIO_PORTD_DIR_R |= 0x0F;        // make PD3-0 output heartbeats
  GPIO_PORTD_AFSEL_R &= ~0x0F;     // disable alt funct on PD3-0
  GPIO_PORTD_DEN_R |= 0x0F;        // enable digital I/O on PD3-0
  GPIO_PORTD_PCTL_R = ~0x0000FFFF;
  GPIO_PORTD_AMSEL_R &= ~0x0F;;    // disable analog functionality on PD
}

void PortF_Init(void){
	SYSCTL_RCGCGPIO_R |= 0x00000020;  // 1) activate clock for Port F
  while((SYSCTL_PRGPIO_R&0x20) == 0){};// allow time for clock to stabilize
  GPIO_PORTF_LOCK_R = 0x4C4F434B;   // 2) unlock GPIO Port F
  GPIO_PORTF_CR_R = 0x1F;           // allow changes to PF4-0
  // only PF0 needs to be unlocked, other bits can't be locked
  GPIO_PORTF_DIR_R = 0x0E;          // 5) PF4,PF0 in, PF3-1 out
  GPIO_PORTF_PUR_R = 0x11;          // enable pull-up on PF0 and PF4
  GPIO_PORTF_DEN_R = 0x1F;          // 7) enable digital I/O on PF4-0
  GPIO_PORTF_DATA_R = 0;            // LEDs off
}


//------------------Idle Task--------------------------------
// foreground thread, runs when nothing else does
// never blocks, never sleeps, never dies
// inputs:  none
// outputs: none

void Idle(void){     
  while(1) {
    WaitForInterrupt();
  }
}



void MPU6050_test(void){
	
	mpu6050Init();
	
	while(1){
		mpu6050ReadAccel(0, 0, 0);
	}
	
}

void digitalServo_test(void){
	
	StartCritical();
	while(1){
		for(uint16_t pulseLength = 1000; pulseLength <= 3000; pulseLength += 50){
			digitalServoMove(pulseLength);
			int helloDelay = 0;
			while(helloDelay < 500000){
				helloDelay++;
			}
		}
		for(uint16_t pulseLength = 3000; pulseLength >= 1000; pulseLength -= 50){
			digitalServoMove(pulseLength);
			int helloDelay = 0;
			while(helloDelay < 500000){
				helloDelay++;
			}
		}
	}
}

Sema4Type commandSync;

void servoMovementTask(void){
	
	while(1){	
		
		// send servo position to user
		OS_MailBox_Send(digitalServogGetCurrentPulseLength());
		
		OS_bWait(&commandSync);
		
		// wait for new servo command value
		uint32_t servoCommandPulse = OS_MailBox_Recv();
		
		digitalServoMove(servoCommandPulse);
		
		// wait for the servo to move to desired spot
		// seems like this isn't needed
		//OS_Sleep(1);
	}
}

void accelerationFilterTask(void){
	
	int16_t currentXAccelValue = 0;
	int16_t currentYAccelValue = 0;
	int16_t currentZAccelValue = 0;
	
	int16_t XAccelDifference = 0;
	int16_t YAccelDifference = 0;
	int16_t ZAccelDifference = 0;
	
	uint16_t movementDecision = 0;
	uint16_t mpu6050Scale = mpu6050GetAFS_SELScaleValue();
	
	// 16384 is max G value
	// 16384 corresponds to 3000
	// -16384 corresponds to 1000
	// 0 -> 16384 corresponds to 2000 -> 3000
	// divisor of 16, but realistically, it's closer to 4
	
	uint16_t digitalServoPulseLengthRange = digitalServogGetPWM_PULSE_UPPER_BOUND() - digitalServogGetPWM_PULSE_LOWER_BOUND();
	uint16_t movementScale = mpu6050Scale/digitalServoPulseLengthRange;
	//uint16_t digitalServoBasePulseLength = digitalServogGetPWM_PULSE_MIDDLE();
	uint16_t digitalServoBasePulseLength = 0;
	
	while(1){
		
		mpu6050ReadAccel(&currentXAccelValue,
										 &currentYAccelValue,
										 &currentZAccelValue);
		
		/*
		UART_OutString("currentXAccelValue: ");
		UART_OutSDec(currentXAccelValue);
		UART_OutChar('\n');
		*/
		
		// ignore white noise instances
		if(currentXAccelValue <= 400 && currentXAccelValue >= -400){
			continue;
		}
		
		digitalServoBasePulseLength = OS_MailBox_Recv();
		
		//movementDecision = digitalServoBasePulseLength + (currentXAccelValue/movementScale);
		movementDecision = digitalServoBasePulseLength + (currentXAccelValue/8);
		
		if(movementDecision > 3000){
			movementDecision = 3000;
		}
		
		if(movementDecision < 1000){
			movementDecision = 1000;
		}
		
		OS_MailBox_Send(movementDecision);
		OS_bSignal(&commandSync);
	}
}

void mpu6050CalibrationTask(void){
	ST7735_DrawString(0, 0, "calibrating MPU6050", ST7735_CYAN);
	ST7735_DrawString(0, 1, "keep device upright!", ST7735_GREEN);
	
	mpu6050Calibration();
	
	ST7735_DrawString(0, 2, "done calibrating!", ST7735_WHITE);
	ST7735_Message(1, 0, "x_accel_offset:", mpu6050GetXAccelOffset());
	ST7735_Message(1, 1, "y_accel_offset:", mpu6050GetYAccelOffset());
	ST7735_Message(1, 2, "z_accel_offset:", mpu6050GetZAccelOffset());
	
	NumCreated += OS_AddThread(&servoMovementTask, 128, 1);
	NumCreated += OS_AddThread(&accelerationFilterTask, 128, 2);
	
	OS_Kill();
}

int main(void){ // realmain
  OS_Init();        					// initialize, disable interrupts
	//UART_Init();
	
  // hardware init
  //ADC_Init(0);  							// sequencer 3, channel 0, PE3, sampling in Interpreter
	UART_Init();								// for interpreter
	mpu6050Init();							// mpu6050 initialization
	digitalServoInit();					// digitalServo initialization
	ST7735_InitR(INITR_REDTAB); // LCD initialization
	OS_ClearMsTime();						// for waking up sleeping threads	
	
	// software construct init
	OS_MailBox_Init();
	OS_InitSemaphore(&commandSync, 0);

  // create initial foreground threads
  NumCreated = 0;
  NumCreated += OS_AddThread(&mpu6050CalibrationTask, 128, 1);
  NumCreated += OS_AddThread(&Idle,128,5);  // at lowest priority 

 
  OS_Launch(TIME_2MS); // doesn't return, interrupts enabled in here
  return 0;            // this never executes
}

