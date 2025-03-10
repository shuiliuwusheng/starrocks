// This file is made available under Elastic License 2.0.
// This file is based on code available under the Apache license here:
//   https://github.com/apache/incubator-doris/blob/master/be/test/util/arrow/arrow_row_block_test.cpp

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

#include <gtest/gtest.h>

#include <sstream>

#include "common/logging.h"
#include "util/arrow/row_block.h"

#define ARROW_UTIL_LOGGING_H
#include <arrow/buffer.h>
#include <arrow/json/api.h>

#include "common/compiler_util.h"
DIAGNOSTIC_PUSH
DIAGNOSTIC_IGNORE("-Wclass-memaccess")
#include <arrow/json/test_common.h>
DIAGNOSTIC_POP

#include <arrow/memory_pool.h>
#include <arrow/pretty_print.h>
#include <arrow/record_batch.h>

#include "storage/row_block2.h"
#include "storage/schema.h"
#include "storage/tablet_schema_helper.h"

namespace starrocks {

class ArrowRowBlockTest : public testing::Test {
public:
    ArrowRowBlockTest() {}
    virtual ~ArrowRowBlockTest() {}
};

static std::string test_str() {
    return R"(
    { "c1": 1, "c2": 1.1 }
    { "c1": 2, "c2": 2.2 }
    { "c1": 3, "c2": 3.3 }
        )";
}

static void MakeBuffer(const std::string& data, std::shared_ptr<arrow::Buffer>* out) {
    arrow::AllocateBuffer(arrow::default_memory_pool(), data.size(), out);
    std::copy(std::begin(data), std::end(data), (*out)->mutable_data());
}

TEST_F(ArrowRowBlockTest, Normal) {
    auto json = test_str();
    std::shared_ptr<arrow::Buffer> buffer;
    MakeBuffer(test_str(), &buffer);
    arrow::json::ParseOptions parse_opts = arrow::json::ParseOptions::Defaults();
    parse_opts.explicit_schema = arrow::schema({
            arrow::field("c1", arrow::int64()),
    });

    std::shared_ptr<arrow::RecordBatch> record_batch;
    auto arrow_st = arrow::json::ParseOne(parse_opts, buffer, &record_batch);
    ASSERT_TRUE(arrow_st.ok());

    std::shared_ptr<Schema> schema;
    auto starrocks_st = convert_to_starrocks_schema(*record_batch->schema(), &schema);
    ASSERT_TRUE(starrocks_st.ok());

    std::shared_ptr<RowBlockV2> row_block;
    starrocks_st = convert_to_row_block(*record_batch, *schema, &row_block);
    ASSERT_TRUE(starrocks_st.ok());

    {
        std::shared_ptr<arrow::Schema> check_schema;
        starrocks_st = convert_to_arrow_schema(*schema, &check_schema);
        ASSERT_TRUE(starrocks_st.ok());

        arrow::MemoryPool* pool = arrow::default_memory_pool();
        std::shared_ptr<arrow::RecordBatch> check_batch;
        starrocks_st = convert_to_arrow_batch(*row_block, check_schema, pool, &check_batch);
        ASSERT_TRUE(starrocks_st.ok());
        ASSERT_EQ(3, check_batch->num_rows());
        ASSERT_TRUE(record_batch->Equals(*check_batch));
    }
}

} // namespace starrocks
