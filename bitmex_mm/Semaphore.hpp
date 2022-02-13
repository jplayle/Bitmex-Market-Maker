#pragma once

#include <mutex>
#include <condition_variable>


struct Semaphore {
	
	void _get_lock(std::mutex& async_write_mutex, std::condition_variable& write_cv, bool& got_lock)
	{
		std::unique_lock<std::mutex> lck(async_write_mutex);
		write_cv.wait(lck, [&]{ return got_lock; });
		got_lock = false;
	}
	
	
	bool _try_lock(std::mutex& async_write_mutex, std::condition_variable& write_cv, bool& got_lock)
	{
		std::unique_lock<std::mutex> lck(async_write_mutex, std::try_to_lock);
		if (lck.owns_lock())
		{
			if (got_lock)
			{
				got_lock = false;
				return true;
			}
		}
		return false;
	}
	
	
	void _unlock(std::mutex& async_write_mutex, std::condition_variable& write_cv, bool& got_lock)
	{
		{
			std::unique_lock<std::mutex> lck(async_write_mutex);
			got_lock = true;
		}
		write_cv.notify_one();
	}	
};