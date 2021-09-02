#pragma once

#include "waitevent.h"

class Barrier
{
public:
	void Wait() &&
	{
		we->Wait([&] { return is_enabled->load(); });
	}

	void Notify() &&
	{
		*is_enabled = true;
		we->WakeupAllWaiters();
	}

private:
	std::shared_ptr<std::atomic<bool>> is_enabled = std::make_shared<std::atomic<bool>>(false);
	std::shared_ptr<lockfree::thread::WaitEvent> we =
		std::make_shared<lockfree::thread::WaitEvent>();
};
