// This file is licensed under the Elastic License 2.0. Copyright 2021 StarRocks Limited.

#pragma once

#include "column/vectorized_fwd.h"
#include "exprs/expr_context.h"
#include "util/runtime_profile.h"

namespace starrocks::vectorized {
struct PermutationItem {
    uint32_t chunk_index;
    uint32_t index_in_chunk;
    uint32_t permutation_index;
};
using Permutation = std::vector<PermutationItem>;

struct DataSegment {
    ChunkPtr chunk;
    Columns order_by_columns;

    DataSegment() : chunk(std::make_shared<Chunk>()) {}

    DataSegment(const std::vector<ExprContext*>* sort_exprs, const ChunkPtr& cnk) { init(sort_exprs, cnk); }

    void init(const std::vector<ExprContext*>* sort_exprs, const ChunkPtr& cnk) {
        chunk = cnk;
        order_by_columns.reserve(sort_exprs->size());
        for (ExprContext* expr_ctx : (*sort_exprs)) {
            order_by_columns.push_back(expr_ctx->evaluate(chunk.get()));
        }
    }

    // compare every row in incoming_column with number_of_row_to_compare row with base_column,
    // save result compare_results, and collect equal rows of incoming_column as rows_to_compare use to
    // comapre with next column.
    template <bool reversed>
    static void compare_between_rows(Column& incoming_column, Column& base_column, size_t number_of_row_to_compare,
                                     std::vector<uint64_t>* rows_to_compare, std::vector<int8_t>* compare_results,
                                     int null_first_flag) {
        uint64_t* indexes;
        uint64_t* next_index;

        size_t num_indexes = rows_to_compare->size();
        next_index = indexes = rows_to_compare->data();

        for (size_t i = 0; i < num_indexes; ++i) {
            uint64_t row = indexes[i];
            int res = incoming_column.compare_at(row, number_of_row_to_compare, base_column, null_first_flag);

            /// Convert to (-1, 0, 1).
            if (res < 0) {
                (*compare_results)[row] = -1;
            } else if (res > 0) {
                (*compare_results)[row] = 1;
            } else {
                (*compare_results)[row] = 0;
                *next_index = row;
                ++next_index;
            }

            if constexpr (reversed) (*compare_results)[row] = -(*compare_results)[row];
        }

        rows_to_compare->resize(next_index - rows_to_compare->data());
    }

    // compare data from incoming_column with number_of_row_to_compare of base_column.
    static void compare_column_with_one_row(Column& incoming_column, Column& base_column,
                                            size_t number_of_row_to_compare, std::vector<uint64_t>* rows_to_compare,
                                            std::vector<int8_t>* compare_result, int sort_order_flag,
                                            int null_first_flag) {
        if (sort_order_flag < 0) {
            compare_between_rows<true>(incoming_column, base_column, number_of_row_to_compare, rows_to_compare,
                                       compare_result, null_first_flag);
        } else {
            compare_between_rows<false>(incoming_column, base_column, number_of_row_to_compare, rows_to_compare,
                                        compare_result, null_first_flag);
        }
    }

    // compare all indexs of rows_to_compare_array from data_segments with row_to_sort of order_by_columns
    // through every column until get result as compare_results_array.
    // rows_to_compare_array is used to save rows that should compare next column.
    static void get_compare_results(size_t row_to_sort, Columns& order_by_columns,
                                    std::vector<std::vector<uint64_t>>* rows_to_compare_array,
                                    std::vector<std::vector<int8_t>>* compare_results_array,
                                    std::vector<DataSegment>& data_segments, const std::vector<int>& sort_order_flags,
                                    const std::vector<int>& null_first_flags) {
        size_t dats_segment_size = data_segments.size();
        size_t size = order_by_columns.size();

        for (size_t i = 0; i < dats_segment_size; ++i) {
            for (size_t j = 0; j < size; ++j) {
                compare_column_with_one_row(*data_segments[i].order_by_columns[j], *order_by_columns[j], row_to_sort,
                                            &(*rows_to_compare_array)[i], &(*compare_results_array)[i],
                                            sort_order_flags[j], null_first_flags[j]);

                if ((*rows_to_compare_array)[i].empty()) break;
            }
        }
    }

