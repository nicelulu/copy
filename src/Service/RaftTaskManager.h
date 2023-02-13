/**
 * Copyright 2021-2023 JD.com, Inc.
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <fstream>
#include <Service/ThreadSafeQueue.h>
#include <libnuraft/nuraft.hxx>
#include <Poco/Util/LayeredConfiguration.h>
#include <Common/StringUtils/StringUtils.h>
#include <Common/ThreadPool.h>
#include <common/logger_useful.h>

namespace RK
{

//Only support backend async task
enum TaskType
{
    IDLE = -1,
    COMMITTED = 0,
    ERROR = 99
};

class BaseTask
{
public:
    BaseTask(TaskType type) : task_type(type) { }
    TaskType task_type;
};

class RaftTaskManager
{
public:
    RaftTaskManager(const std::string & snapshot_dir);
    ~RaftTaskManager();
    //save last index after commit log to state machine
    void afterCommitted(nuraft::ulong last_committed_index);
    //get last committed index
    void getLastCommitted(nuraft::ulong & last_committed_index);
    //server shut down
    void shutDown();

private:
    ThreadPool thread_pool;
    ThreadSafeQueue<std::shared_ptr<BaseTask>> task_queue;
    //std::condition_variable task_var;
    std::mutex write_file;
    std::atomic<bool> is_shut_down{false};
    std::vector<std::string> task_file_names;
    std::vector<int> task_files;
    Poco::Logger * log;
    UInt32 GetTaskTimeoutMS = 100;
    UInt32 BatchSize = 1000;
};

}
