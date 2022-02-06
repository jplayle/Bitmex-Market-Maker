#pragma once

#include <mutex>
#include <condition_variable>


struct Semaphore {
	
	void _write_lock(std::mutex& async_write_mutex, std::condition_variable& write_cv, bool& can_write)
	{
		std::unique_lock<std::mutex> lck(async_write_mutex);
		write_cv.wait(lck, [&]{ return can_write; });
		can_write = false;
	}
	
	
	bool _write_try_lock(std::mutex& async_write_mutex, std::condition_variable& write_cv, bool& can_write)
	{
		std::unique_lock<std::mutex> lck(async_write_mutex, std::try_to_lock);
		if (lck.owns_lock())
		{
			if (can_write)
			{
				can_write = false;
				return true;
			}
		}
		return false;
	}
	
	
	void _write_unlock(std::mutex& async_write_mutex, std::condition_variable& write_cv, bool& can_write)
	{
		{
			std::unique_lock<std::mutex> lck(async_write_mutex);
			can_write = true;
		}
		write_cv.notify_one();
	}	
};