    static const uint8_t BEFORE_LAST_RESULT = 2;
    static const uint8_t IN_LAST_RESULT = 1;

    // there is two compares in the method,
    // the first is:
    //     compare every row in every DataSegment of data_segments with number_of_rows_to_sort - 1 row of this DataSegment,
    //     obtain every row compare result in compare_results_array, if < 0, use it to set IN at filter_array.
    // the second is:
    //     compare every row in compare_results_array that less than 0, use it to compare with first row of this DataSegment,
    //     as the first step, we set BEFORE_LAST_RESULT at filter_array.
    //
    // Actually, we Count the results in the first compare for the second compare.
    Status get_filter_array(std::vector<DataSegment>& data_segments, size_t number_of_rows_to_sort,
                            std::vector<std::vector<uint8_t>>& filter_array, const std::vector<int>& sort_order_flags,
                            const std::vector<int>& null_first_flags, uint32_t& least_num, uint32_t& middle_num,
                            std::function<Status(size_t bytes)> consume_and_check_memory_limit) {
        size_t dats_segment_size = data_segments.size();

        std::vector<std::vector<int8_t>> compare_results_array;
        compare_results_array.resize(dats_segment_size);

        // first compare with last row of this chunk.
        {
            std::vector<std::vector<uint64_t>> rows_to_compare_array;
            rows_to_compare_array.resize(dats_segment_size);

            for (size_t i = 0; i < dats_segment_size; ++i) {
                size_t rows = data_segments[i].chunk->num_rows();

                compare_results_array[i].resize(rows);
                rows_to_compare_array[i].resize(rows);

                for (size_t j = 0; j < rows; ++j) {
                    rows_to_compare_array[i][j] = j;
                }
            }

            // compare all rows of rows_to_compare_array with number_of_rows_to_sort - 1 row of order_by_columns.
            get_compare_results(number_of_rows_to_sort - 1, order_by_columns, &rows_to_compare_array,
                                &compare_results_array, data_segments, sort_order_flags, null_first_flags);
        }

        // but we only have one compare.
        // compare with first row of this DataSegment,
        // then we set BEFORE_LAST_RESULT and IN_LAST_RESULT at filter_array.
        if (number_of_rows_to_sort == 1) {
            RETURN_IF_ERROR(consume_and_check_memory_limit(0));

            least_num = 0, middle_num = 0;
            filter_array.resize(dats_segment_size);
            for (size_t i = 0; i < dats_segment_size; ++i) {
                size_t rows = data_segments[i].chunk->num_rows();
                filter_array[i].resize(rows);

                for (size_t j = 0; j < rows; ++j) {
                    if (compare_results_array[i][j] < 0) {
                        filter_array[i][j] = DataSegment::BEFORE_LAST_RESULT;
                        ++least_num;
                    } else {
                        filter_array[i][j] = DataSegment::IN_LAST_RESULT;
                        ++middle_num;
                    }
                }
            }
        } else {
            std::vector<size_t> first_size_array;
            first_size_array.resize(dats_segment_size);

            middle_num = 0;
            filter_array.resize(dats_segment_size);
            for (size_t i = 0; i < dats_segment_size; ++i) {
                DataSegment& segment = data_segments[i];
                size_t rows = segment.chunk->num_rows();
                filter_array[i].resize(rows);

                size_t local_first_size = middle_num;
                for (size_t j = 0; j < rows; ++j) {
                    if (compare_results_array[i][j] < 0) {
                        filter_array[i][j] = DataSegment::IN_LAST_RESULT;
                        ++middle_num;
                    }
                }

                // obtain number of rows for second compare.
                first_size_array[i] = middle_num - local_first_size;
            }

            RETURN_IF_ERROR(
                    consume_and_check_memory_limit(dats_segment_size * sizeof(size_t) + middle_num * sizeof(uint64_t)));

            // second compare with first row of this chunk, use rows from first compare.
            {
                std::vector<std::vector<uint64_t>> rows_to_compare_array;
                rows_to_compare_array.resize(dats_segment_size);

                for (size_t i = 0; i < dats_segment_size; ++i) {
                    size_t rows = data_segments[i].chunk->num_rows();

                    rows_to_compare_array[i].resize(first_size_array[i]);
                    size_t first_index = 0;
                    for (size_t j = 0; j < rows; ++j) {
                        if (compare_results_array[i][j] < 0) {
                            // used to index datas that belong to LAST RESULT.
                            rows_to_compare_array[i][first_index] = j;
                            ++first_index;
                        }

                        compare_results_array[i][j] = 0;
                    }
                }

                // compare all rows of rows_to_compare_array with 0 row of order_by_columns.
                get_compare_results(0, order_by_columns, &rows_to_compare_array, &compare_results_array, data_segments,
                                    sort_order_flags, null_first_flags);
            }

            least_num = 0;
            for (size_t i = 0; i < dats_segment_size; ++i) {
                DataSegment& segment = data_segments[i];
                size_t rows = segment.chunk->num_rows();

                for (size_t j = 0; j < rows; ++j) {
                    if (compare_results_array[i][j] < 0) {
                        filter_array[i][j] = DataSegment::BEFORE_LAST_RESULT;
                        ++least_num;
                    }
                }
            }
            middle_num -= least_num;
        }

        return Status::OK();
    }

