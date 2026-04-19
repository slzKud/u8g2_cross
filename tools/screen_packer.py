#!/usr/bin/env python3
"""
屏幕程序打包器 - 基于pack.py的图形界面工具
支持GUI、命令行参数和JSON配置三种模式
"""

import struct
import os
import sys
import json
import argparse
import subprocess
import tempfile
import codecs
import re
from typing import Dict, List, Tuple, Any, Optional, Set
from pathlib import Path

# 尝试设置控制台编码为UTF-8（解决中文显示问题）
try:
    if sys.platform == 'win32':
        # Windows系统尝试设置控制台编码
        import io
        if sys.stdout.encoding != 'UTF-8':
            sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
        if sys.stderr.encoding != 'UTF-8':
            sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding='utf-8', errors='replace')
except:
    pass  # 如果失败，继续使用默认编码

# ============================================================================
# 核心打包函数（从pack.py移植）
# ============================================================================

def calc_checksum(data: bytes) -> int:
    """计算累加校验和（所有字节相加，取低8位）"""
    return sum(data) & 0xFF

def build_packet(total_packets: int, packet_index: int, packet_type: int, content: bytes) -> bytes:
    """
    构建一个数据包（不含开头的 'D' 'G'）
    :param total_packets: 整个文件包含的数据包总数
    :param packet_index: 当前包序号 (0-254)
    :param packet_type: 类别ID (0-255)
    :param content: 包内容字节串
    :return: 包含总包数、序号、类型、长度、内容和校验和的完整包字节串
    """
    header = struct.pack('<BBB', total_packets, packet_index, packet_type) + struct.pack('<I', len(content))
    data_without_checksum = header + content
    checksum = calc_checksum(data_without_checksum)
    return data_without_checksum + bytes([checksum])

def create_packets(font_files: Dict[int, str], text_packets_data: List[Tuple[int, int, int, int, str]],
                   output_file: str = 'output.bin') -> None:
    """
    生成数据包文件（原create_example_packets函数重命名）
    :param font_files: 字典 {字体序号: 字体文件路径}，字体序号范围1-15
    :param text_packets_data: 列表，每个元素为 (类别ID, 样式字体组合, x, y, 文本字符串)
                              类别ID应在0x10-0x1f之间
    :param output_file: 输出文件名
    """
    # 1. 处理BDF字体转换（如果有）
    bdf_fonts_data = process_bdf_fonts(font_files, text_packets_data, verbose=False)

    # 2. 读取字体文件，构建字体数据包列表（暂存内容和长度）
    font_contents = {}          # 序号 -> 字体文件内容
    font_packets_len = {}        # 序号 -> 字体数据包总长度（含包头校验）

    for idx, path in font_files.items():
        # 检查是否是BDF字体且已转换
        if idx in bdf_fonts_data:
            # 使用转换后的二进制数据
            content = bdf_fonts_data[idx]
        else:
            # 普通字体文件
            if not os.path.exists(path):
                print(f"警告：字体文件 {path} 不存在，生成示例内容（64字节随机数据）。")
                content = bytes([i % 256 for i in range(64)])
            else:
                with open(path, 'rb') as f:
                    content = f.read()

        font_contents[idx] = content
        # 字体数据包的总长度 = 包头(5) + 内容长度 + 校验(1) = 8 + len(content)
        font_packets_len[idx] = 8 + len(content)

    # 3. 构建文字数据包，计算内容
    text_packets = []            # 每个元素为 (类别ID, 完整内容字节串)
    for (ptype, style_font, x, y, text) in text_packets_data:
        text_content = bytes([style_font, x, y]) + text.encode('utf-8')
        text_packets.append((ptype, text_content))

    # 4. 确定总包数
    font_count = len(font_contents)
    text_count = len(text_packets)
    total_packets = 1 + font_count + text_count   # 1个字体查找表包 + 字体包 + 文字包

    # 5. 计算字体查找表包的长度
    # 查找表内容: 字体数量(1) + 每个字体: 序号(1) + 字体偏移(4) + 最后4字节"其他部分后的偏移"
    lookup_content_len = 1 + font_count * (1 + 4) + 4
    lookup_packet_len = 8 + lookup_content_len   # 8 = 7(包头) + 1(校验)

    # 6. 计算各数据包在文件中的偏移（文件头已占2字节）
    # 文件头偏移0，长度2
    # 字体查找表包起始偏移 = 2
    lookup_start = 2
    # 字体数据包起始偏移 = 2 + lookup_packet_len
    font_start = lookup_start + lookup_packet_len

    # 计算每个字体包的起始偏移
    font_offsets = {}            # 序号 -> 字体包起始偏移
    current_font_offset = font_start
    total_font_len = 0
    sorted_indices = sorted(font_contents.keys())  # 按序号排序
    for idx in sorted_indices:
        font_offsets[idx] = current_font_offset
        plen = font_packets_len[idx]
        current_font_offset += plen
        total_font_len += plen

    # 字体区域结束偏移 = font_start + total_font_len
    font_end = font_start + total_font_len
    # 其他部分后的偏移（文字区域起始） = font_end
    other_part_offset = font_end

    # 7. 构建字体查找表包内容
    lookup_content = struct.pack('<B', font_count)
    for idx in sorted_indices:
        lookup_content += struct.pack('<B', idx)                # 字体序号
        lookup_content += struct.pack('<I', font_offsets[idx]) # 字体偏移
    # 最后加上4字节的"其他部分后的偏移"
    lookup_content += struct.pack('<I', other_part_offset)

    # 构建查找表包，序号设为0
    lookup_packet = build_packet(total_packets, 0, 0x00, lookup_content)

    # 8. 构建所有字体数据包
    font_packets = []            # 存储 (序号, 完整包字节)
    packet_index = 1              # 查找表占0，字体包从1开始
    for idx in sorted_indices:
        content = font_contents[idx]
        pkt = build_packet(total_packets, packet_index, idx, content)
        font_packets.append((idx, pkt))
        packet_index += 1

    # 9. 构建所有文字数据包
    text_packets_full = []
    for (ptype, text_content) in text_packets:
        pkt = build_packet(total_packets, packet_index, ptype, text_content)
        text_packets_full.append(pkt)
        packet_index += 1

    # 10. 写入文件
    with open(output_file, 'wb') as f:
        # 写入文件头
        f.write(b'DG')
        # 写入查找表包
        f.write(lookup_packet)
        # 写入字体包
        for _, pkt in font_packets:
            f.write(pkt)
        # 写入文字包
        for pkt in text_packets_full:
            f.write(pkt)

    print(f"成功生成数据包文件: {output_file}")
    print(f"总包数: {total_packets}")
    print("字体查找表记录了以下字体：")
    for idx in sorted_indices:
        print(f"  字体序号 {idx}: 偏移 {font_offsets[idx]:#x}")
    print(f"文字区域起始偏移: {other_part_offset:#x}")

# 保持与pack.py的兼容性
create_example_packets = create_packets

# ============================================================================
# BIN文件解析功能
# ============================================================================

