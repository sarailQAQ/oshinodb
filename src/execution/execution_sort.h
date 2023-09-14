/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#pragma once

#include "execution_defs.h"
#include "execution_manager.h"
#include "executor_abstract.h"
#include "index/ix.h"
#include "system/sm.h"
#include <climits>
#include <queue>

class SortExecutor : public AbstractExecutor {
private:
    std::unique_ptr<AbstractExecutor> prev_;
    std::map<std::string, ColMeta> cols_map;                              // 框架中只支持一个键排序，需要自行修改数据结构支持多个键排序
    std::vector<TabCol> key_cols_;
    size_t tuple_num;
    int used_tuple;
    std::unique_ptr<RmRecord> current_tuple;
    std::vector<RmRecord> records;
    int limit_;

public:
    SortExecutor(std::unique_ptr<AbstractExecutor> prev, std::vector<TabCol> &key_cols, int limit) {
        prev_ = std::move(prev);

        auto &cols = prev_->cols();
        for (auto key_col: key_cols) {
            auto it = std::find_if(cols.begin(), cols.end(), [&](const ColMeta &col) {
                return col.tab_name == key_col.tab_name && col.name == key_col.col_name;
            });
            cols_map[it->name] = *it;
        }
        key_cols_ = key_cols;
        tuple_num = 0;
        used_tuple = 0;
        limit_ = limit;
    }

    void beginTuple() override {
        if (limit_ < 0) limit_ = INT_MAX;
        size_t count = 0;
        for (prev_->beginTuple(); !prev_->is_end(); prev_->nextTuple()) {
            records.push_back(*(prev_->Next()));
            count++;
        }
        if (!key_cols_.empty())
            std::sort(records.begin(), records.end(), [&](const RmRecord &l, const RmRecord &r) {
                for (const auto &tab_col: key_cols_) {
                    auto col_meta = cols_map[tab_col.col_name];
                    auto &is_desc = tab_col.is_desc;
                    int cmp = Filter::compare(l.data + col_meta.offset, r.data + col_meta.offset, col_meta.len,
                                              col_meta.type);
                    if (cmp != 0) return (is_desc) ? cmp > 0 : cmp < 0;
                }

                // 全部相等
                return false;
            });
    }

    void nextTuple() override {
        used_tuple++;
    }

    std::unique_ptr<RmRecord> Next() override {
        if (is_end()) return nullptr;
        return std::make_unique<RmRecord>(records[used_tuple]);
    }

    bool is_end() override { return used_tuple >= limit_ || used_tuple >= records.size(); }

    size_t tupleLen() override {
        return prev_->tupleLen();
    };

    const std::vector<ColMeta> &cols() override {
        return prev_->cols();
    };

    Rid &rid() override { return _abstract_rid; }
};