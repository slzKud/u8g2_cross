#ifndef _CH58X_U8G2_
#include "../../u8g2.h"
#include "../../u8g2_ext_font.h"
#define CH58X_I2C_REMAP
uint8_t u8x8_gpio_and_delay_soft_i2c_ch58x(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr);
uint8_t u8x8_gpio_and_delay_hw(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr);
uint8_t u8x8_byte_hw_i2c(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr);
#define _CH58X_U8G2_
#endif