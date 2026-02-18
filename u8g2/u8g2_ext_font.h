/*
  u8g2_ext_font.h

  External font support for u8g2 library
  Allows loading font glyphs from external storage on demand

  Copyright (c) 2023
  All rights reserved.
*/

#ifndef U8G2_EXT_FONT_H
#define U8G2_EXT_FONT_H

#include "u8g2.h"

/* Special marker for external font mode */
#define U8G2_EXTERNAL_FONT_MARKER ((const uint8_t *)0xFFFFFFFF)

#ifdef __cplusplus
extern "C" {
#endif

/* File read callback function type */
typedef uint32_t (*u8g2_font_file_read_cb)(void *user_ptr, uint32_t offset, uint8_t *buffer, uint32_t count);

/* External font context structure */
typedef struct u8g2_ext_font_context_t {
    void *user_ptr;                         /* User data pointer passed to read callback */
    u8g2_font_file_read_cb read_cb;         /* File read callback function */
    uint32_t font_data_offset;              /* Offset of font data in the file */

    /* Cached font header information */
    u8g2_font_info_t font_info;

    /* Cached glyph data (single glyph at a time) */
    uint8_t glyph_buffer[64];               /* Buffer for glyph data (adjust size as needed) */
    uint16_t cached_encoding;               /* Encoding of currently cached glyph */

    /* ASCII index cache */
    uint32_t ascii_lower_a_offset;          /* File offset for 'a' */
    uint32_t ascii_upper_A_offset;          /* File offset for 'A' */
    uint32_t unicode_start_offset;          /* File offset for Unicode section */

    /* Unicode index table cache (optional, for faster lookup) */
    uint8_t unicode_index_cached;           /* Flag if Unicode index is cached */
    uint32_t unicode_index_offset;          /* Offset of Unicode index table */
    uint16_t unicode_index_size;            /* Size of Unicode index table in entries */
} u8g2_ext_font_context_t;

/* External font API */

/**
 * Initialize external font context
 * @param u8g2 u8g2 object
 * @param user_ptr User data pointer passed to read callback
 * @param read_cb File read callback function
 * @return 0 on success, non-zero on error
 */
uint8_t u8g2_InitExternalFont(u8g2_t *u8g2, void *user_ptr, u8g2_font_file_read_cb read_cb);

/**
 * Set external font from file
 * @param u8g2 u8g2 object
 * @param font_data_offset Offset of font data in the file
 * @return 0 on success, non-zero on error
 */
uint8_t u8g2_SetExternalFont(u8g2_t *u8g2, uint32_t font_data_offset);

/**
 * Load font header information from external file
 * @param u8g2 u8g2 object
 * @return 0 on success, non-zero on error
 */
uint8_t u8g2_LoadFontHeader(u8g2_t *u8g2);

/**
 * Get glyph data for encoding (replacement for u8g2_font_get_glyph_data)
 * @param u8g2 u8g2 object
 * @param encoding Character encoding
 * @return Pointer to glyph data or NULL if not found
 */
const uint8_t *u8g2_ext_font_get_glyph_data(u8g2_t *u8g2, uint16_t encoding);

/**
 * Load glyph data from external file
 * @param u8g2 u8g2 object
 * @param encoding Character encoding
 * @return Pointer to glyph data or NULL if not found/error
 */
const uint8_t *u8g2_LoadGlyphFromFile(u8g2_t *u8g2, uint16_t encoding);

/**
 * Clean up external font context
 * @param u8g2 u8g2 object
 */
void u8g2_CleanupExternalFont(u8g2_t *u8g2);

/* Utility functions for font file creation */

/**
 * Calculate glyph data size
 * @param glyph_data Pointer to glyph data
 * @return Size of glyph data in bytes
 */
uint8_t u8g2_CalculateGlyphDataSize(const uint8_t *glyph_data);

/**
 * Create external font file index
 * @param font_data Pointer to standard u8g2 font data
 * @param index_buffer Buffer for index data
 * @param buffer_size Size of index buffer
 * @return Size of index data written, or 0 on error
 */
uint16_t u8g2_CreateFontIndex(const uint8_t *font_data, uint8_t *index_buffer, uint16_t buffer_size);

#ifdef __cplusplus
}
#endif

#endif /* U8G2_EXT_FONT_H */