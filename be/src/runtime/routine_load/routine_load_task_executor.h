// This file is made available under Elastic License 2.0.
// This file is based on code available under the Apache license here:
//   https://github.com/apache/incubator-doris/blob/master/be/src/runtime/routine_load/routine_load_task_executor.h

// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#pragma once

#include <functional>
#include <map>
#include <mutex>

#include "gen_cpp/internal_service.pb.h"
#include "runtime/routine_load/data_consumer_pool.h"
#include "util/priority_thread_pool.hpp"
#include "util/starrocks_metrics.h"
#include "util/uid_util.h"

namespace starrocks {

class ExecEnv;
class Status;
class StreamLoadContext;
class TRoutineLoadTask;

// A routine load task executor will receive routine load
// tasks from FE, put it to a fixed thread pool.
// The thread pool will process each task and report the result
// to FE finally.
class RoutineLoadTaskExecutor {
public:
    typedef std::function<void(StreamLoadContext*)> ExecFinishCallback;

    RoutineLoadTaskExecutor(ExecEnv* exec_env)
            : _exec_env(exec_env),
              _thread_pool(config::routine_load_thread_pool_size, config::routine_load_thread_pool_size),
              _data_consumer_pool(10) {
        REGISTER_GAUGE_STARROCKS_METRIC(routine_load_task_count, [this]() {
            std::lock_guard<std::mutex> l(_lock);
            return _task_map.size();
        });

        _data_consumer_pool.start_bg_worker();
    }

    ~RoutineLoadTaskExecutor() {
        _thread_pool.shutdown();
        _thread_pool.join();

        LOG(INFO) << _task_map.size() << " not executed tasks left, cleanup";
        for (auto it = _task_map.begin(); it != _task_map.end(); ++it) {
            auto ctx = it->second;
            if (ctx->unref()) {
                delete ctx;
            }
        }
        _task_map.clear();
    }

    // submit a routine load task
    Status submit_task(const TRoutineLoadTask& task);

    Status get_kafka_partition_meta(const PKafkaMetaProxyRequest& request, std::vector<int32_t>* partition_ids);

    Status get_kafka_partition_offset(const PKafkaOffsetProxyRequest& request, std::vector<int64_t>* beginning_offsets,
                                      std::vector<int64_t>* latest_offsets);

private:
    // execute the task
    void exec_task(StreamLoadContext* ctx, DataConsumerPool* pool, ExecFinishCallback cb);

    void err_handler(StreamLoadContext* ctx, const Status& st, const std::string& err_msg);

    // for test only
    Status _execute_plan_for_test(StreamLoadContext* ctx);

private:
    ExecEnv* _exec_env;
    PriorityThreadPool _thread_pool;
    DataConsumerPool _data_consumer_pool;

    std::mutex _lock;
    // task id -> load context
    std::unordered_map<UniqueId, StreamLoadContext*> _task_map;
};

} // namespace starrocks
