#ifndef PARSER_H
#define PARSER_H
#define USE_LITE_PARSER
#include <stdint.h>

// 字体信息结构体
typedef struct {
    uint8_t id;          // 字体序号 1-15
    uint32_t offset;     // 在文件中的起始偏移（绝对偏移）
    uint32_t count;
} FontInfo;

// 文字信息结构体
typedef struct {
    uint8_t category;    // 类别ID (0x10 或 0x11-0x1F)
    uint8_t style_font;  // 样式/字体序号 (高4位样式，低4位字体)
    uint8_t x;
    uint8_t y;
    char *text;          // UTF-8字符串，以'\0'结尾
} TextInfo;
#ifndef USE_LITE_PARSER
// 解析函数
// 输入: filename 文件名
// 输出: fonts 指向字体数组的指针（动态分配，由调用者free）
//       font_count 字体数量
//       texts 指向文字数组的指针（动态分配，由调用者free）
//       text_count 文字数量
// 返回值: 0 成功, -1 失败
int parse_packet_file(const char *filename, FontInfo **fonts, uint8_t *font_count,
                      TextInfo **texts, uint8_t *text_count);
#endif
int parse_packet_file_lite(void *user_ptr,
                      uint32_t (*read_cb)(void*, uint32_t, uint8_t*, uint32_t),
                      FontInfo **fonts, uint8_t *font_count,
                      TextInfo **texts, uint8_t *text_count);
#endif // PARSER_H