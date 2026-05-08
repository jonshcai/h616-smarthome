/* inih -- simple .INI file parser
   SPDX-License-Identifier: BSD-3-Clause
   Copyright (C) 2009-2020, Ben Hoyt
*/

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include "ini.h"

// 如果不使用栈内存分配，则包含 stdlib.h（通常用于动态内存分配）
#if !INI_USE_STACK
#include <stdlib.h>
#endif

// 定义最大节(section)名长度
#define MAX_SECTION 50
// 定义最大键(name)名长度
#define MAX_NAME 50

/**
 * INI 文件解析函数
 * 
 * @param filename  INI 文件名
 * @param handler   回调函数指针，用于处理每个解析到的键值对
 * @param user      用户自定义数据，会传递给回调函数
 * @return          成功返回0，失败返回错误行号
 */
int ini_parse(const char *filename,
              int (*handler)(void *user, const char *section,
                             const char *name, const char *value),
              void *user)
{
    FILE *file;              // 文件指针
    char line[200];          // 存储每行内容
    char section[MAX_SECTION] = "";  // 当前节名
    char prev_name[MAX_NAME] = "";   // 上一个键名（用于去重）
    char *start;             // 行内容的起始位置
    char *end;               // 字符串结束位置
    char *name;              // 键名指针
    char *value;             // 值指针
    int lineno = 0;          // 当前行号
    int error = 0;           // 错误行号（0表示无错误）
    
    // 打开文件（只读模式）
    file = fopen(filename, "r");
    if (!file)
        return -1;  // 文件打开失败
    
    // 逐行读取文件
    while (fgets(line, sizeof(line), file)) {
        lineno++;
        start = line;
        
        // 跳过行首的空白字符（空格、制表符等）
        while (isspace((unsigned char)*start))
            start++;
        
        // 空行或注释行（以 ; 或 # 开头）跳过
        if (*start == '\0' || *start == ';' || *start == '#')
            continue;
        
        // 处理节定义：[section_name]
        if (*start == '[') {
            // 查找匹配的右括号 ]
            end = strchr(start, ']');
            if (!end) {
                error = lineno;  // 没有找到右括号，语法错误
                break;
            }
            *end = '\0';  // 将 ] 替换为字符串结束符
            // 提取节名（去掉左括号）
            strncpy(section, start + 1, sizeof(section));
            section[sizeof(section) - 1] = '\0';  // 确保字符串以 \0 结尾
            prev_name[0] = '\0';  // 重置上一个键名（新节开始）
        } 
        // 处理键值对：name = value
        else {
            // 查找等号 =
            end = strchr(start, '=');
            if (!end) {
                error = lineno;  // 没有找到等号，语法错误
                break;
            }
            *end = '\0';  // 将 = 替换为字符串结束符，分割键名和值
            name = start;
            
            // 去除键名前的空白字符
            while (isspace((unsigned char)*name))
                name++;
            
            // 值从等号后开始
            value = end + 1;
            // 去除值前的空白字符
            while (isspace((unsigned char)*value))
                value++;
            
            // 去除值后的空白字符
            end = value + strlen(value) - 1;
            while (end > value && isspace((unsigned char)*end))
                end--;
            *(end + 1) = '\0';  // 在值结尾添加字符串结束符
            
            // 检查是否与上一个键名重复（处理多行值的情况）
            if (strcmp(prev_name, name) != 0) {
                // 调用用户提供的回调函数处理这个键值对
                // 如果回调函数返回0，则停止解析并记录错误行号
                if (!handler(user, section, name, value))
                    error = lineno;
                // 保存当前键名，用于下一行的重复检查
                strncpy(prev_name, name, sizeof(prev_name));
                prev_name[sizeof(prev_name) - 1] = '\0';
            }
            // 如果键名重复（多行值），则跳过，继续读取下一行
        }
    }
    
    // 关闭文件
    fclose(file);
    // 返回错误行号（0表示成功，-1表示文件打开失败）
    return error;
}