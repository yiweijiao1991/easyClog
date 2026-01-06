#include "EClog.h"
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>

#define THREAD_NUM 2
#define LOG_COUNT_PER_THREAD 20

// 线程函数：模拟多线程写日志
void *thread_log_writer(void *arg) {
    int thread_id = *(int *)arg;
    free(arg);

    for (int i = 0; i < LOG_COUNT_PER_THREAD; i++) {
        LOG_DEBUG("线程%d - 调试日志：%d\n", thread_id, i);
        LOG_INFO("线程%d - 信息日志：%d\n", thread_id, i);
        LOG_WARNING("线程%d - 警告日志：%d\n", thread_id, i);
        LOG_ERROR("线程%d - 错误日志：%d\n", thread_id, i);
        usleep(1000);
    }

    printf("线程%d 日志写入完成\n", thread_id);
    pthread_exit(NULL);
}

int main() {
    // ========== 1. 初始化日志（默认异步+DEBUG级别） ==========
    printf("===== 1. 初始化日志模块（/tmp/myapp_log/） =====\n");
    logInit("/tmp/myapp_log/");

    // ========== 2. 测试异步模式（默认） ==========
    printf("\n===== 2. 测试异步模式（DEBUG级别） =====\n");
    LOG_DEBUG("主线程 - 异步DEBUG日志\n");
    LOG_ERROR("主线程 - 异步ERROR日志\n");

    // 创建线程写日志（验证异步线程安全）
    pthread_t threads[THREAD_NUM];
    for (int i = 0; i < THREAD_NUM; i++) {
        int *tid = malloc(sizeof(int));
        *tid = i + 1;
        pthread_create(&threads[i], NULL, thread_log_writer, tid);
    }
    for (int i = 0; i < THREAD_NUM; i++) pthread_join(threads[i], NULL);

    // ========== 3. 切换为同步模式 ==========
    printf("\n===== 3. 切换为同步模式 =====\n");
    logSetMode(LOG_MODE_SYNC);

    // 测试同步模式（设置ERROR级别）
    printf("\n===== 4. 同步模式 + ERROR级别 =====\n");
    logSetLevel(LOG_LEVEL_ERROR);
    LOG_DEBUG("主线程 - 同步DEBUG日志（应过滤）\n");
    LOG_ERROR("主线程 - 同步ERROR日志（应输出）\n");

    // ========== 4. 切回异步模式 ==========
    printf("\n===== 5. 切回异步模式 + DEBUG级别 =====\n");
    logSetMode(LOG_MODE_ASYNC);
    logSetLevel(LOG_LEVEL_DEBUG);
    LOG_INFO("主线程 - 切回异步INFO日志\n");

    // ========== 5. 销毁日志模块 ==========
    printf("\n===== 6. 销毁日志模块 =====\n");
    logDeinit();

    printf("\n===== 测试完成！日志路径：/tmp/myapp_log/YYYY-MM-DD.txt =====\n");
    return 0;
}