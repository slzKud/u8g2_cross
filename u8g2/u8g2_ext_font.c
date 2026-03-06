/*
  u8g2_ext_font.c

  External font support for u8g2 library
  Allows loading font glyphs from external storage on demand

  Copyright (c) 2023
  All rights reserved.
*/

#include "u8g2_ext_font.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

extern void u8g2_UpdateRefHeight(u8g2_t *u8g2);

/* Forward declarations */
static uint8_t ext_font_read_byte(u8g2_t *u8g2, uint32_t offset);
static uint16_t ext_font_read_word(u8g2_t *u8g2, uint32_t offset);
static uint8_t ext_font_read_data(u8g2_t *u8g2, uint32_t offset, uint8_t *buffer, uint16_t count);
static u8g2_ext_font_context_t *get_ext_font_context(u8g2_t *u8g2);

/*========================================================================*/
/* Helper functions */

static u8g2_ext_font_context_t *get_ext_font_context(u8g2_t *u8g2)
{
#ifdef U8X8_WITH_USER_PTR
    return (u8g2_ext_font_context_t *)u8g2_GetUserPtr(u8g2);
#else
    /* If user_ptr is not available, we need to store context differently */
    /* For simplicity, assume user_ptr is available */
    return (u8g2_ext_font_context_t *)u8g2_GetUserPtr(u8g2);
#endif
}

static uint8_t ext_font_read_byte(u8g2_t *u8g2, uint32_t offset)
{
    u8g2_ext_font_context_t *ctx = get_ext_font_context(u8g2);
    uint8_t data;

    if (ctx == NULL || ctx->read_cb == NULL)
        return 0;

    if (ctx->read_cb(ctx->user_ptr, offset, &data, 1) == 1)
        return data;

    return 0;
}

static uint16_t ext_font_read_word(u8g2_t *u8g2, uint32_t offset)
{
    uint16_t data;
    u8g2_ext_font_context_t *ctx = get_ext_font_context(u8g2);

    if (ctx == NULL || ctx->read_cb == NULL)
        return 0;

    if (ctx->read_cb(ctx->user_ptr, offset, (uint8_t *)&data, 2) == 2)
    {
        /* Convert from big-endian if needed */
        /* u8g2 font data is stored in big-endian for multi-byte values */
        data = (data << 8) | (data >> 8);
        return data;
    }

    return 0;
}

static uint8_t ext_font_read_data(u8g2_t *u8g2, uint32_t offset, uint8_t *buffer, uint16_t count)
{
    u8g2_ext_font_context_t *ctx = get_ext_font_context(u8g2);

    if (ctx == NULL || ctx->read_cb == NULL)
        return 0;

    return (ctx->read_cb(ctx->user_ptr, offset, buffer, count) == count);
}

/*========================================================================*/
/* External font API implementation */

uint8_t u8g2_InitExternalFont(u8g2_t *u8g2, void *user_ptr, u8g2_font_file_read_cb read_cb)
{
    u8g2_ext_font_context_t *ctx;

    /* Allocate context */
    ctx = (u8g2_ext_font_context_t *)malloc(sizeof(u8g2_ext_font_context_t));
    if (ctx == NULL)
        return 0;

    /* Initialize context */
    memset(ctx, 0, sizeof(u8g2_ext_font_context_t));
    ctx->user_ptr = user_ptr;
    ctx->read_cb = read_cb;
    ctx->cached_encoding = 0xFFFF;  /* Invalid encoding */

    /* Store context in user_ptr */
#ifdef U8X8_WITH_USER_PTR
    u8g2_SetUserPtr(u8g2, ctx);
#else
    /* Alternative storage method needed */
    free(ctx);
    return 0;
#endif

    return 1;
}

uint8_t u8g2_SetExternalFont(u8g2_t *u8g2, uint32_t font_data_offset)
{
    u8g2_ext_font_context_t *ctx = get_ext_font_context(u8g2);

    if (ctx == NULL)
        return 0;

    ctx->font_data_offset = font_data_offset;

    /* Load font header */
    return u8g2_LoadFontHeader(u8g2);
}

