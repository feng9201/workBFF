#include <mtPool/MtPool.h>

namespace mtPool
{
	BS::thread_pool<>& pool()
	{
		static BS::thread_pool<> instance;   // C++11 magic static，线程安全
		return instance;
	}
}
