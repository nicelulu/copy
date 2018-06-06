#include "SystemLogsQueue.h"
#include <DataTypes/DataTypeDateTime.h>
#include <DataTypes/DataTypeString.h>
#include <DataTypes/DataTypeEnum.h>
#include <DataTypes/DataTypesNumber.h>

#include <Poco/Message.h>


namespace DB
{

SystemLogsQueue::SystemLogsQueue()
		: ConcurrentBoundedQueue<MutableColumns>(std::numeric_limits<int>::max()),
		  max_priority(Poco::Message::Priority::PRIO_INFORMATION) {}


Block SystemLogsQueue::getSampleBlock()
{
    return Block {
            {std::make_shared<DataTypeDateTime>(), "event_time"},
            {std::make_shared<DataTypeUInt32>(), "event_time_microseconds"},
            {std::make_shared<DataTypeUInt32>(), "thread_number"},
            {std::make_shared<DataTypeInt8>(), "priority"},
            {std::make_shared<DataTypeString>(), "source"},
            {std::make_shared<DataTypeString>(), "text"},
    };
}

MutableColumns SystemLogsQueue::getSampleColumns()
{
    static Block sample_block = getSampleBlock();
    return sample_block.cloneEmptyColumns();
}

const char * SystemLogsQueue::getProrityName(int priority)
{
    /// See Poco::Message::Priority

    static const char * PRIORITIES [] = {
        "Unknown",
		"Fatal",
		"Critical",
		"Error",
		"Warning",
		"Notice",
		"Information",
		"Debug",
		"Trace"
    };

    return (priority >= 1 && priority <= 8) ? PRIORITIES[priority] : PRIORITIES[0];
}

}
