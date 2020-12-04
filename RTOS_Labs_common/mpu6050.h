#ifndef MPU_6050
#define MPU_6050

#include <stdint.h>

/**
 * @details	initialize the mpu6050 unit to begin reading values
 * @param		void
 * @return	1 if successfully initialized, 0 if not
 * @brief		init mpu6050, I2C uses PB2, PB3
 */

int mpu6050Init(void);

/**
 * @details	read x-axis, y-axis, and z-axis acceleration data from MPU6050
 * @param		xAccel, yAccel, zAccel: signed 16 bit integer pointers to contain accel values
 * @return	void
 * @brief		read accel data from MPU6050
 */

void mpu6050ReadAccel(int16_t* xAccel, int16_t* yAccel, int16_t* zAccel);

/**
 * @details	calibrate the mpu6050 because the values are wonky at bootup
 * @param		void
 * @return	void
 * @brief		calibrate the mpu6050
 */

void mpu6050Calibration(void);

/**
 * @details	return x acceleration offset
 * @param		void
 * @return	void
 * @brief		return x acceleration offset
 */

int16_t mpu6050GetXAccelOffset(void);
	
/**
 * @details	return y acceleration offset
 * @param		void
 * @return	void
 * @brief		return y acceleration offset
 */
 
int16_t mpu6050GetYAccelOffset(void);
	
/**
 * @details	return z acceleration offset
 * @param		void
 * @return	void
 * @brief		return z acceleration offset
 */
 
int16_t mpu6050GetZAccelOffset(void);

/**
 * @details	return AFS_SEL scale value
 * @param		void
 * @return	void
 * @brief		return AFS_SEL scale value
 */

uint16_t mpu6050GetAFS_SELScaleValue(void);
	
#endif