def parse_packet(data: bytes, offset: int) -> Tuple[int, int, int, bytes, int]:
    """
    解析一个数据包
    :param data: 整个文件数据
    :param offset: 数据包起始偏移
    :return: (total_packets, packet_index, packet_type, content, checksum)
    :raises ValueError: 如果数据包格式无效
    """
    if offset + 8 > len(data):
        raise ValueError(f"数据包不完整，偏移 {offset}，数据长度 {len(data)}")

    # 解析包头：总包数(1)、序号(1)、类型(1)、内容长度(4)
    total_packets, packet_index, packet_type = struct.unpack_from('<BBB', data, offset)
    content_len = struct.unpack_from('<I', data, offset + 3)[0]

    # 检查数据包是否完整
    packet_len = 7 + content_len + 1  # 7字节包头 + 内容长度 + 1字节校验和
    if offset + packet_len > len(data):
        raise ValueError(f"数据包内容不完整，需要 {packet_len} 字节，但只有 {len(data) - offset} 字节")

    # 提取内容和校验和
    content_start = offset + 7
    content = data[content_start:content_start + content_len]
    checksum = data[content_start + content_len]

    # 验证校验和
    header_and_content = data[offset:content_start + content_len]
    calculated_checksum = calc_checksum(header_and_content)
    if checksum != calculated_checksum:
        raise ValueError(f"校验和不匹配：计算值 {calculated_checksum}，实际值 {checksum}")

    return total_packets, packet_index, packet_type, content, checksum

def parse_bin_file(bin_file: str) -> Tuple[Dict[int, bytes], List[Tuple[int, int, int, int, str]], Dict[str, Any]]:
    """
    解析BIN文件，提取字体和文本配置信息
    :param bin_file: BIN文件路径
    :return: (font_contents, text_packets_data, metadata)
             font_contents: 字体序号 -> 字体内容字节串
             text_packets_data: 列表，每个元素为 (类型, 样式字体, x, y, 文本)
             metadata: 包含总包数、文字区域偏移等信息的字典
    :raises ValueError: 如果文件格式无效
    """
    with open(bin_file, 'rb') as f:
        data = f.read()

    if len(data) < 2:
        raise ValueError("文件太小，缺少文件头")

    # 检查文件头
    if data[0:2] != b'DG':
        raise ValueError(f"无效的文件头：期望 'DG'，实际得到 {data[0:2]}")

    # 解析第一个包（字体查找表）
    offset = 2  # 跳过'DG'头
    try:
        total_packets, packet_index, packet_type, lookup_content, _ = parse_packet(data, offset)
    except ValueError as e:
        raise ValueError(f"解析查找表包失败：{e}")

    # 验证查找表包
    if packet_index != 0:
        raise ValueError(f"查找表包序号应为0，实际为{packet_index}")
    if packet_type != 0x00:
        raise ValueError(f"查找表包类型应为0x00，实际为0x{packet_type:02x}")

    # 解析查找表内容
    if len(lookup_content) < 1:
        raise ValueError("查找表内容太短")

    font_count = lookup_content[0]
    expected_lookup_len = 1 + font_count * (1 + 4) + 4
    if len(lookup_content) != expected_lookup_len:
        raise ValueError(f"查找表长度不一致：期望 {expected_lookup_len}，实际 {len(lookup_content)}")

    # 解析字体偏移信息
    font_offsets = {}
    lookup_offset = 1  # 跳过字体数量字节
    for i in range(font_count):
        if lookup_offset + 5 > len(lookup_content):
            raise ValueError("查找表内容不完整")
        font_idx = lookup_content[lookup_offset]
        font_offset = struct.unpack_from('<I', lookup_content, lookup_offset + 1)[0]
        font_offsets[font_idx] = font_offset
        lookup_offset += 5

    # 获取文字区域起始偏移
    text_area_offset = struct.unpack_from('<I', lookup_content, lookup_offset)[0]

    # 解析字体包
    font_contents = {}
    for font_idx, font_offset in sorted(font_offsets.items()):
        if font_offset >= len(data):
            raise ValueError(f"字体包偏移 {font_offset} 超出文件范围")

        try:
            tp, pi, pt, content, _ = parse_packet(data, font_offset)
        except ValueError as e:
            raise ValueError(f"解析字体包 {font_idx} 失败：{e}")

        # 验证字体包
        if pi == 0:
            raise ValueError(f"字体包序号不应为0（字体 {font_idx}）")
        if pt != font_idx:
            raise ValueError(f"字体包类型不匹配：期望 {font_idx}，实际 {pt}")

        font_contents[font_idx] = content

    # 解析文字包
    text_packets_data = []
    current_offset = text_area_offset

    # 假设文字包从text_area_offset开始，一直到文件结束
    while current_offset < len(data):
        try:
            tp, pi, pt, content, _ = parse_packet(data, current_offset)
        except ValueError as e:
            # 可能不是有效的数据包，停止解析
            break

        # 检查是否为文字包（类型在0x10-0x1f范围内）
        if 0x10 <= pt <= 0x1f:
            if len(content) < 3:
                raise ValueError(f"文字包内容太短（字体 {pt}）")

            style_font = content[0]
            x = content[1]
            y = content[2]
            text = content[3:].decode('utf-8', errors='replace')
            text_packets_data.append((pt, style_font, x, y, text))

        # 移动到下一个数据包
        packet_len = 7 + len(content) + 1
        current_offset += packet_len

    metadata = {
        'total_packets': total_packets,
        'text_area_offset': text_area_offset,
        'file_size': len(data),
        'font_count': font_count,
        'text_count': len(text_packets_data)
    }

    return font_contents, text_packets_data, metadata

def extract_font_from_bin(bin_file: str, font_index: int, output_file: str) -> bool:
    """
    从BIN文件中提取指定字体的二进制内容
    :param bin_file: BIN文件路径
    :param font_index: 字体序号
    :param output_file: 输出文件路径
    :return: 是否成功提取
    """
    try:
        font_contents, _, _ = parse_bin_file(bin_file)
        if font_index not in font_contents:
            print(f"错误：字体序号 {font_index} 不存在于文件中")
            return False

        with open(output_file, 'wb') as f:
            f.write(font_contents[font_index])

        print(f"成功提取字体 {font_index} 到 {output_file}")
        return True
    except Exception as e:
        print(f"提取字体失败：{e}")
        return False

def replace_font_in_bin(bin_file: str, font_index: int, new_font_file: str, output_file: str) -> bool:
    """
    替换BIN文件中的指定字体
    :param bin_file: 原始BIN文件路径
    :param font_index: 要替换的字体序号
    :param new_font_file: 新字体文件路径
    :param output_file: 输出文件路径（可以是原始文件进行覆盖）
    :return: 是否成功替换
    """
    try:
        # 读取新字体文件
        with open(new_font_file, 'rb') as f:
            new_font_content = f.read()

        # 解析原始文件
        font_contents, text_packets_data, metadata = parse_bin_file(bin_file)

        if font_index not in font_contents:
            print(f"错误：字体序号 {font_index} 不存在于原始文件中")
            return False

        # 替换字体内容
        font_contents[font_index] = new_font_content

        # 重新生成字体文件字典（路径映射）
        # 注意：这里需要临时保存字体内容到文件
        import tempfile
        temp_dir = tempfile.mkdtemp()
        font_files = {}
        for idx, content in font_contents.items():
            temp_font_file = os.path.join(temp_dir, f"font_{idx}.bin")
            with open(temp_font_file, 'wb') as f:
                f.write(content)
            font_files[idx] = temp_font_file

        # 重新生成数据包文件
        create_packets(font_files, text_packets_data, output_file)

        # 清理临时文件
        import shutil
        shutil.rmtree(temp_dir, ignore_errors=True)

        print(f"成功替换字体 {font_index} 并保存到 {output_file}")
        return True
    except Exception as e:
        print(f"替换字体失败：{e}")
        return False

# ============================================================================
# JSON配置支持
# ============================================================================

