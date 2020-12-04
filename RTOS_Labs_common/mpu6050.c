#include "../RTOS_Labs_common/mpu6050.h"
#include "../inc/I2C0.h"
#include "../RTOS_Labs_common/UART0int.h"


#define MPU6050_I2C_ADDR		0x68

#define RESET_VAL						0x00
#define AFS_SEL							0x00	// AFS_SEL = 0 translates to ± 2g

#define ACCEL_CONFIG_REG		0x1C
#define ACCEL_XOUT_H_REG		0x3B
#define ACCEL_XOUT_L_REG	 	0x3C
#define ACCEL_YOUT_H_REG		0x3D
#define ACCEL_YOUT_L_REG		0x3E
#define ACCEL_ZOUT_H_REG		0x3F
#define ACCEL_ZOUT_L_REG		0x40
#define PWR_MGMT_1_REG			0x6B
#define WHO_AM_I_REG				0x75

#define DEBUG 							0
#define NUM_SAMPLES					1000
#define AFS_SEL_SCALE				16384 

static int16_t x_accel_offset = 0;
static int16_t y_accel_offset = 0;
static int16_t z_accel_offset = 0;


int mpu6050Init(void){
	I2C_Init();
	
	if(DEBUG){
		UART_Init();
		I2C_Send1(MPU6050_I2C_ADDR, WHO_AM_I_REG);
		uint8_t data = I2C_Recv(MPU6050_I2C_ADDR);
		if(data != MPU6050_I2C_ADDR){
			UART_OutString("can't read from device \n");
			return 0;
		}else{
			UART_OutString("successfully read from device \n");
		}
	}
	
	I2C_Send2(MPU6050_I2C_ADDR, ACCEL_CONFIG_REG, AFS_SEL);
	I2C_Send2(MPU6050_I2C_ADDR, PWR_MGMT_1_REG, RESET_VAL);
	
	return 1;
}


void mpu6050ReadAccel(int16_t* xAccel, int16_t* yAccel, int16_t* zAccel){
	I2C_Send1(MPU6050_I2C_ADDR, ACCEL_XOUT_H_REG);	// initiate read from x_out high byte
	int16_t x_accel = I2C_Recv2(MPU6050_I2C_ADDR) + x_accel_offset;
	
	I2C_Send1(MPU6050_I2C_ADDR, ACCEL_YOUT_H_REG);	// initiate read from y_out high byte
	int16_t y_accel = I2C_Recv2(MPU6050_I2C_ADDR) + y_accel_offset;
	
	I2C_Send1(MPU6050_I2C_ADDR, ACCEL_ZOUT_H_REG);	// initiate read from z_out high byte
	int16_t z_accel = I2C_Recv2(MPU6050_I2C_ADDR) + z_accel_offset;
	
	*xAccel = x_accel;
	*yAccel = y_accel;
	*zAccel = z_accel;
	
	if(DEBUG){
		UART_OutString("x_accl: ");
		UART_OutSDec(x_accel);
		UART_OutString(" y_accl: ");
		UART_OutSDec(y_accel);
		UART_OutString(" z_accl: ");
		UART_OutSDec(z_accel);
		UART_OutString("\n");
		
		int delay = 0;
		while(delay < 100000){
			delay++;
		}
	}
	
	/*
	uint16_t difference = 0;
	if(x_accel_single > x_accel_combo){
		difference = x_accel_single - x_accel_combo;
	}else{
		difference = x_accel_combo - x_accel_single;
	}
	
	UART_OutString("difference: ");
	UART_OutUDec(difference);
	UART_OutChar('\n');
	*/
}

void mpu6050Calibration(void){
	
	int16_t x_accel_calibrate = 0;
	int16_t y_accel_calibrate = 0;
	int16_t z_accel_calibrate = 0;
	
	
	long x_accel_aggregate = 0;
	long y_accel_aggregate = 0;
	long z_accel_aggregate = 0;
	
	for(int i = 0; i < NUM_SAMPLES; i++){
		mpu6050ReadAccel(&x_accel_calibrate, 
										 &y_accel_calibrate, 
										 &z_accel_calibrate);
		
		x_accel_aggregate += x_accel_calibrate;
		y_accel_aggregate += y_accel_calibrate;
		z_accel_aggregate += z_accel_calibrate;
	}
	
	x_accel_aggregate /= NUM_SAMPLES;
	y_accel_aggregate /= NUM_SAMPLES;
	z_accel_aggregate /= NUM_SAMPLES;
	
	x_accel_offset =  0 - x_accel_aggregate;
	y_accel_offset =  0 - y_accel_aggregate;
	z_accel_offset =  AFS_SEL_SCALE - z_accel_aggregate;
}

int16_t mpu6050GetXAccelOffset(void){
	return x_accel_offset;
}

int16_t mpu6050GetYAccelOffset(void){
	return y_accel_offset;
}

int16_t mpu6050GetZAccelOffset(void){
	return z_accel_offset;
}

uint16_t mpu6050GetAFS_SELScaleValue(void){
	return AFS_SEL_SCALE;
}

