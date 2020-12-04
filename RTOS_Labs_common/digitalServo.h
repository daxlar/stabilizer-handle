#ifndef DIGITAL_SERVO
#define DIGITAL_SERVO

#include <stdint.h>


/**
 * @details	initialize the digitalServo unit and PWM0A module
 * @param		void
 * @return	void
 * @brief		init digital servo, PWM0A uses pin PB6
 */
 
void digitalServoInit(void);

/**
 * @details	command the digitalServo to move to a location corresponding to a pulseLength
 * @param		void
 * @return	void
 * @brief		rotates servo to desired position denoted by pulseLength
 */
 
void digitalServoMove(uint16_t pulseLength);

/**
 * @details	getter for currentPulseLength value
 * @param		void
 * @return	void
 * @brief		getter for currentPulseLength value
 */
 
uint16_t digitalServogGetCurrentPulseLength(void);

/**
 * @details	getter for previousPulseLength value
 * @param		void
 * @return	void
 * @brief		getter for previousPulseLength value
 */
 
uint16_t digitalServogGetPreviousPulseLength(void);

/**
 * @details	getter for PWM_PULSE_LOWER_BOUND value
 * @param		void
 * @return	void
 * @brief		getter for PWM_PULSE_LOWER_BOUND value
 */

uint16_t digitalServogGetPWM_PULSE_LOWER_BOUND(void);

/**
 * @details	getter for PWM_PULSE_MIDDLE value
 * @param		void
 * @return	void
 * @brief		getter for PWM_PULSE_MIDDLE value
 */
 
uint16_t digitalServogGetPWM_PULSE_MIDDLE(void);

/**
 * @details	getter for PWM_PULSE_UPPER_BOUND value
 * @param		void
 * @return	void
 * @brief		getter for PWM_PULSE_UPPER_BOUND value
 */
 
uint16_t digitalServogGetPWM_PULSE_UPPER_BOUND(void);

#endif
