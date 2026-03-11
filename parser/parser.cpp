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
#ifndef USE_LITE_PARSER
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
#endif
// 安全读取辅助函数：确保读取指定字节数，并更新偏移
static int safe_read(void *user_ptr,
                     uint32_t (*read_cb)(void*, uint32_t, uint8_t*, uint32_t),
                     uint32_t *offset, uint8_t *buffer, uint32_t count) {
    if (read_cb(user_ptr, *offset, buffer, count) != count) {
        return -1;  // 读取失败或不足
    }
    *offset += count;
    return 0;
}

// 跳过指定字节数（用于错误恢复或丢弃数据）
static int skip_bytes(void *user_ptr,
                      uint32_t (*read_cb)(void*, uint32_t, uint8_t*, uint32_t),
                      uint32_t *offset, uint32_t count) {
    uint8_t tmp[256];
    while (count > 0) {
        uint32_t chunk = (count < sizeof(tmp)) ? count : sizeof(tmp);
        if (safe_read(user_ptr, read_cb, offset, tmp, chunk) != 0) {
            return -1;
        }
        count -= chunk;
    }
    return 0;
}

int parse_packet_file_lite(void *user_ptr,
                      uint32_t (*read_cb)(void*, uint32_t, uint8_t*, uint32_t),
                      FontInfo **fonts, uint8_t *font_count,
                      TextInfo **texts, uint8_t *text_count) {
    uint32_t offset = 0;                 // 当前读取偏移
    uint8_t header[7];                   // 包头部缓冲区
    uint8_t first_total_packets = 0;      // 第一个包的总包数，用于后续验证
    int fonts_allocated = 0;              // 标记字体数组是否已分配
    int texts_capacity = 0;                // 当前文字数组容量
    int packets_processed = 0;             // 已处理包数

    // 初始化输出指针
    *fonts = NULL;
    *font_count = 0;
    *texts = NULL;
    *text_count = 0;

    // 1. 读取并验证文件头 "DG"
    uint8_t file_header[2];
    if (safe_read(user_ptr, read_cb, &offset, file_header, 2) != 0) {
        fprintf(stderr, "Failed to read file header\n");
        return -1;
    }
    if (file_header[0] != 'D' || file_header[1] != 'G') {
        fprintf(stderr, "Invalid file header (expected 'DG')\n");
        return -1;
    }

    // 2. 读取第一个包（应为字体查找表）
    // 读取包头
    if (safe_read(user_ptr, read_cb, &offset, header, 7) != 0) {
        fprintf(stderr, "Failed to read first packet header\n");
        return -1;
    }

    first_total_packets = header[0];
    uint8_t packet_index = header[1];
    uint8_t packet_type = header[2];
    uint32_t content_len = read_le32(header + 3);

    // 校验和初始化（先累加包头）
    uint32_t checksum = 0;
    for (int i = 0; i < 7; i++) checksum += header[i];

    // 检查类型是否为字体查找表
    if (packet_type != 0x00) {
        fprintf(stderr, "First packet is not font lookup table (type 0x%02X)\n", packet_type);
        // 跳过整个包并返回错误
        skip_bytes(user_ptr, read_cb, &offset, content_len + 1);
        return -1;
    }

    // 读取字体查找表内容
    uint8_t *content = (uint8_t *)malloc(content_len);
    if (!content) {
        perror("Out of memory");
        return -1;
    }
    if (safe_read(user_ptr, read_cb, &offset, content, content_len) != 0) {
        fprintf(stderr, "Failed to read font lookup table content\n");
        free(content);
        return -1;
    }
    // 累加内容到校验和
    for (uint32_t i = 0; i < content_len; i++) checksum += content[i];

    // 读取校验和字节
    uint8_t file_checksum;
    if (safe_read(user_ptr, read_cb, &offset, &file_checksum, 1) != 0) {
        fprintf(stderr, "Failed to read first packet checksum\n");
        free(content);
        return -1;
    }
    // 验证校验和
    if ((uint8_t)(checksum & 0xFF) != file_checksum) {
        fprintf(stderr, "First packet checksum error\n");
        free(content);
        return -1;
    }
    packets_processed++;

    // 解析字体查找表内容
    uint8_t font_num = content[0];
    if (font_num == 0) {
        fprintf(stderr, "No fonts in lookup table\n");
        free(content);
        return -1;
    }
    // 检查内容长度是否匹配
    size_t expected_lookup_len = 1 + font_num * (1 + 4) + 4;
    if (content_len != expected_lookup_len) {
        fprintf(stderr, "Lookup table content length mismatch: expected %zu, got %u\n",
                expected_lookup_len, content_len);
        free(content);
        return -1;
    }

    // 分配字体数组
    *font_count = font_num;
    *fonts = (FontInfo *)malloc(font_num * sizeof(FontInfo));
    if (!*fonts) {
        perror("Out of memory");
        free(content);
        return -1;
    }
    fonts_allocated = 1;
    // 初始化所有字体的count为0
    for (int i = 0; i < font_num; i++) {
        (*fonts)[i].count = 0;
    }

    // 填充每个字体信息
    for (int i = 0; i < font_num; i++) {
        uint8_t *entry = content + 1 + i * 5;
        (*fonts)[i].id = entry[0];
        (*fonts)[i].offset = read_le32(entry + 1);
    }
    // 读取最后的other_offset（可选，忽略）
    // uint32_t other_offset = read_le32(content + 1 + font_num * 5);

    free(content);  // 释放字体查找表内容

    // 3. 处理后续数据包
    while (1) {
        // 尝试读取下一个包头，如果文件结束则跳出
        if (safe_read(user_ptr, read_cb, &offset, header, 7) != 0) {
            // 没有更多数据，正常结束
            break;
        }

        uint8_t total_packets = header[0];
        packet_index = header[1];
        packet_type = header[2];
        content_len = read_le32(header + 3);

        // 验证总包数一致性
        if (total_packets != first_total_packets) {
            fprintf(stderr, "Warning: packet total mismatch (%d vs %d) at index %d\n",
                    total_packets, first_total_packets, packet_index);
        }

        // 重新初始化校验和
        checksum = 0;
        for (int i = 0; i < 7; i++) checksum += header[i];

        // 根据类型处理
        if (packet_type >= 0x01 && packet_type <= 0x0F) {
            // 字体数据包：只需要读取内容更新校验和，并记录字体大小
            uint8_t font_idx = packet_type - 1;  // 假定字体ID从1开始连续
            if (font_idx >= font_num) {
                fprintf(stderr, "Font data packet with invalid font ID %d, skipping\n", packet_type);
                // 跳过内容及校验和
                if (skip_bytes(user_ptr, read_cb, &offset, content_len + 1) != 0) {
                    goto error_cleanup;
                }
                packets_processed++;
                continue;
            }

            // 读取内容并更新校验和（不需要保存）
            uint32_t remaining = content_len;
            uint8_t tmp[128];
            while (remaining > 0) {
                uint32_t chunk = (remaining < sizeof(tmp)) ? remaining : sizeof(tmp);
                if (safe_read(user_ptr, read_cb, &offset, tmp, chunk) != 0) {
                    fprintf(stderr, "Failed to read font data content\n");
                    goto error_cleanup;
                }
                for (uint32_t i = 0; i < chunk; i++) checksum += tmp[i];
                remaining -= chunk;
            }

            // 读取校验和
            uint8_t pkt_checksum;
            if (safe_read(user_ptr, read_cb, &offset, &pkt_checksum, 1) != 0) {
                fprintf(stderr, "Failed to read packet checksum\n");
                goto error_cleanup;
            }

            if ((uint8_t)(checksum & 0xFF) != pkt_checksum) {
                fprintf(stderr, "Font data packet (type 0x%02X) checksum error, skipping\n", packet_type);
                // 已经读取了内容，但校验失败，不更新字体大小
            } else {
                // 校验通过，记录字体数据大小
                (*fonts)[font_idx].count = content_len;
            }
        }
        else if (packet_type >= 0x10 && packet_type <= 0x1F) {
            // 文字数据包：需要解析文本
            if (content_len < 3) {
                fprintf(stderr, "Text packet too short (content len %u), skipping\n", content_len);
                // 跳过内容及校验
                if (skip_bytes(user_ptr, read_cb, &offset, content_len + 1) != 0) {
                    goto error_cleanup;
                }
                packets_processed++;
                continue;
            }

            // 先读取前3字节（style, x, y）
            uint8_t style_font, x, y;
            if (safe_read(user_ptr, read_cb, &offset, &style_font, 1) != 0) {
                fprintf(stderr, "Failed to read text packet style_font\n");
                goto error_cleanup;
            }
            checksum += style_font;
            if (safe_read(user_ptr, read_cb, &offset, &x, 1) != 0) {
                fprintf(stderr, "Failed to read text packet x\n");
                goto error_cleanup;
            }
            checksum += x;
            if (safe_read(user_ptr, read_cb, &offset, &y, 1) != 0) {
                fprintf(stderr, "Failed to read text packet y\n");
                goto error_cleanup;
            }
            checksum += y;

            // 计算文本长度
            uint32_t text_len = content_len - 3;
            // 分配文本内存（包括结尾'\0'）
            char *text_buf = (char *)malloc(text_len + 1);
            if (!text_buf) {
                perror("Out of memory");
                goto error_cleanup;
            }

            // 读取文本内容
            if (safe_read(user_ptr, read_cb, &offset, (uint8_t*)text_buf, text_len) != 0) {
                fprintf(stderr, "Failed to read text packet text\n");
                free(text_buf);
                goto error_cleanup;
            }
            for (uint32_t i = 0; i < text_len; i++) checksum += (uint8_t)text_buf[i];
            text_buf[text_len] = '\0';

            // 读取校验和
            uint8_t pkt_checksum;
            if (safe_read(user_ptr, read_cb, &offset, &pkt_checksum, 1) != 0) {
                fprintf(stderr, "Failed to read packet checksum\n");
                free(text_buf);
                goto error_cleanup;
            }

            if ((uint8_t)(checksum & 0xFF) != pkt_checksum) {
                fprintf(stderr, "Text packet (type 0x%02X) checksum error, skipping\n", packet_type);
                free(text_buf);
            } else {
                // 校验通过，保存到texts数组
                if (*text_count >= texts_capacity) {
                    texts_capacity = texts_capacity == 0 ? 8 : texts_capacity * 2;
                    TextInfo *new_texts = (TextInfo *)realloc(*texts, texts_capacity * sizeof(TextInfo));
                    if (!new_texts) {
                        perror("Out of memory");
                        free(text_buf);
                        goto error_cleanup;
                    }
                    *texts = new_texts;
                }
                TextInfo *ti = &(*texts)[*text_count];
                ti->category = packet_type;
                ti->style_font = style_font;
                ti->x = x;
                ti->y = y;
                ti->text = text_buf;  // 接管已分配的文本内存
                (*text_count)++;
            }
        }
        else {
            // 未知类型：跳过内容及校验
            fprintf(stderr, "Unknown packet type 0x%02X, skipping\n", packet_type);
            if (skip_bytes(user_ptr, read_cb, &offset, content_len + 1) != 0) {
                goto error_cleanup;
            }
        }

        packets_processed++;
        if (packets_processed >= first_total_packets) {
            // 已处理完所有预期的包，可提前结束（但继续读取可能有多余数据）
            break;
        }
    }

    // 检查是否处理了所有包（给出警告）
    if (packets_processed != first_total_packets) {
        fprintf(stderr, "Warning: expected %d packets, processed %d\n",
                first_total_packets, packets_processed);
    }

    return 0;

error_cleanup:
    // 清理已分配的资源
    if (*fonts) {
        free(*fonts);
        *fonts = NULL;
    }
    *font_count = 0;
    if (*texts) {
        for (int i = 0; i < *text_count; i++) {
            free((*texts)[i].text);
        }
        free(*texts);
        *texts = NULL;
    }
    *text_count = 0;
    return -1;
}
