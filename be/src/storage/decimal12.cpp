// This file is made available under Elastic License 2.0.
// This file is based on code available under the Apache license here:
//   https://github.com/apache/incubator-doris/blob/master/be/src/olap/decimal12.cpp

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

#include "storage/decimal12.h"

#include "storage/utils.h"

namespace starrocks {

OLAPStatus decimal12_t::from_string(const std::string& str) {
    integer = 0;
    fraction = 0;
    const char* value_string = str.c_str();
    const char* sign = strchr(value_string, '-');

    if (sign != NULL) {
        if (sign != value_string) {
            return OLAP_ERR_INPUT_PARAMETER_ERROR;
        } else {
            ++value_string;
        }
    }

    const char* sepr = strchr(value_string, '.');
    if ((sepr != NULL && sepr - value_string > MAX_INT_DIGITS_NUM) ||
        (sepr == NULL && strlen(value_string) > MAX_INT_DIGITS_NUM)) {
        integer = 999999999999999999;
        fraction = 999999999;
    } else {
        if (sepr == value_string) {
            sscanf(value_string, ".%9d", &fraction);
            integer = 0;
        } else {
            sscanf(value_string, "%18ld.%9d", &integer, &fraction);
        }

        int32_t frac_len = (NULL != sepr) ? MAX_FRAC_DIGITS_NUM - strlen(sepr + 1) : MAX_FRAC_DIGITS_NUM;
        frac_len = frac_len > 0 ? frac_len : 0;
        fraction *= g_power_table[frac_len];
    }

    if (sign != NULL) {
        fraction = -fraction;
        integer = -integer;
    }

    return OLAP_SUCCESS;
}

} // namespace starrocks