    void clear() {
        chunk.reset(std::make_unique<Chunk>().release());
        order_by_columns.clear();
    }

    // Return value:
    //  < 0: current row precedes the row in the other chunk;
    // == 0: current row is equal to the row in the other chunk;
    //  > 0: current row succeeds the row in the other chunk;
    int compare_at(size_t index_in_chunk, const DataSegment& other, size_t index_in_other_chunk,
                   const std::vector<int>& sort_order_flag, const std::vector<int>& null_first_flag) const {
        size_t col_number = order_by_columns.size();
        for (size_t col_index = 0; col_index < col_number; ++col_index) {
            const auto& left_col = order_by_columns[col_index];
            const auto& right_col = other.order_by_columns[col_index];
            int c = left_col->compare_at(index_in_chunk, index_in_other_chunk, *right_col, null_first_flag[col_index]);
            if (c != 0) {
                return c * sort_order_flag[col_index];
            }
        }
        return 0;
    }
};
using DataSegments = std::vector<DataSegment>;

// Sort Chunks in memory with specified order by rules.
class ChunksSorter {
public:
    /**
     * Constructor.
     * @param sort_exprs     The order-by columns or columns with expresion. This sorter will use but not own the object.
     * @param is_asc         Orders on each column.
     * @param is_null_first  NULL values should at the head or tail.
     * @param size_of_chunk_batch  In the case of a positive limit, this parameter limits the size of the batch in Chunk unit.
     */
    ChunksSorter(const std::vector<ExprContext*>* sort_exprs, const std::vector<bool>* is_asc,
                 const std::vector<bool>* is_null_first, size_t size_of_chunk_batch = 1000);
    virtual ~ChunksSorter();

    void setup_runtime(MemTracker* mem_tracker, RuntimeProfile* profile, const std::string& parent_timer);

    // Append a Chunk for sort.
    virtual Status update(RuntimeState* state, const ChunkPtr& chunk) = 0;
    // Finish seeding Chunk, and get sorted data with top OFFSET rows have been skipped.
    virtual Status done(RuntimeState* state) = 0;
    // get_next only works after done().
    virtual void get_next(ChunkPtr* chunk, bool* eos) = 0;

protected:
    inline size_t _get_number_of_order_by_columns() const { return _sort_exprs->size(); }

    Status _consume_and_check_memory_limit(RuntimeState* state, int64_t mem_bytes);

    // sort rules
    const std::vector<ExprContext*>* _sort_exprs;
    std::vector<int> _sort_order_flag; // 1 for ascending, -1 for descending.
    std::vector<int> _null_first_flag; // 1 for greatest, -1 for least.

    size_t _next_output_row = 0;

    const size_t _size_of_chunk_batch;
    MemTracker* _mem_tracker;
    int64_t _last_memory_usage;

    RuntimeProfile::Counter* _build_timer = nullptr;
    RuntimeProfile::Counter* _sort_timer = nullptr;
    RuntimeProfile::Counter* _merge_timer = nullptr;
    RuntimeProfile::Counter* _output_timer = nullptr;
};

} // namespace starrocks::vectorized
