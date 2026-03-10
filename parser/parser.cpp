#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// 计算校验和：对data的前len个字节累加，取低8位
static uint8_t calc_checksum(const uint8_t *data, size_t len) {
    uint32_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum += data[i];
    }
    return (uint8_t)(sum & 0xFF);
}

// 从缓冲区读取小端32位整数
static uint32_t read_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// 从缓冲区读取小端16位整数
static uint16_t read_le16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

int parse_packet_file(const char *filename, FontInfo **fonts, uint8_t *font_count,
                      TextInfo **texts, uint8_t *text_count) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        perror("Failed to open file");
        return -1;
    }

    // 获取文件大小
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    rewind(fp);

    // 分配缓冲区并读取整个文件
    uint8_t *buffer = (uint8_t *)malloc(file_size);
    if (!buffer) {
        perror("Out of memory");
        fclose(fp);
        return -1;
    }
    if (fread(buffer, 1, file_size, fp) != (size_t)file_size) {
        perror("Failed to read file");
        free(buffer);
        fclose(fp);
        return -1;
    }
    fclose(fp);

    // 检查文件头
    if (file_size < 2 || buffer[0] != 'D' || buffer[1] != 'G') {
        fprintf(stderr, "Invalid file header (expected 'DG')\n");
        free(buffer);
        return -1;
    }

    // 当前解析位置（跳过文件头）
    uint8_t *p = buffer + 2;
    size_t remaining = file_size - 2;

    // 先解析第一个包（应为字体查找表）
    if (remaining < 7) { // 至少要有包头(6) + 校验(1)
        fprintf(stderr, "File too short\n");
        free(buffer);
        return -1;
    }

    uint8_t total_packets = p[0];
    uint8_t packet_index = p[1];
    uint8_t packet_type = p[2];
    uint32_t content_len = read_le32(p + 3);
    uint32_t packet_len = 7 + content_len + 1; // 包头7 + 内容 + 校验
    //printf("total_packets:%d,packet_index:%d,packet_type:%d,packet_len:%x,remaining:%x\n",total_packets,packet_index,packet_type,packet_len,remaining);
    if (packet_len > remaining) {
        fprintf(stderr, "First packet length exceeds file\n");
        free(buffer);
        return -1;
    }

    // 校验和验证
    uint8_t checksum = calc_checksum(p, packet_len - 1);
    if (checksum != p[packet_len - 1]) {
        fprintf(stderr, "First packet checksum error\n");
        free(buffer);
        return -1;
    }

    if (packet_type != 0x00) {
        fprintf(stderr, "First packet is not font lookup table (type 0x%02X)\n", packet_type);
        free(buffer);
        return -1;
    }

    // 解析字体查找表内容
    uint8_t *content = p + 7;
    uint8_t font_num = content[0];
    // 字体条目数必须至少为0
    if (font_num == 0) {
        fprintf(stderr, "No fonts in lookup table\n");
        free(buffer);
        return -1;
    }

    // 检查内容长度是否足够
    size_t expected_lookup_len = 1 + font_num * (1 + 4) + 4;
    if (content_len != expected_lookup_len) {
        fprintf(stderr, "Lookup table content length mismatch: expected %zu, got %u\n",
                expected_lookup_len, content_len);
        free(buffer);
        return -1;
    }

    // 分配字体数组
    *font_count = font_num;
    *fonts = (FontInfo *)malloc(font_num * sizeof(FontInfo));
    if (!*fonts) {
        perror("Out of memory");
        free(buffer);
        return -1;
    }

    // 读取每个字体信息
    for (int i = 0; i < font_num; i++) {
        uint8_t *entry = content + 1 + i * 5;
        (*fonts)[i].id = entry[0];
        (*fonts)[i].offset = read_le32(entry + 1);
    }

    // 读取最后4字节的其他部分偏移（可选，这里不使用）
    uint32_t other_offset = read_le32(content + 1 + font_num * 5);
    (void)other_offset; // 忽略或可用于验证

    // 更新解析位置到下一个包
    p += packet_len;
    remaining -= packet_len;

    // 初始化文字数组动态列表
    *texts = NULL;
    *text_count = 0;
    int text_capacity = 0;

    // 解析后续包，直到文件结束或达到总包数-1（第一个已处理）
    int packets_processed = 1; // 已经处理了查找表
    while (remaining >= 7 && packets_processed < total_packets) {
        uint8_t *packet_start = p; // 记录包起始位置（用于可能的调试）
        total_packets = p[0];      // 每个包中的总包数应一致，可做检查
        packet_index = p[1];
        packet_type = p[2];
        content_len = read_le32(p + 3);
        packet_len = 7 + content_len + 1;

        if (packet_len > remaining) {
            fprintf(stderr, "Packet %d length exceeds remaining data\n", packets_processed);
            break;
        }

        // 校验和验证
        checksum = calc_checksum(p, packet_len - 1);
        if (checksum != p[packet_len - 1]) {
            fprintf(stderr, "Packet %d (type 0x%02X) checksum error, skipping\n",
                    packets_processed, packet_type);
            // 跳过此包继续
            p += packet_len;
            remaining -= packet_len;
            packets_processed++;
            continue;
        }

        content = p + 7;

        // 根据类别ID处理
        if (packet_type >= 0x01 && packet_type <= 0x0F) {
            (*fonts)[packet_type-1].count = content_len;
            // 字体数据包：可以验证字体序号是否在查找表中，这里忽略
            // 无需存储额外信息
        }
        else if (packet_type >= 0x10 && packet_type <= 0x1F) {
            // 文字数据包
            if (content_len < 3) {
                fprintf(stderr, "Text packet too short (content len %u)\n", content_len);
                p += packet_len;
                remaining -= packet_len;
                packets_processed++;
                continue;
            }

            uint8_t style_font = content[0];
            uint8_t x = content[1];
            uint8_t y = content[2];
            size_t text_len = content_len - 3;

            // 分配TextInfo结构
            if (*text_count >= text_capacity) {
                text_capacity = text_capacity == 0 ? 8 : text_capacity * 2;
                TextInfo *new_texts = (TextInfo *)realloc(*texts, text_capacity * sizeof(TextInfo));
                if (!new_texts) {
                    perror("Out of memory");
                    // 清理已分配的文字和字体
                    for (int i = 0; i < *text_count; i++) {
                        free((*texts)[i].text);
                    }
                    free(*texts);
                    free(*fonts);
                    free(buffer);
                    return -1;
                }
                *texts = new_texts;
            }

            TextInfo *ti = &(*texts)[*text_count];
            ti->category = packet_type;
            ti->style_font = style_font;
            ti->x = x;
            ti->y = y;
            ti->text = (char *)malloc(text_len + 1);
            if (!ti->text) {
                perror("Out of memory");
                // 清理已分配的部分
                for (int i = 0; i < *text_count; i++) {
                    free((*texts)[i].text);
                }
                free(*texts);
                free(*fonts);
                free(buffer);
                return -1;
            }
            memcpy(ti->text, content + 3, text_len);
            ti->text[text_len] = '\0';

            (*text_count)++;
        }
        else {
            // 其他类别ID，忽略
            fprintf(stderr, "Unknown packet type 0x%02X, skipping\n", packet_type);
        }

        p += packet_len;
        remaining -= packet_len;
        packets_processed++;
    }

    // 检查是否处理了所有包
    if (packets_processed != total_packets) {
        fprintf(stderr, "Warning: expected %d packets, processed %d\n",
                total_packets, packets_processed);
    }

    free(buffer);
    return 0;
}