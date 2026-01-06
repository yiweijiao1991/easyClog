/*
 * Copyright (c) 2026 yiweijiao
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * 作者：yiweijiao
 * 功能：日志模块头文件（支持同步/异步模式、分级过滤、异步刷盘）
 */
#ifndef  __LOG_H__
#define  __LOG_H__

#include <stdio.h>
#include <stdint.h>

// ========== 1. 新增：日志模式枚举 ==========
typedef enum {
    LOG_MODE_SYNC,    // 同步模式：直接写入文件，无缓冲区/刷盘线程
    LOG_MODE_ASYNC    // 异步模式：缓冲区+刷盘线程（默认）
} LOG_MODE;

// ========== 2. 日志级别枚举（保留） ==========
typedef enum {
    LOG_LEVEL_DEBUG = 0,    // 最低级，输出所有日志
    LOG_LEVEL_INFO,         // 输出INFO/WARNING/ERROR
    LOG_LEVEL_WARNING,      // 输出WARNING/ERROR
    LOG_LEVEL_ERROR         // 最高级，仅输出ERROR
} LOG_LEVEL;

// ========== 3. 缓冲区配置（仅异步模式生效） ==========
#define LOG_BUFFER_MAX_SIZE 1024  // 异步缓冲区最大容量
#define LOG_ENTRY_MAX_LEN 2048    // 单条日志最大长度

// ========== 4. 日志条目结构体（仅异步模式生效） ==========
typedef struct {
    LOG_LEVEL level;                // 日志级别（分级过滤）
    char content[LOG_ENTRY_MAX_LEN];// 完整日志内容
    uint8_t used;                   // 缓冲区节点是否占用：0-空闲，1-占用
} LogEntry;

// ========== 5. 对外接口 ==========
// 初始化日志模块（指定目录，默认异步模式+DEBUG级别）
void logInit(char *rootPath);
// 设置日志模式（同步/异步）
void logSetMode(LOG_MODE mode);
// 设置日志级别（分级过滤）
void logSetLevel(LOG_LEVEL level);
// 销毁日志模块（异步模式下刷盘剩余日志）
void logDeinit(void);
// 核心日志写入接口（内部用，外部调用宏）
int logWrite(LOG_LEVEL level, const char *file, const char*func, int line, const char *format, ...);

// ========== 6. 日志宏（保留） ==========
#define LOG_DEBUG(...)  logWrite(LOG_LEVEL_DEBUG, __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...)   logWrite(LOG_LEVEL_INFO,  __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)
#define LOG_WARNING(...) logWrite(LOG_LEVEL_WARNING, __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...)  logWrite(LOG_LEVEL_ERROR, __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)

#endif