#include "u8g2/u8g2.h"
#ifndef SDL2
#include "u8g2/port/linux/u8g2arm.h"
#endif
#include <cstdio>
#include <iostream>
u8g2_t u8g2;

int main(void)
{
  int x, y;

  int k;
  int i;
  #ifdef SDL2
  u8g2_SetupBuffer_SDL_128x64_4(&u8g2, &u8g2_cb_r0);
  #endif
  #ifdef LUCKFOX
  u8x8_t *p_u8x8 = u8g2_GetU8x8(&u8g2);
  u8g2_Setup_ssd1306_i2c_128x64_noname_f(&u8g2, U8G2_R0, u8x8_byte_arm_linux_hw_i2c,
                                      u8x8_arm_linux_gpio_and_delay);
                                      u8x8_SetPin(p_u8x8, U8X8_PIN_I2C_CLOCK, U8X8_PIN_NONE);
  u8x8_SetPin(p_u8x8, U8X8_PIN_I2C_DATA, U8X8_PIN_NONE);
  u8x8_SetPin(p_u8x8, U8X8_PIN_RESET, U8X8_PIN_NONE);

  bool success = u8g2arm_arm_init_hw_i2c(p_u8x8, 3); // I2C 3
  if (!success) {
    std::cout << "failed to initialize display" << std::endl;
    return 1;
  }
  #endif
  u8x8_InitDisplay(u8g2_GetU8x8(&u8g2));
  u8x8_SetPowerSave(u8g2_GetU8x8(&u8g2), 0);

  u8g2_SetFont(&u8g2, u8g2_font_6x13_tf);

  x = 50;
  y = 30;

  for (;;)
  {
    u8g2_FirstPage(&u8g2);
    i = 0;
    do
    {
      u8g2_DrawStr(&u8g2, 2, 28, "123");
      i++;

    } while (u8g2_NextPage(&u8g2));
    #ifdef SDL2
    do
    {
      k = u8g_sdl_get_key();
    } while (k < 0);

    if (k == 273)
      y -= 7;
    if (k == 274)
      y += 7;
    if (k == 276)
      x -= 7;
    if (k == 275)
      x += 7;

    if (k == 'e')
      y -= 1;
    if (k == 'x')
      y += 1;
    if (k == 's')
      x -= 1;
    if (k == 'd')
      x += 1;
    if (k == 'q')
      break;
    #endif
  }
  #ifndef SDL2
  u8g2_ClearDisplay(&u8g2);
  #endif
  return 0;
}