def load_config_from_json(json_file: str) -> Tuple[Dict[int, str], List[Tuple[int, int, int, int, str]], str]:
    """
    从JSON文件加载配置
    :param json_file: JSON配置文件路径
    :return: (font_files, text_packets_data, output_file)
    """
    with open(json_file, 'r', encoding='utf-8') as f:
        config = json.load(f)

    # 解析字体文件配置
    font_files = {}
    if 'font_files' in config:
        for idx_str, path in config['font_files'].items():
            try:
                idx = int(idx_str)
                font_files[idx] = str(path)
            except ValueError:
                print(f"警告：字体序号 '{idx_str}' 不是有效的整数，已跳过")

    # 解析文本数据包配置
    text_packets_data = []
    if 'text_packets' in config:
        for packet in config['text_packets']:
            try:
                # 支持十六进制和十进制格式
                ptype = int(packet['type'], 0) if isinstance(packet['type'], str) else int(packet['type'])
                style_font = int(packet['style_font'], 0) if isinstance(packet['style_font'], str) else int(packet['style_font'])
                x = int(packet['x'])
                y = int(packet['y'])
                text = str(packet['text'])
                text_packets_data.append((ptype, style_font, x, y, text))
            except (KeyError, ValueError) as e:
                print(f"警告：跳过无效的文本数据包配置 {packet}: {e}")

    # 获取输出文件路径
    output_file = config.get('output_file', 'output.bin')

    return font_files, text_packets_data, output_file

def save_config_to_json(config_file: str, font_files: Dict[int, str],
                        text_packets_data: List[Tuple[int, int, int, int, str]],
                        output_file: str) -> None:
    """
    保存配置到JSON文件
    """
    config = {
        'font_files': {str(idx): path for idx, path in font_files.items()},
        'text_packets': [],
        'output_file': output_file
    }

    for ptype, style_font, x, y, text in text_packets_data:
        config['text_packets'].append({
            'type': f"0x{ptype:02x}",
            'style_font': f"0x{style_font:02x}",
            'x': x,
            'y': y,
            'text': text
        })

    with open(config_file, 'w', encoding='utf-8') as f:
        json.dump(config, f, indent=2, ensure_ascii=False)

# ============================================================================
# BDF字体转换支持
# ============================================================================

def extract_unique_codepoints(text: str) -> Set[int]:
    """
    从文本中提取唯一的Unicode码点
    """
    codepoints = set()
    for char in text:
        codepoints.add(ord(char))
    return codepoints

def codepoints_to_map_entries(codepoints: Set[int], merge_ascii_ranges: bool = True) -> List[str]:
    """
    将码点集合转换为map文件条目
    """
    entries = []

    # 将码点排序
    sorted_codepoints = sorted(codepoints)

    if merge_ascii_ranges:
        i = 0
        while i < len(sorted_codepoints):
            start = sorted_codepoints[i]
            # 只对ASCII字符（0-127）进行范围合并
            if start <= 127:
                j = i + 1
                while j < len(sorted_codepoints) and sorted_codepoints[j] == sorted_codepoints[j-1] + 1:
                    # 检查是否仍在ASCII范围内
                    if sorted_codepoints[j] > 127:
                        break
                    j += 1

                if j - i > 2:  # 至少3个连续字符才合并为范围
                    entries.append(f"{start}-{sorted_codepoints[j-1]}")
                    i = j
                    continue

            # 单个字符：转换为十六进制格式，带$前缀
            entries.append(f"${sorted_codepoints[i]:04X}")
            i += 1
    else:
        # 不合并范围，所有字符单独列出
        for cp in sorted_codepoints:
            entries.append(f"${cp:04X}")

    return entries

def generate_map_file(codepoints: Set[int], output_map: str, merge_ascii_ranges: bool = True) -> None:
    """
    生成map文件
    """
    entries = codepoints_to_map_entries(codepoints, merge_ascii_ranges)

    with open(output_map, 'w', encoding='utf-8') as f:
        for entry in entries:
            f.write(entry + ',\n')

    print(f"已生成map文件: {output_map}，包含 {len(entries)} 个条目")

def convert_bdf_to_bin_data(bdf_file: str, font_name: str, map_file: str,
                           bdfconv_path: Optional[str] = None,
                           verbose: bool = False) -> bytes:
    """
    将BDF文件转换为二进制数据，返回字节串

    注意：此函数会生成临时C文件和临时BIN文件，并在转换后清理
    """
    # 设置默认bdfconv路径
    if bdfconv_path is None:
        bdfconv_path = os.path.join(os.path.dirname(__file__), "bdf2bin", "bdfconv.exe")

    # 检查bdfconv是否存在
    if not os.path.exists(bdfconv_path):
        raise FileNotFoundError(f"找不到bdfconv可执行文件: {bdfconv_path}")

    # 构建bdf2bin.py的命令行参数
    bdf2bin_script = os.path.join(os.path.dirname(__file__), "bdf2bin", "bdf2bin.py")

    # 创建临时BIN文件
    temp_bin = tempfile.NamedTemporaryFile(mode='wb', suffix='.bin', delete=False)
    bin_file = temp_bin.name
    temp_bin.close()

    try:
        # 构建命令
        cmd = [sys.executable, bdf2bin_script, bdf_file, "-n", font_name, "-m", map_file,
               "-o", bin_file, "--bdfconv", bdfconv_path]
               # 不使用--keep-c，让bdf2bin.py清理C文件

        if verbose:
            cmd.append("-v")

        result = subprocess.run(cmd, capture_output=True, text=True, encoding='utf-8')

        if result.returncode != 0:
            raise RuntimeError(f"bdf2bin转换失败:\n{result.stderr}")

        # 读取BIN文件内容
        with open(bin_file, 'rb') as f:
            bin_data = f.read()

        return bin_data
    finally:
        # 清理临时BIN文件
        try:
            if os.path.exists(bin_file):
                os.unlink(bin_file)
        except:
            pass

def get_font_name_from_bdf(bdf_path: str) -> str:
    """
    从BDF文件路径生成字体名称
    格式: u8g2_font_<basename_without_ext>
    """
    basename = os.path.splitext(os.path.basename(bdf_path))[0]
    return f"u8g2_font_{basename}"

def process_bdf_fonts(font_files: Dict[int, str],
                      text_packets_data: List[Tuple[int, int, int, int, str]],
                      verbose: bool = False) -> Dict[int, bytes]:
    """
    处理BDF字体转换，返回字体序号到二进制数据的映射

    对于每个BDF字体文件:
    1. 找出使用该字体的所有文本数据包
    2. 提取所有唯一字符，并添加ASCII 32-127
    3. 生成map文件
    4. 转换BDF为二进制数据

    返回: 字典 {字体序号: 二进制数据}
    """
    bdf_fonts_data = {}

    # 找出所有BDF字体
    bdf_font_indices = [idx for idx, path in font_files.items()
                       if isinstance(path, str) and path.lower().endswith('.bdf')]

    if not bdf_font_indices:
        return bdf_fonts_data

    # 构建字体序号到文本字符的映射
    font_to_codepoints = {idx: set() for idx in bdf_font_indices}

    # 添加默认ASCII字符 32-127
    for idx in bdf_font_indices:
        font_to_codepoints[idx].update(range(32, 128))

    # 从文本数据包中提取字符
    for ptype, style_font, x, y, text in text_packets_data:
        font_id = style_font & 0x0F  # 低4位是字体序号
        if font_id in font_to_codepoints:
            codepoints = extract_unique_codepoints(text)
            font_to_codepoints[font_id].update(codepoints)

    # 转换每个BDF字体
    for idx in bdf_font_indices:
        bdf_path = font_files[idx]

        # 生成字体名称
        font_name = get_font_name_from_bdf(bdf_path)

        # 提取字符集
        codepoints = font_to_codepoints[idx]
        print(f"字体 {idx} (BDF): 需要 {len(codepoints)} 个字符，包括ASCII 32-127")

        if not codepoints:
            print(f"警告: 字体 {idx} 没有字符需要转换，跳过")
            continue

        # 创建临时map文件
        temp_map = tempfile.NamedTemporaryFile(mode='w', suffix='.map', delete=False, encoding='utf-8')
        map_file = temp_map.name
        temp_map.close()

        try:
            # 生成map文件
            generate_map_file(codepoints, map_file, merge_ascii_ranges=True)

            # 转换BDF到二进制数据
            bin_data = convert_bdf_to_bin_data(bdf_path, font_name, map_file, verbose=verbose)

            bdf_fonts_data[idx] = bin_data
            print(f"字体 {idx} 转换完成: {len(bin_data)} 字节")
        except Exception as e:
            print(f"错误: 字体 {idx} 转换失败: {e}")
            raise
        finally:
            # 清理临时map文件
            try:
                if os.path.exists(map_file):
                    os.unlink(map_file)
            except:
                pass

    return bdf_fonts_data

