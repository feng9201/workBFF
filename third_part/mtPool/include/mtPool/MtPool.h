#pragma once
/*****
* @brief: 进程级单例线程池，自包含 BS::thread_pool v5.1.0 (MIT)，不依赖 vcpkg mttool
* @auth:  hxf
*
*   #include <mtPool/MtPool.h>
*
*   // 异步任务（不关心结果）
*   mtPool::pool().detach_task([] { doWork(); });
*
*   // 需要返回值
*   auto fut = mtPool::pool().submit([] { return 42; });
*   int v = fut.get();
*
*   // 等待全部任务完成（勿在线程池工作线程内调用，会死锁）
*   mtPool::pool().wait_for_tasks();
*
*   // 原生接口全部可用：pause/unpause/reset(n)/get_thread_count/get_tasks_queued ...
*
* 共享语义：
*   - DLL 模式（CMake: MTPOOL_BUILD_SHARED=ON）：进程内 EXE + 所有 DLL 共享同一个线程池
*   - 静态模式（默认）：每个链接它的 EXE/DLL 内各自持有一份副本，进程内 EXE 与 DLL 之间不共享
*   - 进程之间天然隔离（线程池是进程内资源）
******/
#include <mtPool/mt_thread_pool.h>

// DLL 模式：导出/导入 pool()；静态模式：宏为空，符号随静态库链接
#if defined(MTPOOL_SHARED)
    #if defined(MTPOOL_BUILDING)
        #define MTPOOL_API __declspec(dllexport)
    #else
        #define MTPOOL_API __declspec(dllimport)
    #endif
#else
    #define MTPOOL_API
#endif

namespace mtPool
{
	// 进程级唯一线程池，首次调用时惰性创建（线程安全）
	// 默认线程数 = 硬件并发数；如需调整：mtPool::pool().reset(n)（会等待现有任务完成）
	MTPOOL_API BS::thread_pool<>& pool();
}
