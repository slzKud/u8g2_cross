#ifndef __CH58X_SOFT_I2C__
#include "CH58x_common.h"
#define IIC_SCL_PIN     GPIO_Pin_5//PB5-SCL
#define IIC_SDA_PIN     GPIO_Pin_4//PB4-SDA

#define GPIOB_PinOutput(pin)      (R32_PB_DIR    |=  pin)//配置为输出模式
#define GPIOB_PinInput(pin)       (R32_PB_DIR    &= ~pin)//配置为输入模式

#define IIC_SCL_H()              R32_PB_DIR    &= ~IIC_SCL_PIN
#define IIC_SCL_L()              R32_PB_DIR    |=  IIC_SCL_PIN

#define IIC_SDA_H()              R32_PB_DIR    &= ~IIC_SDA_PIN
#define IIC_SDA_L()              R32_PB_DIR    |=  IIC_SDA_PIN
void IIC_Init();
void IIC_START();
void IIC_STOP();
#define __CH58X_SOFT_I2C__
#endif