# ============================================================================
# 命令行参数支持
# ============================================================================

def parse_command_line() -> argparse.Namespace:
    """
    解析命令行参数
    """
    parser = argparse.ArgumentParser(
        description='屏幕程序打包器 - 生成屏幕显示数据包',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  # 使用JSON配置文件
  python screen_packer.py --config config.json

  # 使用命令行参数
  python screen_packer.py --fonts "1:font1.bin,2:font2.bin" --texts "0x10,0x01,10,20,Hello" --output output.bin

  # 启动GUI界面
  python screen_packer.py --gui
        """
    )

    parser.add_argument('--config', '-c', type=str, help='JSON配置文件路径')
    parser.add_argument('--fonts', '-f', type=str, help='字体文件配置，格式: "序号1:路径1,序号2:路径2"')
    parser.add_argument('--texts', '-t', type=str, action='append',
                       help='文本数据包配置，格式: "类型,样式字体,x,y,文本"。可多次使用')
    parser.add_argument('--output', '-o', type=str, default='output.bin', help='输出文件路径')
    parser.add_argument('--gui', '-g', action='store_true', help='启动图形界面')

    return parser.parse_args()

def parse_fonts_argument(fonts_arg: str) -> Dict[int, str]:
    """
    解析--fonts参数
    """
    font_files = {}
    if not fonts_arg:
        return font_files

    for item in fonts_arg.split(','):
        if ':' not in item:
            print(f"警告：跳过无效的字体配置项 '{item}'，格式应为 '序号:路径'")
            continue
        idx_str, path = item.split(':', 1)
        try:
            idx = int(idx_str)
            font_files[idx] = path.strip()
        except ValueError:
            print(f"警告：字体序号 '{idx_str}' 不是有效的整数，已跳过")

    return font_files

def parse_texts_argument(texts_args: List[str]) -> List[Tuple[int, int, int, int, str]]:
    """
    解析--texts参数
    """
    text_packets_data = []
    if not texts_args:
        return text_packets_data

    for text_arg in texts_args:
        parts = text_arg.split(',')
        if len(parts) < 5:
            print(f"警告：跳过无效的文本配置项 '{text_arg}'，需要5个参数")
            continue

        try:
            # 支持十六进制格式
            ptype = int(parts[0], 0) if parts[0].startswith('0x') else int(parts[0])
            style_font = int(parts[1], 0) if parts[1].startswith('0x') else int(parts[1])
            x = int(parts[2])
            y = int(parts[3])
            text = ','.join(parts[4:])  # 文本可能包含逗号
            text_packets_data.append((ptype, style_font, x, y, text))
        except ValueError as e:
            print(f"警告：跳过无效的文本配置项 '{text_arg}': {e}")

    return text_packets_data

def run_from_command_line(args: argparse.Namespace) -> None:
    """
    根据命令行参数运行打包
    """
    font_files = {}
    text_packets_data = []

    # 优先使用配置文件
    if args.config:
        if os.path.exists(args.config):
            font_files, text_packets_data, output_file = load_config_from_json(args.config)
            if args.output != 'output.bin':  # 命令行输出文件覆盖配置文件中的设置
                output_file = args.output
        else:
            print(f"错误：配置文件 '{args.config}' 不存在")
            sys.exit(1)
    else:
        # 解析命令行参数
        if args.fonts:
            font_files = parse_fonts_argument(args.fonts)
        if args.texts:
            text_packets_data = parse_texts_argument(args.texts)
        output_file = args.output

    # 检查是否有配置
    if not font_files and not text_packets_data:
        print("错误：未提供任何配置（字体或文本数据包）")
        print("请使用 --config 指定配置文件，或使用 --fonts 和 --texts 提供配置")
        sys.exit(1)

    # 执行打包
    try:
        create_packets(font_files, text_packets_data, output_file)
    except Exception as e:
        print(f"错误：生成数据包失败 - {e}")
        sys.exit(1)

# ============================================================================
# GUI界面
# ============================================================================

def run_gui() -> None:
    """
    启动图形界面
    """
    try:
        import tkinter as tk
        from tkinter import ttk, filedialog, messagebox
    except ImportError:
        print("错误：未找到tkinter，无法启动GUI界面")
        print("请安装tkinter或使用命令行模式")
        sys.exit(1)

    class ScreenPackerGUI:
        def __init__(self, root):
            self.root = root
            self.root.title("屏幕程序打包器")
            self.root.geometry("1000x850")

            # 配置数据
            self.font_files = {}  # 序号 -> 路径
            self.text_packets = []  # 列表 of (ptype, style_font, x, y, text)
            self.current_bin_file = None  # 当前打开的BIN文件路径

            # 选择状态
            self.selected_font_index = None  # 当前选中的字体序号
            self.selected_text_index = None  # 当前选中的文本包索引

            # 样式勾选框变量
            self.bold_var = tk.BooleanVar()
            self.italic_var = tk.BooleanVar()
            self.underline_var = tk.BooleanVar()
            self.strikethrough_var = tk.BooleanVar()
            self.font_id_combo_var = tk.StringVar()  # 字体选择下拉框

            self.setup_ui()

        def get_available_font_ids(self):
            """获取可用的字体ID列表（1-15中未使用的）"""
            all_ids = set(range(1, 16))  # 1-15
            used_ids = set(self.font_files.keys())
            available_ids = sorted(list(all_ids - used_ids))
            return available_ids

        def get_text_type_options(self):
            """获取文本类型选项（0x10-0x1f）"""
            return [f"0x{i:02x}" for i in range(0x10, 0x20)]

        def get_configured_font_ids(self):
            """获取已配置的字体ID列表（用于文本包字体选择）"""
            return sorted(list(self.font_files.keys()))

        def update_font_selection_combo(self):
            """更新文本包字体选择下拉框的选项"""
            font_ids = self.get_configured_font_ids()
            if hasattr(self, 'text_font_combo'):
                self.text_font_combo['values'] = font_ids
                # 如果没有选择字体，清空选择
                if not self.font_id_combo_var.get() and font_ids:
                    self.font_id_combo_var.set('')
                elif self.font_id_combo_var.get() and int(self.font_id_combo_var.get()) not in font_ids:
                    # 如果当前选择的字体不存在于列表中，清空选择
                    self.font_id_combo_var.set('')

        def update_font_id_combo(self):
            """更新字体ID下拉框的可用选项"""
            available_ids = self.get_available_font_ids()
            # 如果当前选中的字体ID不在可用列表中（即正在编辑现有字体），将其加入列表
            if self.selected_font_index is not None and self.selected_font_index not in available_ids:
                available_ids.append(self.selected_font_index)
                available_ids.sort()
            self.font_id_combo['values'] = available_ids
            # 如果没有选中字体，清空选择
            if self.selected_font_index is None:
                self.font_index_var.set('')
            else:
                self.font_index_var.set(str(self.selected_font_index))

        def update_text_type_combo(self):
            """更新文本类型下拉框的可用选项"""
            options = self.get_text_type_options()
            self.text_type_combo['values'] = options
            # 如果当前选中了文本包，设置其类型
            if self.selected_text_index is not None and 0 <= self.selected_text_index < len(self.text_packets):
                ptype, _, _, _, _ = self.text_packets[self.selected_text_index]
                self.text_type_var.set(f"0x{ptype:02x}")
            else:
                self.text_type_var.set('')

        def add_or_update_font(self):
            """添加或更新字体"""
            try:
                idx_str = self.font_index_var.get().strip()
                path = self.font_path_var.get().strip()

                if not idx_str:
                    messagebox.showwarning("警告", "请选择字体序号")
                    return
                if not path:
                    messagebox.showwarning("警告", "请选择字体文件路径")
                    return

                idx = int(idx_str)

                if idx < 1 or idx > 15:
                    messagebox.showwarning("警告", "字体序号必须在1-15之间")
                    return

                if self.selected_font_index is None:
                    # 添加新字体
                    if idx in self.font_files:
                        messagebox.showwarning("警告", f"字体序号 {idx} 已存在")
                        return
                    self.font_files[idx] = path
                    messagebox.showinfo("成功", f"字体 {idx} 已添加")
                else:
                    # 更新现有字体
                    old_idx = self.selected_font_index
                    if idx != old_idx:
                        # 如果序号改变，需要删除旧的，添加新的
                        del self.font_files[old_idx]
                        self.font_files[idx] = path
                    else:
                        # 序号不变，直接更新路径
                        self.font_files[idx] = path
                    messagebox.showinfo("成功", f"字体 {idx} 已更新")

                # 更新界面
                self.update_font_tree()
                self.update_font_id_combo()
                self.clear_font_selection()

            except ValueError:
                messagebox.showwarning("警告", "字体序号必须是数字")

        def clear_font_selection(self):
            """清除字体选择"""
            self.selected_font_index = None
            self.font_tree.selection_remove(self.font_tree.selection())
            self.font_index_var.set('')
            self.font_path_var.set('')
            self.add_update_font_btn.config(text="添加字体")
            self.update_font_id_combo()
            # 禁用提取和替换按钮（如果没有打开的BIN文件或没有选择）
            self.extract_font_btn.config(state="disabled")
            self.replace_font_btn.config(state="disabled")

        def add_or_update_text_packet(self):
            """添加或更新文本数据包"""
            try:
                type_str = self.text_type_var.get().strip()
                font_id_str = self.font_id_combo_var.get().strip()
                x_str = self.text_x_var.get().strip()
                y_str = self.text_y_var.get().strip()
                text = self.text_text_var.get().strip()

                if not type_str:
                    messagebox.showwarning("警告", "请选择文本类型")
                    return
                if not font_id_str:
                    messagebox.showwarning("警告", "请选择字体")
                    return
                if not x_str:
                    messagebox.showwarning("警告", "请输入X坐标")
                    return
                if not y_str:
                    messagebox.showwarning("警告", "请输入Y坐标")
                    return
                if not text:
                    messagebox.showwarning("警告", "请输入文本内容")
                    return

                # 解析输入
                ptype = int(type_str, 0) if type_str.startswith('0x') else int(type_str)
                font_id = int(font_id_str)

                # 组合样式字体字节（高四位：粗体、斜体、下划线、删除线）
                style_byte = 0
                if self.bold_var.get():
                    style_byte |= 0x80  # 第7位：粗体
                if self.italic_var.get():
                    style_byte |= 0x40  # 第6位：斜体
                if self.underline_var.get():
                    style_byte |= 0x20  # 第5位：下划线
                if self.strikethrough_var.get():
                    style_byte |= 0x10  # 第4位：删除线

                # 添加字体序号（低四位）
                style_font = style_byte | (font_id & 0x0F)

                x = int(x_str)
                y = int(y_str)

                if ptype < 0x10 or ptype > 0x1f:
                    if not messagebox.askyesno("确认", f"类型0x{ptype:02x}不在推荐范围(0x10-0x1f)，是否继续？"):
                        return

                if self.selected_text_index is None:
                    # 添加新文本包
                    self.text_packets.append((ptype, style_font, x, y, text))
                    messagebox.showinfo("成功", "文本包已添加")
                else:
                    # 更新现有文本包
                    index = self.selected_text_index
                    self.text_packets[index] = (ptype, style_font, x, y, text)
                    messagebox.showinfo("成功", "文本包已更新")

                # 更新界面
                self.update_text_tree()
                self.update_text_type_combo()
                self.clear_text_selection()

            except ValueError as e:
                messagebox.showwarning("警告", f"输入格式错误: {e}")

        def clear_text_selection(self):
            """清除文本包选择"""
            self.selected_text_index = None
            self.text_tree.selection_remove(self.text_tree.selection())
            self.text_type_var.set('')
            # 清除样式勾选框
            self.bold_var.set(False)
            self.italic_var.set(False)
            self.underline_var.set(False)
            self.strikethrough_var.set(False)
            # 清除字体选择
            self.font_id_combo_var.set('')
            # 清除其他字段
            self.text_x_var.set('')
            self.text_y_var.set('')
            self.text_text_var.set('')
            self.add_update_text_btn.config(text="添加文本包")
            self.update_text_type_combo()

        def on_font_tree_select(self, event):
            """字体树选中事件处理"""
            selection = self.font_tree.selection()
            if selection and self.current_bin_file:
                self.extract_font_btn.config(state="normal")
                self.replace_font_btn.config(state="normal")
            else:
                self.extract_font_btn.config(state="disabled")
                self.replace_font_btn.config(state="disabled")

            # 填充字体信息到编辑字段
            if selection:
                item = selection[0]
                values = self.font_tree.item(item)['values']
                if values:
                    font_index = int(values[0])
                    # 从font_files字典中获取原始路径
                    if font_index in self.font_files:
                        font_path = self.font_files[font_index]
                        self.selected_font_index = font_index
                        self.font_index_var.set(str(font_index))
                        self.font_path_var.set(font_path)
                        self.add_update_font_btn.config(text="更新字体")
                        self.update_font_id_combo()
            else:
                self.clear_font_selection()

        def on_text_tree_select(self, event):
            """文本树选中事件处理"""
            selection = self.text_tree.selection()
            # 填充文本包信息到编辑字段
            if selection:
                item = selection[0]
                values = self.text_tree.item(item)['values']
                if values:
                    # 解析显示的值（类型是十六进制字符串）
                    type_str = values[0]  # 格式如 "0x10"
                    style_font_str = values[1]  # 格式如 "0x01"
                    x_str = values[2]
                    y_str = values[3]
                    text = values[4]

                    # 找到在列表中的索引
                    for i, (ptype, style_font, x, y, t) in enumerate(self.text_packets):
                        if (f"0x{ptype:02x}" == type_str and
                            f"0x{style_font:02x}" == style_font_str and
                            str(x) == x_str and str(y) == y_str and
                            (t == text or (len(t) > 50 and text == t[:50] + "..."))):
                            self.selected_text_index = i
                            break

                    # 填充字段
                    self.text_type_var.set(type_str)

                    # 解析样式字体字节
                    style_font = int(style_font_str, 0) if style_font_str.startswith('0x') else int(style_font_str)

                    # 设置样式勾选框
                    self.bold_var.set(bool(style_font & 0x80))      # 第7位：粗体
                    self.italic_var.set(bool(style_font & 0x40))    # 第6位：斜体
                    self.underline_var.set(bool(style_font & 0x20)) # 第5位：下划线
                    self.strikethrough_var.set(bool(style_font & 0x10)) # 第4位：删除线

                    # 设置字体下拉框（低四位）
                    font_id = style_font & 0x0F
                    self.font_id_combo_var.set(str(font_id) if font_id > 0 else '')

                    self.text_x_var.set(x_str)
                    self.text_y_var.set(y_str)
                    # 如果文本被截断，显示完整文本
                    if text.endswith("..."):
                        # 查找完整文本
                        for _, _, _, _, full_text in self.text_packets:
                            if full_text.startswith(text[:-3]):
                                text = full_text
                                break
                    self.text_text_var.set(text)
                    self.add_update_text_btn.config(text="更新文本包")
                    self.update_text_type_combo()
                    self.update_font_selection_combo()  # 确保字体下拉框选项正确
            else:
                self.clear_text_selection()

        def setup_ui(self):
            # 创建主框架
            main_frame = ttk.Frame(self.root, padding="10")
            main_frame.grid(row=0, column=0, sticky=(tk.W, tk.E, tk.N, tk.S))

            # 配置网格权重
            self.root.columnconfigure(0, weight=1)
            self.root.rowconfigure(0, weight=1)
            main_frame.columnconfigure(0, weight=1)

            # 字体配置区域
            font_frame = ttk.LabelFrame(main_frame, text="字体文件配置", padding="5")
            font_frame.grid(row=0, column=0, sticky=(tk.W, tk.E), pady=(0, 10))
            font_frame.columnconfigure(1, weight=1)

            ttk.Label(font_frame, text="字体序号 (1-15):").grid(row=0, column=0, padx=5, pady=2, sticky=tk.W)
            self.font_index_var = tk.StringVar()
            self.font_id_combo = ttk.Combobox(font_frame, textvariable=self.font_index_var, width=10, state="readonly")
            self.font_id_combo.grid(row=0, column=1, padx=5, pady=2, sticky=tk.W)
            self.update_font_id_combo()  # 初始化可用ID列表

            ttk.Label(font_frame, text="字体文件路径:").grid(row=1, column=0, padx=5, pady=2, sticky=tk.W)
            self.font_path_var = tk.StringVar()
            font_path_entry = ttk.Entry(font_frame, textvariable=self.font_path_var, width=50)
            font_path_entry.grid(row=1, column=1, padx=5, pady=2, sticky=(tk.W, tk.E))

            ttk.Button(font_frame, text="浏览...", command=self.browse_font_file).grid(row=1, column=2, padx=5, pady=2)

            # 字体操作按钮
            font_button_frame = ttk.Frame(font_frame)
            font_button_frame.grid(row=2, column=0, columnspan=3, pady=5)

            self.add_update_font_btn = ttk.Button(font_button_frame, text="添加字体", command=self.add_or_update_font)
            self.add_update_font_btn.grid(row=0, column=0, padx=5)

            ttk.Button(font_button_frame, text="清除选择", command=self.clear_font_selection).grid(row=0, column=1, padx=5)

            # 字体列表
            self.font_tree = ttk.Treeview(font_frame, columns=("序号", "路径"), show="headings", height=4)
            self.font_tree.heading("序号", text="序号")
            self.font_tree.heading("路径", text="文件路径")
            self.font_tree.column("序号", width=80)
            self.font_tree.column("路径", width=400)
            self.font_tree.grid(row=3, column=0, columnspan=3, pady=(5, 0), sticky=(tk.W, tk.E))
            # 绑定选中事件
            self.font_tree.bind('<<TreeviewSelect>>', self.on_font_tree_select)

            ttk.Button(font_frame, text="删除选中字体", command=self.remove_font).grid(row=4, column=0, columnspan=3, pady=5)

            # 文本数据包配置区域
            text_frame = ttk.LabelFrame(main_frame, text="文本数据包配置", padding="5")
            text_frame.grid(row=1, column=0, sticky=(tk.W, tk.E), pady=(0, 10))
            text_frame.columnconfigure(1, weight=1)

            # 输入字段
            # 类型字段（下拉框）
            ttk.Label(text_frame, text="类型 (0x10-0x1f):").grid(row=0, column=0, padx=5, pady=2, sticky=tk.W)
            self.text_type_var = tk.StringVar()
            self.text_type_combo = ttk.Combobox(text_frame, textvariable=self.text_type_var, width=15, state="readonly")
            self.text_type_combo.grid(row=0, column=1, padx=5, pady=2, sticky=tk.W)
            self.update_text_type_combo()  # 初始化类型选项

            # 样式字体配置（第1行）
            ttk.Label(text_frame, text="样式字体:").grid(row=1, column=0, padx=5, pady=2, sticky=tk.W)

            # 创建框架容纳样式勾选框和字体选择
            style_font_frame = ttk.Frame(text_frame)
            style_font_frame.grid(row=1, column=1, padx=5, pady=2, sticky=tk.W)

            # 样式勾选框（高四位：粗体、斜体、下划线、删除线）
            self.bold_cb = ttk.Checkbutton(style_font_frame, text="粗体", variable=self.bold_var)
            self.bold_cb.grid(row=0, column=0, padx=(0, 5))

            self.italic_cb = ttk.Checkbutton(style_font_frame, text="斜体", variable=self.italic_var)
            self.italic_cb.grid(row=0, column=1, padx=(0, 5))

            self.underline_cb = ttk.Checkbutton(style_font_frame, text="下划线", variable=self.underline_var)
            self.underline_cb.grid(row=0, column=2, padx=(0, 5))

            self.strikethrough_cb = ttk.Checkbutton(style_font_frame, text="删除线", variable=self.strikethrough_var)
            self.strikethrough_cb.grid(row=0, column=3, padx=(0, 5))

            # 字体选择下拉框（低四位：字体序号）
            ttk.Label(style_font_frame, text="字体:").grid(row=1, column=0, padx=(0, 5), pady=(5, 0), sticky=tk.W)
            self.text_font_combo = ttk.Combobox(style_font_frame, textvariable=self.font_id_combo_var, width=10, state="readonly")
            self.text_font_combo.grid(row=1, column=1, columnspan=3, padx=(0, 5), pady=(5, 0), sticky=tk.W)
            self.update_font_selection_combo()  # 初始化字体选项

            # 其他字段（从第2行开始）
            other_fields = [
                ("X坐标:", "text_x_var", 15),
                ("Y坐标:", "text_y_var", 15),
                ("文本内容:", "text_text_var", 40)
            ]

            for i, (label, var_name, width) in enumerate(other_fields, start=2):
                ttk.Label(text_frame, text=label).grid(row=i, column=0, padx=5, pady=2, sticky=tk.W)
                var = tk.StringVar()
                setattr(self, var_name, var)
                ttk.Entry(text_frame, textvariable=var, width=width).grid(row=i, column=1, padx=5, pady=2, sticky=tk.W)

            # 文本包操作按钮
            text_button_frame = ttk.Frame(text_frame)
            text_button_frame.grid(row=5, column=0, columnspan=2, pady=5)

            self.add_update_text_btn = ttk.Button(text_button_frame, text="添加文本包", command=self.add_or_update_text_packet)
            self.add_update_text_btn.grid(row=0, column=0, padx=5)

            ttk.Button(text_button_frame, text="清除选择", command=self.clear_text_selection).grid(row=0, column=1, padx=5)

            # 文本包列表
            columns = ("类型", "样式字体", "X", "Y", "文本")
            self.text_tree = ttk.Treeview(text_frame, columns=columns, show="headings", height=6)
            for col in columns:
                self.text_tree.heading(col, text=col)
                self.text_tree.column(col, width=100 if col != "文本" else 200)
            self.text_tree.grid(row=6, column=0, columnspan=2, pady=(5, 0), sticky=(tk.W, tk.E))
            # 绑定选中事件
            self.text_tree.bind('<<TreeviewSelect>>', self.on_text_tree_select)

            ttk.Button(text_frame, text="删除选中文本包", command=self.remove_text_packet).grid(row=7, column=0, columnspan=2, pady=5)

            # 输出文件配置
            output_frame = ttk.Frame(main_frame)
            output_frame.grid(row=2, column=0, sticky=(tk.W, tk.E), pady=(0, 10))
            output_frame.columnconfigure(1, weight=1)

            ttk.Label(output_frame, text="输出文件:").grid(row=0, column=0, padx=5, pady=2, sticky=tk.W)
            self.output_file_var = tk.StringVar(value="output.bin")
            output_entry = ttk.Entry(output_frame, textvariable=self.output_file_var, width=50)
            output_entry.grid(row=0, column=1, padx=5, pady=2, sticky=(tk.W, tk.E))
            ttk.Button(output_frame, text="浏览...", command=self.browse_output_file).grid(row=0, column=2, padx=5, pady=2)

            # 按钮区域
            button_frame = ttk.Frame(main_frame)
            button_frame.grid(row=3, column=0, pady=10)

            # 第一行按钮
            ttk.Button(button_frame, text="生成数据包", command=self.generate_packets).grid(row=0, column=0, padx=5)
            ttk.Button(button_frame, text="加载配置", command=self.load_config).grid(row=0, column=1, padx=5)
            ttk.Button(button_frame, text="保存配置", command=self.save_config).grid(row=0, column=2, padx=5)
            ttk.Button(button_frame, text="打开BIN文件", command=self.open_bin_file).grid(row=0, column=3, padx=5)
            ttk.Button(button_frame, text="退出", command=self.root.quit).grid(row=0, column=4, padx=5)

            # 第二行按钮
            self.save_as_btn = ttk.Button(button_frame, text="另存为", command=self.save_as, state="disabled")
            self.save_as_btn.grid(row=1, column=0, padx=5, pady=(5, 0))

            self.overwrite_btn = ttk.Button(button_frame, text="覆盖保存", command=self.overwrite_save, state="disabled")
            self.overwrite_btn.grid(row=1, column=1, padx=5, pady=(5, 0))

            self.extract_font_btn = ttk.Button(button_frame, text="提取选中字体", command=self.extract_selected_font, state="disabled")
            self.extract_font_btn.grid(row=1, column=2, padx=5, pady=(5, 0))

            self.replace_font_btn = ttk.Button(button_frame, text="替换选中字体", command=self.replace_selected_font, state="disabled")
            self.replace_font_btn.grid(row=1, column=3, padx=5, pady=(5, 0))

        def show_bdf_info(self, bdf_path: str):
            """显示BDF文件信息"""
            try:
                basename = os.path.basename(bdf_path)
                font_name = get_font_name_from_bdf(bdf_path)
                messagebox.showinfo("BDF字体信息",
                    f"已选择BDF字体文件:\n"
                    f"文件: {basename}\n"
                    f"自动生成字体名称: {font_name}\n\n"
                    f"注意：BDF字体将在生成数据包时自动转换为二进制格式。\n"
                    f"转换将包含ASCII字符32-127，并根据文本数据包内容添加所需字符。")
            except Exception as e:
                messagebox.showwarning("信息", f"已选择BDF字体文件: {os.path.basename(bdf_path)}")

        def browse_font_file(self):
            filename = filedialog.askopenfilename(
                title="选择字体文件",
                filetypes=[("BDF字体文件", "*.bdf"), ("二进制字体文件", "*.bin"), ("所有文件", "*.*")]
            )
            if filename:
                self.font_path_var.set(filename)
                # 如果选择的是BDF文件，显示提示
                if filename.lower().endswith('.bdf'):
                    self.show_bdf_info(filename)

        def browse_output_file(self):
            filename = filedialog.asksaveasfilename(
                title="选择输出文件",
                defaultextension=".bin",
                filetypes=[("二进制文件", "*.bin"), ("所有文件", "*.*")]
            )
            if filename:
                self.output_file_var.set(filename)


        def remove_font(self):
            selection = self.font_tree.selection()
            if not selection:
                messagebox.showwarning("警告", "请选择要删除的字体")
                return

            # 检查是否删除当前选中的字体
            selected_indices = []
            for item in selection:
                idx = int(self.font_tree.item(item)['values'][0])
                if idx in self.font_files:
                    del self.font_files[idx]
                    selected_indices.append(idx)

            # 如果删除的字体包含当前选中的字体，清除选择
            if self.selected_font_index in selected_indices:
                self.clear_font_selection()

            self.update_font_tree()
            self.update_font_id_combo()

        def update_font_tree(self):
            # 清空树
            for item in self.font_tree.get_children():
                self.font_tree.delete(item)

            # 添加数据
            for idx, path in sorted(self.font_files.items()):
                # 如果是BDF文件，在路径中显示标识
                display_path = path
                if isinstance(path, str) and path.lower().endswith('.bdf'):
                    basename = os.path.basename(path)
                    display_path = f"{basename} (BDF格式)"

                self.font_tree.insert("", "end", values=(idx, display_path))

            # 更新文本包字体选择下拉框
            self.update_font_selection_combo()


        def remove_text_packet(self):
            selection = self.text_tree.selection()
            if not selection:
                messagebox.showwarning("警告", "请选择要删除的文本包")
                return

            # 检查是否删除当前选中的文本包
            selected_indices = []
            for item in selection:
                selected_indices.append(self.text_tree.index(item))

            # 从后往前删除，避免索引问题
            for index in sorted(selected_indices, reverse=True):
                if 0 <= index < len(self.text_packets):
                    del self.text_packets[index]

            # 如果删除的文本包包含当前选中的文本包，清除选择
            if self.selected_text_index in selected_indices:
                self.clear_text_selection()
            # 如果选中的索引在删除后发生变化（因为索引移动），需要重新计算
            elif self.selected_text_index is not None:
                # 计算删除后新索引位置（由于从后往前删除，选中的索引可能改变）
                deleted_before = sum(1 for idx in selected_indices if idx < self.selected_text_index)
                self.selected_text_index -= deleted_before
                if self.selected_text_index < 0:
                    self.selected_text_index = None

            self.update_text_tree()
            self.update_text_type_combo()

        def update_text_tree(self):
            # 清空树
            for item in self.text_tree.get_children():
                self.text_tree.delete(item)

            # 添加数据
            for ptype, style_font, x, y, text in self.text_packets:
                self.text_tree.insert("", "end", values=(
                    f"0x{ptype:02x}",
                    f"0x{style_font:02x}",
                    str(x),
                    str(y),
                    text[:50] + "..." if len(text) > 50 else text
                ))

        def generate_packets(self):
            if not self.font_files and not self.text_packets:
                messagebox.showwarning("警告", "请至少配置一个字体文件或文本数据包")
                return

            output_file = self.output_file_var.get().strip()
            if not output_file:
                messagebox.showwarning("警告", "请指定输出文件")
                return

            try:
                create_packets(self.font_files, self.text_packets, output_file)
                messagebox.showinfo("成功", f"数据包文件已生成: {output_file}")
            except Exception as e:
                messagebox.showerror("错误", f"生成数据包失败: {e}")

        def load_config(self):
            filename = filedialog.askopenfilename(
                title="选择配置文件",
                filetypes=[("JSON文件", "*.json"), ("所有文件", "*.*")]
            )
            if not filename:
                return

            try:
                font_files, text_packets, output_file = load_config_from_json(filename)
                self.font_files = font_files
                self.text_packets = text_packets
                self.output_file_var.set(output_file)

                self.update_font_tree()
                self.update_text_tree()

                messagebox.showinfo("成功", f"配置已从 '{filename}' 加载")
            except Exception as e:
                messagebox.showerror("错误", f"加载配置失败: {e}")

        def save_config(self):
            filename = filedialog.asksaveasfilename(
                title="保存配置文件",
                defaultextension=".json",
                filetypes=[("JSON文件", "*.json"), ("所有文件", "*.*")]
            )
            if not filename:
                return

            try:
                save_config_to_json(
                    filename,
                    self.font_files,
                    self.text_packets,
                    self.output_file_var.get()
                )
                messagebox.showinfo("成功", f"配置已保存到 '{filename}'")
            except Exception as e:
                messagebox.showerror("错误", f"保存配置失败: {e}")

        def open_bin_file(self):
            """打开并解析BIN文件，加载配置到界面"""
            filename = filedialog.askopenfilename(
                title="选择BIN文件",
                filetypes=[("二进制文件", "*.bin"), ("所有文件", "*.*")]
            )
            if not filename:
                return

            try:
                # 解析BIN文件
                font_contents, text_packets_data, metadata = parse_bin_file(filename)

                # 将字体内容保存到临时文件
                import tempfile
                temp_dir = tempfile.mkdtemp()
                self.temp_dir = temp_dir  # 保存临时目录引用，用于后续清理

                font_files = {}
                for idx, content in font_contents.items():
                    temp_font_file = os.path.join(temp_dir, f"font_{idx}.bin")
                    with open(temp_font_file, 'wb') as f:
                        f.write(content)
                    font_files[idx] = temp_font_file

                # 更新界面数据
                self.font_files = font_files
                self.text_packets = text_packets_data
                self.current_bin_file = filename
                self.output_file_var.set(filename)  # 默认输出到原文件

                # 更新界面显示
                self.update_font_tree()
                self.update_text_tree()

                # 启用相关按钮
                self.save_as_btn.config(state="normal")
                self.overwrite_btn.config(state="normal")
                self.extract_font_btn.config(state="normal")
                self.replace_font_btn.config(state="normal")

                messagebox.showinfo("成功", f"BIN文件已加载: {filename}\n"
                                          f"字体数量: {metadata['font_count']}\n"
                                          f"文本包数量: {metadata['text_count']}")
            except Exception as e:
                messagebox.showerror("错误", f"加载BIN文件失败: {e}")
                # 清理临时目录（如果创建了）
                if hasattr(self, 'temp_dir'):
                    import shutil
                    shutil.rmtree(self.temp_dir, ignore_errors=True)

        def save_as(self):
            """另存为BIN文件"""
            filename = filedialog.asksaveasfilename(
                title="另存为BIN文件",
                defaultextension=".bin",
                filetypes=[("二进制文件", "*.bin"), ("所有文件", "*.*")]
            )
            if not filename:
                return

            try:
                create_packets(self.font_files, self.text_packets, filename)
                messagebox.showinfo("成功", f"BIN文件已保存: {filename}")
                self.current_bin_file = filename
                self.output_file_var.set(filename)
            except Exception as e:
                messagebox.showerror("错误", f"保存BIN文件失败: {e}")

        def overwrite_save(self):
            """覆盖保存当前BIN文件"""
            if not self.current_bin_file:
                messagebox.showwarning("警告", "没有打开的BIN文件")
                return

            if not messagebox.askyesno("确认", f"确定要覆盖文件 {self.current_bin_file} 吗？"):
                return

            try:
                create_packets(self.font_files, self.text_packets, self.current_bin_file)
                messagebox.showinfo("成功", f"BIN文件已覆盖保存: {self.current_bin_file}")
            except Exception as e:
                messagebox.showerror("错误", f"覆盖保存失败: {e}")

        def extract_selected_font(self):
            """提取选中的字体到文件"""
            selection = self.font_tree.selection()
            if not selection:
                messagebox.showwarning("警告", "请选择要提取的字体")
                return

            for item in selection:
                values = self.font_tree.item(item)['values']
                if not values:
                    continue
                font_index = int(values[0])

                # 询问保存路径
                filename = filedialog.asksaveasfilename(
                    title=f"保存字体 {font_index}",
                    defaultextension=".bin",
                    filetypes=[("二进制文件", "*.bin"), ("所有文件", "*.*")],
                    initialfile=f"font_{font_index}.bin"
                )
                if not filename:
                    continue

                # 提取字体
                if extract_font_from_bin(self.current_bin_file, font_index, filename):
                    messagebox.showinfo("成功", f"字体 {font_index} 已提取到 {filename}")

        def replace_selected_font(self):
            """替换选中的字体"""
            selection = self.font_tree.selection()
            if not selection:
                messagebox.showwarning("警告", "请选择要替换的字体")
                return

            if len(selection) > 1:
                messagebox.showwarning("警告", "请一次只选择一个字体进行替换")
                return

            item = selection[0]
            values = self.font_tree.item(item)['values']
            if not values:
                return

            font_index = int(values[0])

            # 选择新字体文件
            new_font_file = filedialog.askopenfilename(
                title=f"选择新字体文件（替换字体 {font_index}）",
                filetypes=[("二进制文件", "*.bin"), ("所有文件", "*.*")]
            )
            if not new_font_file:
                return

            # 选择输出文件（默认覆盖原文件）
            output_file = filedialog.asksaveasfilename(
                title="保存替换后的BIN文件",
                defaultextension=".bin",
                filetypes=[("二进制文件", "*.bin"), ("所有文件", "*.*")],
                initialfile=os.path.basename(self.current_bin_file) if self.current_bin_file else "output.bin"
            )
            if not output_file:
                return

            # 替换字体
            if replace_font_in_bin(self.current_bin_file, font_index, new_font_file, output_file):
                messagebox.showinfo("成功", f"字体 {font_index} 已替换，新文件保存为 {output_file}")
                # 重新加载新文件
                self.current_bin_file = output_file
                self.output_file_var.set(output_file)

    # 创建并运行GUI
    root = tk.Tk()
    app = ScreenPackerGUI(root)
    root.mainloop()

# ============================================================================
# 主函数
# ============================================================================

def main():
    """
    主函数：根据参数选择运行模式
    """
    args = parse_command_line()

    if args.gui or (not args.config and not args.fonts and not args.texts):
        # 如果指定了--gui，或者没有任何配置参数，则启动GUI
        run_gui()
    else:
        # 否则使用命令行模式
        run_from_command_line(args)

if __name__ == "__main__":
    main()