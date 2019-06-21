#include <Storages/Kafka/ReadBufferFromKafkaConsumer.h>

namespace DB
{

using namespace std::chrono_literals;

ReadBufferFromKafkaConsumer::~ReadBufferFromKafkaConsumer()
{
    /// NOTE: see https://github.com/edenhill/librdkafka/issues/2077
    consumer->unsubscribe();
    consumer->unassign();
    while (consumer->get_consumer_queue().next_event(1s));
}

void ReadBufferFromKafkaConsumer::commit()
{
    if (current != messages.end())
    {
        /// Since we can poll more messages than we already processed,
        /// commit only processed messages.
        consumer->async_commit(*current);
    }
    else
    {
        /// Commit everything we polled so far because either:
        /// - read all polled messages (current == messages.end()),
        /// - read nothing at all (messages.empty()),
        /// - stalled.
        consumer->async_commit();
    }

    const auto & offsets = consumer->get_offsets_committed(consumer->get_assignment());
    for (const auto & topic_part : offsets)
    {
        LOG_TRACE(
            log,
            "Committed offset " << topic_part.get_offset() << " (topic: " << topic_part.get_topic()
                                << ", partition: " << topic_part.get_partition() << ")");
    }
}

void ReadBufferFromKafkaConsumer::subscribe(const Names & topics)
{
    // While we wait for an assignment after subscribtion, we'll poll zero messages anyway.
    // If we're doing a manual select then it's better to get something after a wait, then immediate nothing.
    if (consumer->get_subscription().empty())
    {
        consumer->pause(); // don't accidentally read any messages
        consumer->subscribe(topics);
        consumer->poll(5s);
        consumer->resume();
    }

    stalled = false;
}

void ReadBufferFromKafkaConsumer::unsubscribe()
{
    LOG_TRACE(log, "Re-joining claimed consumer after failure");
    consumer->unsubscribe();
}

/// Try to commit messages implicitly after we processed the previous batch.
bool ReadBufferFromKafkaConsumer::nextImpl()
{
    /// NOTE: ReadBuffer was implemented with an immutable underlying contents in mind.
    ///       If we failed to poll any message once - don't try again.
    ///       Otherwise, the |poll_timeout| expectations get flawn.
    if (stalled)
        return false;

    if (current == messages.end())
    {
        if (intermediate_commit)
            commit();

        /// Don't drop old messages immediately, since we may need them for virtual columns.
        auto new_messages = consumer->poll_batch(batch_size, std::chrono::milliseconds(poll_timeout));
        if (new_messages.empty())
        {
            LOG_TRACE(log, "Stalled");
            stalled = true;
            return false;
        }
        messages = std::move(new_messages);
        current = messages.begin();

        LOG_TRACE(log, "Polled batch of " << messages.size() << " messages");
    }

    if (auto err = current->get_error())
    {
        ++current;

        // TODO: should throw exception instead
        LOG_ERROR(log, "Consumer error: " << err);
        return false;
    }

    // XXX: very fishy place with const casting.
    auto new_position = reinterpret_cast<char *>(const_cast<unsigned char *>(current->get_payload().get_data()));
    BufferBase::set(new_position, current->get_payload().get_size(), 0);

    ++current;

    return true;
}

}
