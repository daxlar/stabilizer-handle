#include "../RTOS_Labs_common/digitalServo.h"
#include "../inc/PWM.h"

/**
	*	internally, PWM0A is set to use a 64 divided system clock
	*	PWM0A clock is (likely) now 80MHz/64 = 1.25MHz
	*	1.25MHz = 0.0000008 s/tick
	*	digital servos have response rate of 300Hz, this is equivalent to PWM period of 300Hz
	*	ticks for a period = (1/300 s)/ (0.0000008 s\tick) = 4,166.66 ticks
	* ticks for 200Hz period = (1/200 s)/ (0.0000008 s\tick) = 6250 ticks
	* servos have operational pulse widths between 1ms and 2ms
	* 1 ms worth of ticks = (1/1000 s)/ (0.0000008 s\tick) = 1250 ticks
	* 2 ms worth of ticks = (1/2000 s)/ (0.0000008 s\tick) = 2500 ticks
	
	* weird results.... tick range of 3000 to 600 seemed to be nearly full range
	* 3000 -> 2.4ms, 600 -> 0.48 ms
	*/
	
#define PWM_PERIOD 						4200
#define PWM_PULSE_LOWER_BOUND	1000
#define PWM_PULSE_UPPER_BOUND	3000
#define PWM_PULSE_MIDDLE			(PWM_PULSE_UPPER_BOUND + PWM_PULSE_LOWER_BOUND)/2


static uint16_t currentPulseLength = 0;
static uint16_t previousPulseLength = 0;

void digitalServoInit(void){
	
	uint16_t pwm_period = PWM_PERIOD;
	uint16_t pulseLength = PWM_PULSE_MIDDLE;
	PWM0A_Init(pwm_period, pulseLength);
	currentPulseLength = pulseLength;
}

void digitalServoMove(uint16_t pulseLength){
	previousPulseLength = currentPulseLength;
	currentPulseLength = pulseLength;
	PWM0A_Duty(currentPulseLength);
}

uint16_t digitalServogGetCurrentPulseLength(void){
	return currentPulseLength;
}

uint16_t digitalServogGetPreviousPulseLength(void){
	return previousPulseLength;
}

uint16_t digitalServogGetPWM_PULSE_LOWER_BOUND(void){
	return PWM_PULSE_LOWER_BOUND;
}

uint16_t digitalServogGetPWM_PULSE_MIDDLE(void){
	return PWM_PULSE_MIDDLE;
}

uint16_t digitalServogGetPWM_PULSE_UPPER_BOUND(void){
	return PWM_PULSE_UPPER_BOUND;
}

