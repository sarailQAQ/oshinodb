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


struct function {
    void* result;
    ColType type;
    int len;
    Func f;
    bool is_null;

    void calc(void* param) {
        if (f == FUNC_NULL) return;

        if (is_null) {
            memmove(result, param, len);
            is_null = false;
            return;
        }

        else if (f == FUNC_COUNT) {
            *(int*)result = *(int*)result + 1;
        } else if (f == FUNC_MAX) {
            if (type == TYPE_INT) *(int*) result = std::max(*(int*)result, *(int*)param);
            else if (type == TYPE_FLOAT) *(float*)result = std::max(*(float*)result, *(float *)param);
            else if (type == TYPE_STRING) {
                if (strcmp((char*) result, (char*)param) < 0) memmove(result, param, len);
            }
        } else if (f == FUNC_MIN) {
            if (type == TYPE_INT) *(int*) result = std::min(*(int*)result, *(int*)param);
            else if (type == TYPE_FLOAT) *(float*)result = std::min(*(float*)result, *(float *)param);
            else if (type == TYPE_STRING) {
                if (strcmp((char*) result, (char*)param) > 0) memmove(result, param, len);
            }
        } else if (f == FUNC_SUM) {
            if (type == TYPE_INT) *(int*) result += *(int*)param;
            else if (type == TYPE_FLOAT) *(float*)result += *(float *)param;
        }
    }

    explicit function(Func f_, ColType type_ = TYPE_INT, int len_ = 4) {
        f = f_;
        type = type_;
        len = len_;
        is_null = true;

        if (type == TYPE_INT) result = new int ;
        else if (type == TYPE_FLOAT) result = new float ;
        else if (type == TYPE_STRING) result = new char[len];

        if (f == FUNC_COUNT) {
            type = TYPE_INT;
            len = 4;
            *(int*)result = 0;
            is_null = false;
        } else if (f == FUNC_SUM) {
            if (type != TYPE_INT && type != TYPE_FLOAT) throw InternalError("failed to construct function");

            if (type == TYPE_INT) *(int*) result = 0;
            else if (type == TYPE_FLOAT) *(float*)result = 0;
            is_null = false;
        }
    }
};

class AggregateExecutor : public AbstractExecutor {
private:
    std::unique_ptr<AbstractExecutor> prev_;        // 投影节点的儿子节点
    std::vector<ColMeta> cols_;                     // 需要投影的字段
    size_t len_;                                    // 字段总长度
    std::vector<size_t> sel_idxs_;
    std::vector<TabCol> sel_cols_;
    std::vector<function> functions;
    bool is_end_;

public:
    AggregateExecutor(std::unique_ptr<AbstractExecutor> prev, const std::vector<TabCol> &sel_cols) {
        prev_ = std::move(prev);
        is_end_ = true;

        size_t curr_offset = 0;
        auto &prev_cols = prev_->cols();
        for (auto &sel_col : sel_cols) {
            auto pos = get_col(prev_cols, sel_col);
            sel_idxs_.push_back(pos - prev_cols.begin());

            functions.emplace_back(sel_col.func, pos->type, pos->len);

            const auto& f = functions[functions.size() - 1];
            ColMeta col = *pos;
            col.len = f.len;
            col.type = f.type;
            col.offset = curr_offset;
            curr_offset += f.len;
            if (f.f == FUNC_COUNT) is_end_ = false; // count字段需要返回0

            cols_.push_back(col);
        }

        len_ = curr_offset;
        sel_cols_ = sel_cols;
    }

    void beginTuple() override {
        prev_->beginTuple();
    }

    void nextTuple() override {

    }

    std::unique_ptr<RmRecord> Next() override {
        if (is_end()) return nullptr;

        auto proj_rec = std::make_unique<RmRecord>(len_);
        for (; !prev_->is_end(); prev_->nextTuple()) {
            auto prev_rec = prev_->Next();
            auto &prev_cols = prev_->cols();
            for (size_t idx = 0; idx < cols_.size(); idx++) {
                size_t prev_idx = sel_idxs_[idx];
                auto &prev_col = prev_cols[prev_idx];
                auto &proj_col = cols_[idx];
                functions[idx].calc(prev_rec->data+prev_col.offset);
            }
        }

        for (auto f : functions) {
            memmove(proj_rec->data, f.result, f.len);
        }

        is_end_ = true;
        return proj_rec;
    }

    bool is_end() override {
        return is_end_ && prev_->is_end();
    }

    const std::vector<ColMeta> &cols() override {
        return cols_;
    }

    Rid &rid() override { return _abstract_rid; }
};