uint8_t u8g2_LoadFontHeader(u8g2_t *u8g2)
{
    u8g2_ext_font_context_t *ctx = get_ext_font_context(u8g2);
    u8g2_font_info_t *font_info;
    uint32_t base_offset;

    if (ctx == NULL)
        return 0;

    font_info = &ctx->font_info;
    base_offset = ctx->font_data_offset;

    /* Read font header (first 23 bytes) */
    font_info->glyph_cnt = ext_font_read_byte(u8g2, base_offset + 0);
    font_info->bbx_mode = ext_font_read_byte(u8g2, base_offset + 1);
    font_info->bits_per_0 = ext_font_read_byte(u8g2, base_offset + 2);
    font_info->bits_per_1 = ext_font_read_byte(u8g2, base_offset + 3);
    font_info->bits_per_char_width = ext_font_read_byte(u8g2, base_offset + 4);
    font_info->bits_per_char_height = ext_font_read_byte(u8g2, base_offset + 5);
    font_info->bits_per_char_x = ext_font_read_byte(u8g2, base_offset + 6);
    font_info->bits_per_char_y = ext_font_read_byte(u8g2, base_offset + 7);
    font_info->bits_per_delta_x = ext_font_read_byte(u8g2, base_offset + 8);
    font_info->max_char_width = ext_font_read_byte(u8g2, base_offset + 9);
    font_info->max_char_height = ext_font_read_byte(u8g2, base_offset + 10);
    font_info->x_offset = ext_font_read_byte(u8g2, base_offset + 11);
    font_info->y_offset = ext_font_read_byte(u8g2, base_offset + 12);
    font_info->ascent_A = ext_font_read_byte(u8g2, base_offset + 13);
    font_info->descent_g = ext_font_read_byte(u8g2, base_offset + 14);
    font_info->ascent_para = ext_font_read_byte(u8g2, base_offset + 15);
    font_info->descent_para = ext_font_read_byte(u8g2, base_offset + 16);
    font_info->start_pos_upper_A = ext_font_read_word(u8g2, base_offset + 17);
    font_info->start_pos_lower_a = ext_font_read_word(u8g2, base_offset + 19);
#ifdef U8G2_WITH_UNICODE
    font_info->start_pos_unicode = ext_font_read_word(u8g2, base_offset + 21);
#endif

    /* Calculate absolute offsets for ASCII sections */
    ctx->ascii_upper_A_offset = base_offset + 23 + font_info->start_pos_upper_A;
    ctx->ascii_lower_a_offset = base_offset + 23 + font_info->start_pos_lower_a;
#ifdef U8G2_WITH_UNICODE
    ctx->unicode_start_offset = base_offset + 23 + font_info->start_pos_unicode;
#else
    ctx->unicode_start_offset = 0;
#endif

    /* Copy font info to u8g2 structure */
    memcpy(&u8g2->font_info, font_info, sizeof(u8g2_font_info_t));

    /* Set font height mode (same as u8g2_SetFont) */
    u8g2->font_height_mode = U8G2_FONT_HEIGHT_MODE_TEXT;

    /* Update reference height (calculates font_ref_ascent and font_ref_descent) */
    u8g2_UpdateRefHeight(u8g2);

    /* Set font position to baseline (same as u8g2_SetFont would do) */
    //u8g2->font_calc_vref = u8g2_font_calc_vref_font;

    /* Set font pointer to indicate external font mode */
    u8g2->font = U8G2_EXTERNAL_FONT_MARKER;

    /* Reset cached glyph */
    ctx->cached_encoding = 0xFFFF;

    return 1;
}

const uint8_t *u8g2_ext_font_get_glyph_data(u8g2_t *u8g2, uint16_t encoding)
{
    u8g2_ext_font_context_t *ctx = get_ext_font_context(u8g2);
    const uint8_t *result;

    if (ctx == NULL)
        return NULL;

    /* Check if glyph is already cached */
    if (ctx->cached_encoding == encoding)
    {
        /* Return glyph data (skip encoding and size bytes) */
        if (encoding <= 255)
            return ctx->glyph_buffer + 2;  /* ASCII: skip 1 byte encoding + 1 byte size */
        else
            return ctx->glyph_buffer + 3;  /* Unicode: skip 2 bytes encoding + 1 byte size */
    }

    /* Load glyph from file */
    result = u8g2_LoadGlyphFromFile(u8g2, encoding);

    /* Debug output */
    //printf("u8g2_ext_font_get_glyph_data: encoding=0x%04x, result=%p\n", encoding, result);

    return result;
}

