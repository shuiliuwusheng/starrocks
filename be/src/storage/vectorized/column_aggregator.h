// This file is licensed under the Elastic License 2.0. Copyright 2021 StarRocks Limited.

#pragma once

#include <memory>
#include <vector>

#include "column/chunk.h"
#include "column/column_helper.h"
#include "simd/simd.h"
#include "storage/vectorized/chunk_helper.h"
#include "storage/vectorized/chunk_iterator.h"

namespace starrocks::vectorized {

class ColumnAggregatorBase {
public:
    ColumnAggregatorBase() : _source_column(nullptr), _aggregate_column(nullptr) {}

    virtual ~ColumnAggregatorBase() = default;

    // update input column. |src| is readonly.
    virtual void update_source(const ColumnPtr& src) { _source_column = src; }

    // update output aggregate column
    virtual void update_aggregate(Column* agg) { _aggregate_column = agg; }

    virtual void aggregate_keys(int start, int nums, const uint32* selective_index) {}

    virtual void aggregate_values(int start, int nums, const uint32* aggregate_loops, bool previous_neq) {}

    virtual void finalize() { _aggregate_column = nullptr; }

public:
    ColumnPtr _source_column;
    Column* _aggregate_column;
};

template <typename ColumnType>
class KeyColumnAggregator final : public ColumnAggregatorBase {
    void aggregate_keys(int start, int nums, const uint32* selective_index) override {
        _aggregate_column->append_selective(*_source_column, selective_index, 0, nums);
    }
};

class ValueColumnAggregatorBase : public ColumnAggregatorBase {
public:
    virtual void reset() {}

    virtual void append_data(Column* agg) {}

    // |data| is readonly.
    virtual void aggregate_impl(int row, const ColumnPtr& data) {}

    // |data| is readonly.
    virtual void aggregate_batch_impl(int start, int end, const ColumnPtr& data) {}
};

using ColumnAggregatorPtr = std::unique_ptr<ColumnAggregatorBase>;
using ValueColumnAggregatorPtr = std::unique_ptr<ValueColumnAggregatorBase>;

template <typename ColumnType, typename StateType>
class ValueColumnAggregator : public ValueColumnAggregatorBase {
public:
    void update_aggregate(Column* agg) override {
        down_cast<ColumnType*>(agg);
        _aggregate_column = agg;
        reset();
    }

    void aggregate_values(int start, int nums, const uint32* aggregate_loops, bool previous_neq) override {
        if (nums <= 0) {
            return;
        }

        // if different with last row in previous chunk
        if (previous_neq) {
            append_data(_aggregate_column);
            reset();
        }

        for (int i = 0; i < nums - 1; ++i) {
            aggregate_batch_impl(start, start + aggregate_loops[i], _source_column);
            append_data(_aggregate_column);

            start += aggregate_loops[i];
            reset();
        }

        if (nums > 0) {
            // last row just aggregate, not finalize
            aggregate_batch_impl(start, start + aggregate_loops[nums - 1], _source_column);
        }
    }

    void finalize() override {
        append_data(_aggregate_column);
        _aggregate_column = nullptr;
    }

    StateType& data() { return _data; }

    void reset() override { this->data() = StateType{}; }

    void append_data(Column* agg) override = 0;

    void aggregate_impl(int row, const ColumnPtr& src) override = 0;

    void aggregate_batch_impl(int start, int end, const ColumnPtr& src) override = 0;

private:
    StateType _data{};
};

class ValueNullableColumnAggregator final : public ValueColumnAggregatorBase {
public:
    explicit ValueNullableColumnAggregator(ValueColumnAggregatorPtr child)
            : ValueColumnAggregatorBase(),
              _child(std::move(child)),
              _aggregate_nulls(nullptr),
              _source_nulls_data(nullptr),
              _row_is_null(0) {}

    void update_source(const ColumnPtr& src) override {
        _source_column = src;

        auto* nullable = down_cast<NullableColumn*>(src.get());
        _child->update_source(nullable->data_column());

        _source_nulls_data = nullable->null_column_data().data();
    }

    void update_aggregate(Column* agg) override {
        _aggregate_column = agg;

        NullableColumn* n = down_cast<NullableColumn*>(agg);
        _child->update_aggregate(n->data_column().get());

        _aggregate_nulls = down_cast<NullColumn*>(n->null_column().get());
        reset();
    }

    void aggregate_values(int start, int nums, const uint32* aggregate_loops, bool previous_neq) override {
        if (nums <= 0) {
            return;
        }

        if (previous_neq) {
            _append_data();
            reset();
        }

        int row_nums = 0;
        for (int i = 0; i < nums; ++i) {
            row_nums += aggregate_loops[i];
        }

        int zeros = SIMD::count_zero(_source_nulls_data + start, row_nums);

        if (zeros == 0) {
            // all null
            for (int i = 0; i < nums - 1; ++i) {
                _row_is_null &= 1u;
                _append_data();
                start += aggregate_loops[i];
                reset();
            }

            if (nums - 1 >= 0) {
                _row_is_null &= 1u;
            }
        } else if (zeros == row_nums) {
            // all not null
            for (int i = 0; i < nums - 1; ++i) {
                _row_is_null &= 0u;
                _child->aggregate_batch_impl(start, start + implicit_cast<int>(aggregate_loops[i]),
                                             _child->_source_column);
                _append_data();
                start += aggregate_loops[i];
                reset();
            }

            if (nums - 1 >= 0) {
                _row_is_null &= 0u;
                _child->aggregate_batch_impl(start, start + implicit_cast<int>(aggregate_loops[nums - 1]),
                                             _child->_source_column);
            }
        } else {
            for (int i = 0; i < nums - 1; ++i) {
                for (int j = start; j < start + aggregate_loops[i]; ++j) {
                    if (_source_nulls_data[j] != 1) {
                        _row_is_null &= 0u;
                        _child->aggregate_impl(j, _child->_source_column);
                    }
                }

                _append_data();
                start += aggregate_loops[i];
                reset();
            }

            if (nums - 1 >= 0) {
                for (int j = start; j < start + aggregate_loops[nums - 1]; ++j) {
                    if (_source_nulls_data[j] != 1) {
                        _row_is_null &= 0u;
                        _child->aggregate_impl(j, _child->_source_column);
                    }
                }
            }
        }
    }

    void finalize() override {
        _child->finalize();
        _aggregate_nulls->append(_row_is_null);
        down_cast<NullableColumn*>(_aggregate_column)->set_has_null(SIMD::count_nonzero(_aggregate_nulls->get_data()));

        _aggregate_nulls = nullptr;
        _aggregate_column = nullptr;
    }

    void reset() override {
        _row_is_null = 1;
        _child->reset();
    }

private:
    void _append_data() {
        _aggregate_nulls->append(_row_is_null);
        _child->append_data(_child->_aggregate_column);
    }

    ValueColumnAggregatorPtr _child;

    NullColumn* _aggregate_nulls;

    uint8_t* _source_nulls_data;

    uint8_t _row_is_null;
};

} // namespace starrocks::vectorized
