#include "CH58x_common.h"
#include "ch58x_u8g2.h"
#include "ch58x_soft_I2C.h"
//#include "ch58x_hard_I2C.h"
/* I2C Mode Definition */
#define HOST_MODE     0
#define SLAVE_MODE    1

/* I2C Communication Mode Selection */
#define I2C_MODE      HOST_MODE
//#define I2C_MODE      SLAVE_MODE

/* Global define */
#define SIZE            7
#define MASTER_ADDR     0x42
#define SLAVE_ADDR      0x78

uint8_t u8x8_gpio_and_delay_soft_i2c_ch58x(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
    switch (msg) {
        case U8X8_MSG_DELAY_100NANO: // delay arg_int * 100 nano seconds
            __nop();
            break;
        case U8X8_MSG_DELAY_10MICRO: // delay arg_int * 10 micro seconds
            for (uint16_t n = 0; n < 320; n++)
                __nop();
            break;
        case U8X8_MSG_DELAY_MILLI:   // delay arg_int * 1 milli second
            mDelaymS(1);
            break;
        case U8X8_MSG_DELAY_I2C:     // arg_int is the I2C speed in 100KHz, e.g. 4 = 400 KHz
            mDelayuS(5);
            break;                    // arg_int=1: delay by 5us, arg_int = 4: delay by 1.25us
        case U8X8_MSG_GPIO_I2C_CLOCK: // arg_int=0: Output low at I2C clock pin
            arg_int ? (IIC_SCL_H()) : (IIC_SCL_L());  
            break;                    // arg_int=1: Input dir with pullup high for I2C clock pin
        case U8X8_MSG_GPIO_I2C_DATA:  // arg_int=0: Output low at I2C data pin
            arg_int ? (IIC_SDA_H()) : (IIC_SDA_L());  
            break;                    // arg_int=1: Input dir with pullup high for I2C data pin
        default:
            u8x8_SetGPIOResult(u8x8, 1); // default return value
            break;
    }
    return 1;
}
uint8_t u8x8_gpio_and_delay_hw(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
    switch (msg) {
        case U8X8_MSG_DELAY_100NANO: // delay arg_int * 100 nano seconds
            break;
        case U8X8_MSG_DELAY_10MICRO: // delay arg_int * 10 micro seconds
            break;
        case U8X8_MSG_DELAY_MILLI: // delay arg_int * 1 milli second
            mDelaymS(1);
            break;
        case U8X8_MSG_DELAY_I2C: // arg_int is the I2C speed in 100KHz, e.g. 4 = 400 KHz
            break;                    // arg_int=1: delay by 5us, arg_int = 4: delay by 1.25us
        case U8X8_MSG_GPIO_I2C_CLOCK: // arg_int=0: Output low at I2C clock pin
            break;                    // arg_int=1: Input dir with pullup high for I2C clock pin
        case U8X8_MSG_GPIO_I2C_DATA:  // arg_int=0: Output low at I2C data pin
            break;                    // arg_int=1: Input dir with pullup high for I2C data pin
        case U8X8_MSG_GPIO_MENU_SELECT:
            u8x8_SetGPIOResult(u8x8, /* get menu select pin state */ 0);
            break;
        case U8X8_MSG_GPIO_MENU_NEXT:
            u8x8_SetGPIOResult(u8x8, /* get menu next pin state */ 0);
            break;
        case U8X8_MSG_GPIO_MENU_PREV:
            u8x8_SetGPIOResult(u8x8, /* get menu prev pin state */ 0);
            break;
        case U8X8_MSG_GPIO_MENU_HOME:
            u8x8_SetGPIOResult(u8x8, /* get menu home pin state */ 0);
            break;
        default:
            u8x8_SetGPIOResult(u8x8, 1); // default return value
            break;
    }
    return 1;
}

void HARD_IIC_Init(void)
{
    #ifdef CH58X_I2C_REMAP
    GPIOB_ModeCfg(GPIO_Pin_20 | GPIO_Pin_21, GPIO_ModeIN_PU);
    #else
    GPIOB_ModeCfg(GPIO_Pin_13 | GPIO_Pin_12, GPIO_ModeIN_PU);
    #endif
    I2C_Init(I2C_Mode_I2C, 400000, I2C_DutyCycle_16_9, I2C_Ack_Enable, I2C_AckAddr_7bit, MASTER_ADDR);
}

uint8_t u8x8_byte_hw_i2c(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
    uint8_t* data = (uint8_t*) arg_ptr;
    switch(msg) {
        case U8X8_MSG_BYTE_SEND:
            while( arg_int-- > 0 ) {
                I2C_SendData(*data++);
                while (!I2C_CheckEvent(I2C_EVENT_MASTER_BYTE_TRANSMITTED)) 
                    continue;
            }
            break;
        case U8X8_MSG_BYTE_INIT:
        /* add your custom code to init i2c subsystem */
            HARD_IIC_Init();
            I2C_Cmd(ENABLE);  
            break;
        case U8X8_MSG_BYTE_SET_DC:
        /* ignored for i2c */
            break;
        case U8X8_MSG_BYTE_START_TRANSFER:
            while(I2C_GetFlagStatus(I2C_FLAG_BUSY));
            I2C_GenerateSTART(ENABLE);
            while(!I2C_CheckEvent(I2C_EVENT_MASTER_MODE_SELECT))
                continue;
            I2C_Send7bitAddress(0x78, I2C_Direction_Transmitter);
            while(!I2C_CheckEvent(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED))
                continue;
            break;
        case U8X8_MSG_BYTE_END_TRANSFER:
            I2C_GenerateSTOP(ENABLE);
            break;
        default:
            return 0;
    }
    return 1;
}