const uint8_t *u8g2_LoadGlyphFromFile(u8g2_t *u8g2, uint16_t encoding)
{
    u8g2_ext_font_context_t *ctx = get_ext_font_context(u8g2);
    uint32_t current_offset;
    uint8_t found = 0;

    if (ctx == NULL)
        return NULL;

    /* Clear cache */
    ctx->cached_encoding = 0xFFFF;

    /* Handle ASCII characters (0-255) */
    if (encoding <= 255)
    {
        if (encoding >= 'a')
        {
            current_offset = ctx->ascii_lower_a_offset;
        }
        else if (encoding >= 'A')
        {
            current_offset = ctx->ascii_upper_A_offset;
        }
        else
        {
            /* For control characters and other low ASCII, start from beginning */
            current_offset = ctx->font_data_offset + 23;
        }

        /* Search in ASCII section */
        for (;;)
        {
            uint8_t glyph_size;
            uint8_t current_encoding;

            /* Read encoding and glyph size */
            if (!ext_font_read_data(u8g2, current_offset, ctx->glyph_buffer, 2))
                break;

            current_encoding = ctx->glyph_buffer[0];
            glyph_size = ctx->glyph_buffer[1];

            /* Check for end marker (glyph_size == 0) */
            if (glyph_size == 0)
                break;

            /* Check if this is the glyph we're looking for */
            if (current_encoding == encoding)
            {
                /* Debug output */
                //printf("u8g2_LoadGlyphFromFile: ASCII glyph found, encoding=0x%02x, glyph_size=%d\n", encoding, glyph_size);

                /* Read the rest of glyph data */
                if (glyph_size > 2 && glyph_size <= sizeof(ctx->glyph_buffer))
                {
                    if (ext_font_read_data(u8g2, current_offset + 2, ctx->glyph_buffer + 2, glyph_size - 2))
                    {
                        found = 1;
                        break;
                    }
                }
                else
                {
                    /* Debug: glyph size issue */
                    /* printf("u8g2_LoadGlyphFromFile: glyph_size=%d, buffer_size=%d\n", glyph_size, sizeof(ctx->glyph_buffer)); */
                }
                break;
            }

            /* Move to next glyph */
            current_offset += glyph_size;
        }
    }
#ifdef U8G2_WITH_UNICODE
    else
    {
        /* Unicode character handling */
        /* First, find the Unicode section start */
        current_offset = ctx->unicode_start_offset;

        if (current_offset == 0)
            return NULL;

        /* Skip Unicode lookup table for now (simplified implementation) */
        /* For proper implementation, we should parse the lookup table */

        /* Search through Unicode glyphs */
        for (;;)
        {
            uint16_t current_encoding;
            uint8_t glyph_size;
            
            /* Read encoding (2 bytes) and glyph size (1 byte) */
            if (!ext_font_read_data(u8g2, current_offset, ctx->glyph_buffer, 3))
                break;

            current_encoding = (ctx->glyph_buffer[0] << 8) | ctx->glyph_buffer[1];
            glyph_size = ctx->glyph_buffer[2];

            if(glyph_size==0xff && current_encoding==0x4){
                current_offset += 4; // skip edge protect bytes:0x00 0x04 0xff 0xff
                continue;
            }

            /* Check for end marker (current_encoding == 0) */
            if (current_encoding == 0)
                break;
                
            /* Check if this is the glyph we're looking for */
            if (current_encoding == encoding)
            {
                /* Read the rest of glyph data */
                if (glyph_size > 3 && glyph_size <= sizeof(ctx->glyph_buffer))
                {
                    if (ext_font_read_data(u8g2, current_offset + 3, ctx->glyph_buffer + 3, glyph_size - 3))
                    {
                        found = 1;
                        break;
                    }
                }
                break;
            }

            /* Move to next glyph */
            current_offset += glyph_size;
        }
    }
#endif

    if (found)
    {
        ctx->cached_encoding = encoding;
        /* Return glyph data (skip encoding and size bytes) */
        if (encoding <= 255)
            return ctx->glyph_buffer + 2;  /* ASCII: skip 1 byte encoding + 1 byte size */
        else
            return ctx->glyph_buffer + 3;  /* Unicode: skip 2 bytes encoding + 1 byte size */
    }

    return NULL;
}

void u8g2_CleanupExternalFont(u8g2_t *u8g2)
{
    u8g2_ext_font_context_t *ctx = get_ext_font_context(u8g2);

    if (ctx != NULL)
    {
        free(ctx);
#ifdef U8X8_WITH_USER_PTR
        u8g2_SetUserPtr(u8g2, NULL);
#endif
    }
}

/*========================================================================*/
/* Font index creation utilities */

uint8_t u8g2_CalculateGlyphDataSize(const uint8_t *glyph_data)
{
    /* This function needs to parse the glyph data to calculate its size */
    /* For now, return the size byte from glyph data */
    if (glyph_data == NULL)
        return 0;

    /* Glyph data format: [encoding][size][data...] */
    /* For ASCII: size at offset 1, for Unicode: size at offset 2 */
    /* Simplified: assume ASCII format */
    return glyph_data[1];
}

uint16_t u8g2_CreateFontIndex(const uint8_t *font_data, uint8_t *index_buffer, uint16_t buffer_size)
{
    /* TODO: Implement font index creation */
    /* This would create an index table for faster glyph lookup in external files */
    return 0;
}