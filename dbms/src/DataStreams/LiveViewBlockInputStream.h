/* Copyright (c) 2018 BlackBerry Limited

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at
http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License. */
#pragma once

#include <limits>

#include <Common/ConcurrentBoundedQueue.h>
#include <Poco/Condition.h>
#include <DataStreams/OneBlockInputStream.h>
#include <DataStreams/IBlockInputStream.h>
#include <Storages/StorageLiveView.h>


namespace DB
{

/** Implements LIVE VIEW table WATCH input stream.
 *  Keeps stream alive by outputing blocks with no rows
 *  based on period specified by the heartbeat interval.
 */
class LiveViewBlockInputStream : public IBlockInputStream
{

using NonBlockingResult = std::pair<Block, bool>;

public:
    ~LiveViewBlockInputStream() override
    {
        /// Start storage no users thread
        /// if we are the last active user
        if (!storage->is_dropped && blocks_ptr.use_count() < 3)
            storage->startNoUsersThread(temporary_live_view_timeout_sec);
    }

    LiveViewBlockInputStream(std::shared_ptr<StorageLiveView> storage_,
        std::shared_ptr<BlocksPtr> blocks_ptr_,
        std::shared_ptr<BlocksMetadataPtr> blocks_metadata_ptr_,
        std::shared_ptr<bool> active_ptr_,
        const bool has_limit_, const UInt64 limit_,
        const UInt64 heartbeat_interval_sec_,
        const UInt64 temporary_live_view_timeout_sec_)
        : storage(std::move(storage_)), blocks_ptr(std::move(blocks_ptr_)), blocks_metadata_ptr(std::move(blocks_metadata_ptr_)), active_ptr(std::move(active_ptr_)), has_limit(has_limit_), limit(limit_), heartbeat_interval_usec(heartbeat_interval_sec_ * 1000000), temporary_live_view_timeout_sec(temporary_live_view_timeout_sec_)
    {
        /// grab active pointer
        active = active_ptr.lock();
    }

    String getName() const override { return "LiveViewBlockInputStream"; }

    void cancel(bool kill) override
    {
        if (isCancelled() || storage->is_dropped)
            return;
        IBlockInputStream::cancel(kill);
        Poco::FastMutex::ScopedLock lock(storage->mutex);
        storage->condition.broadcast();
    }

    Block getHeader() const override { return storage->getHeader(); }

    void refresh()
    {
        if (active && blocks && it == end)
            it = blocks->begin();
    }

    void suspend()
    {
        active.reset();
    }

    void resume()
    {
        active = active_ptr.lock();
        {
            if (!blocks || blocks.get() != (*blocks_ptr).get())
                blocks = (*blocks_ptr);
        }
        it = blocks->begin();
        begin = blocks->begin();
        end = blocks->end();
    }

    NonBlockingResult tryRead()
    {
        return tryRead_(false);
    }

protected:
    Block readImpl() override
    {
        /// try reading
        return tryRead_(true).first;
    }

    /** tryRead method attempts to read a block in either blocking
     *  or non-blocking mode. If blocking is set to false
     *  then method return empty block with flag set to false
     *  to indicate that method would block to get the next block.
     */
    NonBlockingResult tryRead_(bool blocking)
    {
        Block res;

        if (has_limit && num_updates == static_cast<Int64>(limit))
        {
            return { Block(), true };
        }
        /// If blocks were never assigned get blocks
        if (!blocks)
        {
            Poco::FastMutex::ScopedLock lock(storage->mutex);
            if (!active)
                return { Block(), false };
            blocks = (*blocks_ptr);
            it = blocks->begin();
            begin = blocks->begin();
            end = blocks->end();
        }

        if (isCancelled() || storage->is_dropped)
        {
            return { Block(), true };
        }

        if (it == end)
        {
            {
                Poco::FastMutex::ScopedLock lock(storage->mutex);
                if (!active)
                    return { Block(), false };
                /// If we are done iterating over our blocks
                /// and there are new blocks availble then get them
                if (blocks.get() != (*blocks_ptr).get())
                {
                    blocks = (*blocks_ptr);
                    it = blocks->begin();
                    begin = blocks->begin();
                    end = blocks->end();
                }
                /// No new blocks available wait for new ones
                else
                {
                    if (!blocking)
                    {
                        return { Block(), false };
                    }
                    if (!end_of_blocks)
                    {
                        end_of_blocks = true;
                        return { getHeader(), true };
                    }
                    while (true)
                    {
                        UInt64 timestamp_usec = static_cast<UInt64>(timestamp.epochMicroseconds());
                        bool signaled = storage->condition.tryWait(storage->mutex, std::max(static_cast<UInt64>(0), heartbeat_interval_usec - (timestamp_usec - last_event_timestamp_usec)) / 1000);

                        if (isCancelled() || storage->is_dropped)
                        {
                            return { Block(), true };
                        }
                        if (signaled)
                        {
                            break;
                        }
                        else
                        {
                            // heartbeat
                            last_event_timestamp_usec = static_cast<UInt64>(timestamp.epochMicroseconds());
                            return { getHeader(), true };
                        }
                    }
                }
            }
            return tryRead_(blocking);
        }

        res = *it;

        ++it;

        if (it == end)
        {
            end_of_blocks = false;
            num_updates += 1;
        }

        last_event_timestamp_usec = static_cast<UInt64>(timestamp.epochMicroseconds());
        return { res, true };
    }

private:
    std::shared_ptr<StorageLiveView> storage;
    std::shared_ptr<BlocksPtr> blocks_ptr;
    std::shared_ptr<BlocksMetadataPtr> blocks_metadata_ptr;
    std::weak_ptr<bool> active_ptr;
    std::shared_ptr<bool> active;
    BlocksPtr blocks;
    BlocksMetadataPtr blocks_metadata;
    Blocks::iterator it;
    Blocks::iterator end;
    Blocks::iterator begin;
    const bool has_limit;
    const UInt64 limit;
    Int64 num_updates = -1;
    bool end_of_blocks = false;
    UInt64 heartbeat_interval_usec;
    UInt64 temporary_live_view_timeout_sec;
    UInt64 last_event_timestamp_usec = 0;
    Poco::Timestamp timestamp;
};

}
