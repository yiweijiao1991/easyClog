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
 * 功能：日志模块实现文件（支持同步/异步模式、分级过滤、异步刷盘）
 */
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <string.h>
#include <stdarg.h>
#include <execinfo.h>
#include <errno.h>

#include "EClog.h"

// ========== 全局变量 ==========
static char rootPath[256] = {0};                // 日志根目录
static LOG_MODE g_log_mode = LOG_MODE_ASYNC;    // 当前日志模式（默认异步）
static LOG_LEVEL g_log_filter_level = LOG_LEVEL_DEBUG; // 过滤级别

// 异步模式专属变量
static pthread_t log_flush_thread;    // 刷盘线程ID
static int log_running = 1;           // 刷盘线程运行标志
static LogEntry log_buffer[LOG_BUFFER_MAX_SIZE];// 环形缓冲区
static int buffer_write_idx = 0;      // 写索引（生产者）
static int buffer_read_idx = 0;       // 读索引（消费者）

// 通用锁（同步模式：保护文件写入；异步模式：保护缓冲区+模式切换）
static pthread_mutex_t log_mutex;
static pthread_cond_t buffer_cond;    // 异步模式条件变量

// ========== 内部辅助函数 ==========
// 日志级别转字符串
static const char* logLevelToString(LOG_LEVEL level) {
    switch (level) {
        case LOG_LEVEL_DEBUG:    return "DEBUG";
        case LOG_LEVEL_INFO:     return "INFO";
        case LOG_LEVEL_WARNING:  return "WARNING";
        case LOG_LEVEL_ERROR:    return "ERROR";
        default:                 return "UNKNOWN";
    }
}

