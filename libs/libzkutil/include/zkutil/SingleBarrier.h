#pragma once

#include <zkutil/ZooKeeperHolder.h>
#include <string>
#include <functional>

namespace zkutil
{

/** Single distributed barrier for ZooKeeper.
  */
class SingleBarrier final
{
public:
	using CancellationHook = std::function<void()>;

public:
	SingleBarrier(ZooKeeperPtr zookeeper_, const std::string & path_, size_t counter_);

	SingleBarrier(const SingleBarrier &) = delete;
	SingleBarrier & operator=(const SingleBarrier &) = delete;

	SingleBarrier(SingleBarrier &&) = default;
	SingleBarrier & operator=(SingleBarrier &&) = default;

	/// Register a function that checks whether barrier operation should be cancelled.
	void setCancellationHook(CancellationHook cancellation_hook_);

	void enter(uint64_t timeout = 0);

private:
	void abortIfRequested();

private:
	ZooKeeperPtr zookeeper;
	EventPtr event = new Poco::Event;
	CancellationHook cancellation_hook;
	std::string path;
	size_t counter;
};

}
