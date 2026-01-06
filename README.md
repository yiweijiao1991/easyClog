# easyClog
easyClog - 轻量级高性能C语言日志库，支持同步/异步模式切换、日志分级过滤、异步刷盘，线程安全易集成

🚀 easyClog - 让C语言日志接入更简单、更高效

核心特性：

✅ 双模切换：同步模式（实时写文件，低并发友好）/ 异步模式（缓冲区+后台刷盘线程，高并发友好）

✅ 分级过滤：DEBUG/INFO/WARNING/ERROR 四级日志，动态调整过滤级别

✅ 线程安全：多线程并发写日志无冲突，互斥锁保护核心逻辑

✅ 轻量无依赖：仅依赖系统pthread库，跨Linux/macOS/Windows(MinGW)

✅ 自动建目录：日志目录不存在时自动创建，无需手动处理

✅ 优雅退出：异步模式销毁前刷完缓冲区日志，避免丢失

✅ 精细时间戳：日志时间精确到微秒，便于问题定位

✅ 按日切分：日志文件自动按 YYYY-MM-DD.txt 命名，无需手动管理

✅ MIT 开源：免费商用、修改、分发，无版权限制

适用场景：各类C语言项目（嵌入式/服务器/工具类），兼顾低并发实时性和高并发性能需求

easyClog is a lightweight and high-performance logging library designed for C language development. It focuses on "simple integration and flexible use", providing core capabilities such as synchronous/asynchronous mode switching, multi-level log filtering, asynchronous disk flushing, and thread safety. With zero extra dependencies (only relying on the system pthread library), easyClog is suitable for various C projects (embedded, server, tool-type) and can adapt to both low-concurrency (real-time logging) and high-concurrency (performance-sensitive) scenarios. It supports automatic directory creation, microsecond-level timestamping, daily log file splitting, and graceful exit (ensuring no log loss in asynchronous mode), and is open-sourced under the MIT license for free commercial use.
