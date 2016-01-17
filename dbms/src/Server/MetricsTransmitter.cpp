#include "MetricsTransmitter.h"

#include <daemon/Daemon.h>
#include <DB/Common/setThreadName.h>


namespace DB
{

MetricsTransmitter::~MetricsTransmitter()
{
	try
	{
		{
			std::lock_guard<std::mutex> lock{mutex};
			quit = true;
		}

		cond.notify_one();

		thread.join();
	}
	catch (...)
	{
		DB::tryLogCurrentException(__FUNCTION__);
	}
}


void MetricsTransmitter::run()
{
	setThreadName("ProfileEventsTx");

	const auto get_next_minute = [] {
		return std::chrono::time_point_cast<std::chrono::minutes, std::chrono::system_clock>(
			std::chrono::system_clock::now() + std::chrono::minutes(1)
		);
	};

	std::unique_lock<std::mutex> lock{mutex};

	while (true)
	{
		if (cond.wait_until(lock, get_next_minute(), [this] { return quit; }))
			break;

		transmitCounters();
	}
}


void MetricsTransmitter::transmitCounters()
{
	GraphiteWriter::KeyValueVector<size_t> key_vals{};
	key_vals.reserve(ProfileEvents::END);

	for (size_t i = 0; i < ProfileEvents::END; ++i)
	{
		const auto counter = ProfileEvents::counters[i];
		const auto counter_increment = counter - prev_counters[i];
		prev_counters[i] = counter;

		std::string key{ProfileEvents::getDescription(static_cast<ProfileEvents::Event>(i))};
		key_vals.emplace_back(event_path_prefix + key, counter_increment);
	}

	Daemon::instance().writeToGraphite(key_vals);
}

}
