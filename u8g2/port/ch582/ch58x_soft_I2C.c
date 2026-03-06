#include "CH58x_common.h"
#include "ch58x_soft_I2C.h"
/*引脚初始化*/
void IIC_Init(void)
{
    GPIOB_ModeCfg(IIC_SCL_PIN,GPIO_ModeIN_PU );
    GPIOB_ModeCfg(IIC_SDA_PIN,GPIO_ModeIN_PU );
    GPIOB_ResetBits(IIC_SCL_PIN|IIC_SDA_PIN);
    IIC_SCL_H();
    IIC_SDA_H();
}


/**
  * @brief  I2C开始
  * @param  无
  * @retval 无
  */
void IIC_START(void)//开始信号：SCL为高电平时，SDA由高电平转变为低电平
{
    IIC_SDA_H();
    IIC_SCL_H();
    IIC_SDA_L();
    IIC_SCL_L();
}

/**
  * @brief  I2C停止
  * @param  无
  * @retval 无
  */
void IIC_STOP(void)//结束信号：SCL为高电平时，SDA由低电平转变为高电平
{
    IIC_SDA_L();
    IIC_SCL_H();
    IIC_SDA_H();
}