// 创建日志目录（复用）
static int createDir(char *fileName) {
    if ((fileName == NULL) || (strlen(fileName) == 0)) return -1;
    char *tag = NULL;
    char buf[1000], path[1000];
    int flag = 0, ret;

    for (tag = fileName; *tag; tag++) {
#ifdef WIN32
        if (*tag == '\\')
#else
        if (*tag == '/')
#endif
        {
            memset(buf, 0, sizeof(buf));
            memset(path, 0, sizeof(path));
            strncpy(buf, fileName, sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
            buf[strlen(fileName) - strlen(tag) + 1] = '\0';
            strncpy(path, buf, sizeof(path) - 1);
            path[sizeof(path) - 1] = '\0';

            if (access(path, F_OK) == -1) {
#ifdef WIN32
                ret = mkdir(path);
#else
                ret = mkdir(path, S_IRWXU);
#endif
                if (ret < 0) {
                    fprintf(stderr, "创建目录失败 %s, errno: %d\n", path, errno);
                    return -1;
                } else flag = 1;
            }
        }
    }
    return flag ? 0 : 1;
}

// 获取日志文件句柄（复用）
static FILE *get_file_fd(char *file_name) {
    static char prev_file_name[2048] = {0};
    static FILE *fd = NULL;

    if (0 == strcmp(prev_file_name, file_name)) {
        if (NULL == fd) {
            fd = fopen(file_name, "ab+");
            memset(prev_file_name, 0, sizeof(prev_file_name));
            strncpy(prev_file_name, file_name, sizeof(prev_file_name) - 1);
            prev_file_name[sizeof(prev_file_name) - 1] = '\0';
        }
    } else {
        if (NULL != fd) fclose(fd);
        fd = fopen(file_name, "ab+");
        memset(prev_file_name, 0, sizeof(prev_file_name));
        strncpy(prev_file_name, file_name, sizeof(prev_file_name) - 1);
        prev_file_name[sizeof(prev_file_name) - 1] = '\0';
    }
    return fd;
}

// ========== 同步模式直接写文件 ==========
static int logWriteDirect(LOG_LEVEL level, const char *content) {
    // 分级过滤：仅写入≥过滤级别的日志
    if (level < g_log_filter_level) return 0;

    // 拼接日志文件名（按日期）
    char chFileName[1024] = {0};
    time_t timep;
    struct tm tmp;
    time(&timep);
    localtime_r(&timep, &tmp);
    snprintf(chFileName, sizeof(chFileName) - 1,
             "%s%d-%02d-%02d.txt",
             rootPath, (1900 + tmp.tm_year), (tmp.tm_mon + 1), tmp.tm_mday);
    chFileName[sizeof(chFileName) - 1] = '\0';

    // 创建目录
    int ret = createDir(chFileName);
    if (ret == -1) {
        fprintf(stderr, "创建日志目录失败，跳过写入: %s\n", chFileName);
        return -1;
    }

    // 写入文件（加锁保证线程安全）
    FILE *fd = get_file_fd(chFileName);
    if (fd != NULL) {
        fputs(content, fd);
        fflush(fd); // 同步模式强制刷盘
        return 0;
    } else {
        fprintf(stderr, "打开日志文件失败: %s\n", chFileName);
        return -1;
    }
}

// ========== 异步模式：刷盘线程函数 ==========
static void *log_flush_thread_func(void *arg) {
    while (log_running) {
        pthread_mutex_lock(&log_mutex);

        // 防止虚假唤醒 + 检查退出标志
        while (buffer_read_idx == buffer_write_idx && log_running) {
            pthread_cond_wait(&buffer_cond, &log_mutex);
        }

        // 退出线程：刷完剩余日志后退出（先解锁！）
        if (!log_running) {
            // 刷完剩余日志
            while (buffer_read_idx != buffer_write_idx) {
                LogEntry *entry = &log_buffer[buffer_read_idx];
                if (entry->used) {
                    logWriteDirect(entry->level, entry->content);
                    entry->used = 0;
                    buffer_read_idx = (buffer_read_idx + 1) % LOG_BUFFER_MAX_SIZE;
                }
            }
            pthread_mutex_unlock(&log_mutex); // 必须先解锁再退出
            break;
        }

        // 读取缓冲区日志
        LogEntry *entry = &log_buffer[buffer_read_idx];
        if (entry->used) {
            // 调用同步写文件函数（复用逻辑）
            logWriteDirect(entry->level, entry->content);
            
            // 释放缓冲区节点
            entry->used = 0;
            buffer_read_idx = (buffer_read_idx + 1) % LOG_BUFFER_MAX_SIZE;
        }

        pthread_mutex_unlock(&log_mutex);
    }
    return NULL;
}

// ========== 对外接口实现 ==========
// 1. 日志初始化
void logInit(char *path) {
    // 初始化日志目录
    if (path != NULL && strlen(path) < sizeof(rootPath) - 1) {
        memset(rootPath, 0, sizeof(rootPath));
        strncpy(rootPath, path, sizeof(rootPath) - 1);
        rootPath[sizeof(rootPath) - 1] = '\0';
    }

    // 初始化通用锁
    pthread_mutex_init(&log_mutex, NULL);
    pthread_cond_init(&buffer_cond, NULL);
    memset(log_buffer, 0, sizeof(log_buffer));

    // 默认异步模式：创建刷盘线程
    if (g_log_mode == LOG_MODE_ASYNC) {
        if (pthread_create(&log_flush_thread, NULL, log_flush_thread_func, NULL) != 0) {
            fprintf(stderr, "创建刷盘线程失败, errno: %d\n", errno);
            exit(EXIT_FAILURE);
        }
    }
}

// 2. 设置日志模式（同步/异步）【核心修复】
void logSetMode(LOG_MODE mode) {
    pthread_mutex_lock(&log_mutex); // 加锁保证线程安全

    if (g_log_mode == mode) {
        pthread_mutex_unlock(&log_mutex);
        printf("日志模式已为 %s，无需切换\n", mode == LOG_MODE_SYNC ? "同步" : "异步");
        return;
    }

    int need_join = 0; // 标记是否需要等待刷盘线程退出
    // 异步 → 同步：先停止刷盘线程（仅设置标志+唤醒，不在这里join）
    if (g_log_mode == LOG_MODE_ASYNC && mode == LOG_MODE_SYNC) {
        log_running = 0;
        pthread_cond_signal(&buffer_cond); // 唤醒刷盘线程
        need_join = 1;
        printf("异步模式切同步：已唤醒刷盘线程刷剩余日志\n");
    }
    // 同步 → 异步：创建刷盘线程
    else if (g_log_mode == LOG_MODE_SYNC && mode == LOG_MODE_ASYNC) {
        log_running = 1;
        memset(log_buffer, 0, sizeof(log_buffer));
        buffer_write_idx = 0;
        buffer_read_idx = 0;
        if (pthread_create(&log_flush_thread, NULL, log_flush_thread_func, NULL) != 0) {
            fprintf(stderr, "创建刷盘线程失败, errno: %d\n", errno);
            pthread_mutex_unlock(&log_mutex);
            exit(EXIT_FAILURE);
        }
        printf("同步模式切异步：已创建刷盘线程\n");
    }

    // 更新当前模式
    g_log_mode = mode;
    pthread_mutex_unlock(&log_mutex); // 先解锁！再join

    // 异步切同步时，等待刷盘线程退出（此时锁已释放，刷盘线程能正常执行）
    if (need_join) {
        pthread_join(log_flush_thread, NULL);
        printf("异步模式切同步：刷盘线程已退出\n");
    }

    printf("日志模式已切换为：%s\n", mode == LOG_MODE_SYNC ? "同步" : "异步");
}

// 3. 设置日志级别（保留）
void logSetLevel(LOG_LEVEL level) {
    pthread_mutex_lock(&log_mutex);
    g_log_filter_level = level;
    pthread_mutex_unlock(&log_mutex);
    printf("日志级别已设置为：%s\n", logLevelToString(level));
}

// 4. 核心日志写入（分支处理同步/异步）
int logWrite(LOG_LEVEL level, const char *file, const char*func, int line, const char *format, ...) {
    if (format == NULL || (!log_running && g_log_mode == LOG_MODE_ASYNC)) return -1;

    // 步骤1：格式化完整日志内容
    char logContent[LOG_ENTRY_MAX_LEN - 128] = {0};
    va_list args;
    va_start(args, format);
    vsnprintf(logContent, sizeof(logContent) - 1, format, args);
    logContent[sizeof(logContent) - 1] = '\0';
    va_end(args);

    // 拼接时间+级别+位置+内容
    char chContent[LOG_ENTRY_MAX_LEN] = {0};
    struct timeval tv;
    struct tm tmp;
    time_t timep;
    time(&timep);
    localtime_r(&timep, &tmp);
    gettimeofday(&tv, NULL);
    snprintf(chContent, sizeof(chContent) - 1,
            "%02d:%02d:%02d.%06d    [%s]    [%s:%s:%d]    %s",
            tmp.tm_hour, tmp.tm_min, tmp.tm_sec, (int)tv.tv_usec,
            logLevelToString(level), file, func, line, logContent);
    chContent[sizeof(chContent) - 1] = '\0';

    // 步骤2：分支处理同步/异步
    if (g_log_mode == LOG_MODE_SYNC) {
        // 同步模式：直接写文件（加锁保证线程安全）
        pthread_mutex_lock(&log_mutex);
        int ret = logWriteDirect(level, chContent);
        pthread_mutex_unlock(&log_mutex);
        
        // 控制台输出（仅≥过滤级别）
        if (level >= g_log_filter_level) printf("%s", chContent);
        return ret;
    } else {
        // 异步模式：写入缓冲区
        pthread_mutex_lock(&log_mutex);

        // 检查缓冲区是否满
        int next_write_idx = (buffer_write_idx + 1) % LOG_BUFFER_MAX_SIZE;
        if (next_write_idx == buffer_read_idx) {
            fprintf(stderr, "异步缓冲区满，丢弃日志：[%s] %s\n", logLevelToString(level), logContent);
            pthread_mutex_unlock(&log_mutex);
            return -1;
        }

        // 写入缓冲区
        LogEntry *entry = &log_buffer[buffer_write_idx];
        entry->level = level;
        strncpy(entry->content, chContent, sizeof(entry->content) - 1);
        entry->content[sizeof(entry->content) - 1] = '\0';
        entry->used = 1;
        buffer_write_idx = next_write_idx;

        // 唤醒刷盘线程
        pthread_cond_signal(&buffer_cond);
        pthread_mutex_unlock(&log_mutex);

        // 控制台输出（仅≥过滤级别）
        if (level >= g_log_filter_level) printf("%s", chContent);
        return 0;
    }
}

// 5. 日志模块销毁
void logDeinit(void) {
    pthread_mutex_lock(&log_mutex);

    // 异步模式：停止刷盘线程，刷完剩余日志
    if (g_log_mode == LOG_MODE_ASYNC) {
        log_running = 0;
        pthread_cond_signal(&buffer_cond);
        pthread_mutex_unlock(&log_mutex);
        pthread_join(log_flush_thread, NULL); // 等待刷盘完成
    } else {
        pthread_mutex_unlock(&log_mutex);
    }

    // 销毁同步资源
    pthread_mutex_destroy(&log_mutex);
    pthread_cond_destroy(&buffer_cond);

    // 关闭日志文件句柄
    FILE *fd = get_file_fd("");
    if (fd != NULL) fclose(fd);

    printf("日志模块已销毁，模式：%s\n", g_log_mode == LOG_MODE_SYNC ? "同步" : "异步");
}