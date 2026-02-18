#include "u8g2/u8g2.h"
#ifndef SDL2
#include "u8g2/port/linux/u8g2arm.h"
#endif
#include <cstdio>
#include <iostream>

/* External font support */
#include "u8g2/u8g2_ext_font.h"

u8g2_t u8g2;

uint32_t sd_card_read_cb(void *user_ptr, uint32_t offset,
                        uint8_t *buffer, uint32_t count)
{
    /* user_ptr could be a file handle or SD card context */
    FILE *font_file = (FILE *)user_ptr;

    fseek(font_file, offset, SEEK_SET);
    return fread(buffer, 1, count, font_file);
}

/* Memory-based file read callback for external font demo */
static uint32_t memory_font_read_cb(void *user_ptr, uint32_t offset, uint8_t *buffer, uint32_t count)
{
    //printf("memory_font_read_cb offset=%x,count=%x\n",offset,count);
    const uint8_t *font_data = (const uint8_t *)user_ptr;

    /* Simple bounds checking - in real application, you would know the font size */
    /* For demo, we assume offset + count is within bounds */
    const uint32_t max_size = 2161; /* Approximate size of u8g2_font_6x13_tf */

    if (offset >= max_size) return 0;
    if (offset + count > max_size) count = max_size - offset;

    /* Copy data from memory */
    for (uint32_t i = 0; i < count; i++) {
        buffer[i] = font_data[offset + i];
    }

    return count;
}

int main(void)
{
  int x, y;

  int k;
  int i;
  FILE *font_file;
  font_file = fopen("myfont2.bin", "rb");
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

  /* Demo: Using external font support */
  if(font_file!=NULL){
    std::cout << "Initializing external font..." << std::endl;

    if (u8g2_InitExternalFont(&u8g2, (void *)font_file, sd_card_read_cb)) {
        if (u8g2_SetExternalFont(&u8g2, 0)) {
            std::cout << "External font loaded successfully" << std::endl;
        } else {
            std::cout << "Failed to set external font, using internal font" << std::endl;
            u8g2_SetFont(&u8g2, u8g2_font_6x13_tf);
        }
    } else {
        std::cout << "Failed to initialize external font, using internal font" << std::endl;
        u8g2_SetFont(&u8g2, u8g2_font_6x13_tf);
    }
  }else{
    u8g2_SetFont(&u8g2, u8g2_font_6x13_tf);
  }
  x = 50;
  y = 30;

  for (;;)
  {
    u8g2_FirstPage(&u8g2);
    i = 0;
    do
    {
      u8g2_DrawUTF8(&u8g2, 2, 28, font_file!=NULL?"Hello,Ext Font.":"Hello,inside Font.");
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
  if(font_file!=NULL){
    /* Clean up external font resources */
    u8g2_CleanupExternalFont(&u8g2);
    fclose(font_file);
  }
  return 0;
}

