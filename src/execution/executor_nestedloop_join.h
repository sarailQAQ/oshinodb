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
#include "filter.h"
#include "queue"

class NestedLoopJoinExecutor : public AbstractExecutor {
private:
    std::unique_ptr<AbstractExecutor> left_;    // 左儿子节点（需要join的表）
    std::unique_ptr<AbstractExecutor> right_;   // 右儿子节点（需要join的表）
    size_t len_;                                // join后获得的每条记录的长度
    std::vector<ColMeta> cols_;                 // join后获得的记录的字段

    std::vector<Condition> fed_conds_;          // join条件
    bool isend;

    Filter *filter_;
    std::unique_ptr<RmRecord> cur_rec_;

    int buffer_size;
    std::vector<std::unique_ptr<RmRecord>> l_records;
    std::queue<std::unique_ptr<RmRecord>> buff;

    void make_buff() {
        if (isend) return;
        // 指向下一位

        for (; !left_->is_end() || !l_records.empty(); ) {
            if (l_records.empty())
                for (size_t i = 0; i < buffer_size && !left_->is_end(); i++) {
                    l_records.push_back(left_->Next());
                    left_->nextTuple();
                }
            for (; !right_->is_end(); right_->nextTuple()) {
                auto rrec = right_->Next();
                auto data = new char[len_];
                for (auto& lrec : l_records) {
                    memset(data, 0, len_);
                    memmove(data, lrec->data, lrec->size);
                    memmove(data+lrec->size, rrec->data, rrec->size);
                    cur_rec_ =  std::make_unique<RmRecord>(len_, data);
                    if (filter_->filter(cols_, cur_rec_.get())) {
                        buff.push(std::move(cur_rec_));
                    }
                }
                delete[] data;
                if (!buff.empty()) return;
            }
            l_records.clear();
            right_->beginTuple();
        }
        isend = true;
    }

public:
    NestedLoopJoinExecutor(std::unique_ptr<AbstractExecutor> left, std::unique_ptr<AbstractExecutor> right,
                           std::vector<Condition> conds) {
        left_ = std::move(left);
        right_ = std::move(right);
        len_ = left_->tupleLen() + right_->tupleLen();
        cols_ = left_->cols();
        auto right_cols = right_->cols();
        for (auto &col: right_cols) {
            col.offset += left_->tupleLen();
        }

        cols_.insert(cols_.end(), right_cols.begin(), right_cols.end());
        isend = false;
        fed_conds_ = std::move(conds);

        filter_ = new Filter(fed_conds_);

        buffer_size = std::max(64, int(4 * PAGE_SIZE / left_->tupleLen()));
    }

    void beginTuple() override {
        left_->beginTuple();
        right_->beginTuple();

        l_records.clear();
        while (!buff.empty()) buff.pop();

        make_buff();
    }

    void nextTuple() override {
        if (isend || !buff.empty()) return;

        if (!right_->is_end()) right_->nextTuple();

        make_buff();
    }

    std::unique_ptr<RmRecord> Next() override {
        if (buff.empty()) throw InternalError("NestedLoopJoinExecutor::Next buff is empty");
        auto rec = std::move(buff.front());
        buff.pop();
        return rec;
    }

    bool is_end() override {
        return isend;
    }

    const std::vector<ColMeta> &cols() override {
        return cols_;
    }

    size_t tupleLen() override {
        return len_;
    }

    Rid &rid() override { return _abstract_rid; }
};