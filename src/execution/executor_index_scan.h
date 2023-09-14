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

#include "climits"

#include "execution_defs.h"
#include "execution_manager.h"
#include "executor_abstract.h"
#include "index/ix.h"
#include "system/sm.h"
#include "float.h"
#include "filter.h"

class IndexScanExecutor : public AbstractExecutor {
private:
    std::string tab_name_;                      // 表名称
    TabMeta tab_;                               // 表的元数据
    std::vector<Condition> conds_;              // 扫描条件
    RmFileHandle *fh_;                          // 表的数据文件句柄
    std::vector<ColMeta> cols_;                 // 需要读取的字段
    size_t len_;                                // 选取出来的一条记录的长度
    std::vector<Condition> fed_conds_;          // 扫描条件，和conds_字段相同

    std::vector<std::string> index_col_names_;  // index scan涉及到的索引包含的字段
    IndexMeta index_meta_;                      // index scan涉及到的索引元数据

    Rid rid_;
    std::unique_ptr<RecScan> scan_;

    SmManager *sm_manager_;

    Filter *filter_;

    static void fill(char *val, ColType type, int len, bool min_or_max) {
        switch (type) {
            case TYPE_INT: {
                *(int *) val = min_or_max ? INT_MIN : INT_MAX;
                break;
            }
            case TYPE_FLOAT:
                *(float *) val = min_or_max ? -FLT_MAX : FLT_MAX;
                break;
            case TYPE_STRING:
                memset(val, (min_or_max ? 0 : CHAR_MAX), len);
                break;
            case TYPE_BIGINT:
                *(long long *) val = min_or_max ? LLONG_MIN : LLONG_MAX;
                break;
            case TYPE_DATETIME:
                memset(val, (min_or_max ? 0 : CHAR_MAX), len);
                break;
            case TYPE_DOUBLE:
                *(long double *) val = min_or_max ? -DBL_MAX : DBL_MAX;
                break;
        }
    }

public:
    IndexScanExecutor(SmManager *sm_manager, std::string tab_name, std::vector<Condition> conds,
                      std::vector<std::string> index_col_names,
                      Context *context) {
        sm_manager_ = sm_manager;
        context_ = context;
        tab_name_ = std::move(tab_name);
        tab_ = sm_manager_->db_.get_table(tab_name_);
        conds_ = std::move(conds);
        // index_no_ = index_no;
        index_col_names_ = index_col_names;
        index_meta_ = *(tab_.get_index_meta(index_col_names_));
        fh_ = sm_manager_->fhs_.at(tab_name_).get();
        cols_ = tab_.cols;
        len_ = cols_.back().offset + cols_.back().len;
        std::map<CompOp, CompOp> swap_op = {
                {OP_EQ, OP_EQ},
                {OP_NE, OP_NE},
                {OP_LT, OP_GT},
                {OP_GT, OP_LT},
                {OP_LE, OP_GE},
                {OP_GE, OP_LE},
        };

        for (auto &cond: conds_) {
            if (cond.lhs_col.tab_name != tab_name_) {
                // lhs is on other table, now rhs must be on this table
                assert(!cond.is_rhs_val && cond.rhs_col.tab_name == tab_name_);
                // swap lhs and rhs
                std::swap(cond.lhs_col, cond.rhs_col);
                cond.op = swap_op.at(cond.op);
            }
        }
        fed_conds_ = conds_;

        filter_ = new Filter(fed_conds_);

        if (!context->lock_mgr_->lock_shared_on_table(context->txn_, fh_->GetFd()))
            throw TransactionAbortException(context->txn_->get_transaction_id(), AbortReason::DEADLOCK_PREVENTION);
    }

    void beginTuple() override {
        auto ih = sm_manager_->ihs_.at(
                sm_manager_->get_ix_manager()->get_index_name(tab_name_, index_col_names_)).get();
        auto lower = ih->leaf_begin(), upper = ih->leaf_end();
        auto lower_unmatched_idx = 0, upper_unmatched_idx = 0, lOffset = 0, rOffset = 0;
        char *lKey = new char[index_meta_.col_tot_len], *rKey = new char[index_meta_.col_tot_len];
        // TODO 条件里出现多个关于一个列的条件时，只会使用一个条件，比如 where id>2 and id>3
        for (size_t idx = 0; idx < index_col_names_.size(); idx++) {
            auto &col_name = index_col_names_[idx];
            auto col = tab_.get_col(col_name);

            // 匹配左区间闭端点
            if (lower_unmatched_idx == idx) {
                for (auto &cond: fed_conds_) {
                    if (!cond.is_rhs_val || cond.lhs_col.col_name != col_name || cond.op == OP_NE ) continue;

                    auto &op = cond.op;
                    if (op == OP_EQ || op == OP_GT || op == OP_GE) {
                        memmove(lKey + lOffset, cond.rhs_val.raw->data, col->len);
                        lOffset += col->len;
                        lower_unmatched_idx++;
                    }
                }
            }

            // 匹配右开区间端点
            if (upper_unmatched_idx == idx) {
                for (auto &cond: fed_conds_) {
                    if (!cond.is_rhs_val || cond.lhs_col.col_name != col_name || cond.op == OP_NE ) continue;

                    auto &op = cond.op;
                    if (op == OP_EQ || op == OP_LT || op == OP_LE) {
                        memmove(rKey + rOffset, cond.rhs_val.raw->data, col->len);
                        rOffset += col->len;
                        upper_unmatched_idx++;
                    }
                }
            }
        }
        if (lower_unmatched_idx > 0) {
            // 填充左key, 用最小值填充
            for (size_t idx = lower_unmatched_idx; idx < index_col_names_.size(); idx++) {
                const auto col = tab_.get_col(index_col_names_[idx]);
                char *temp = new char[col->len];
                fill(temp, col->type, col->len, true);
                memmove(lKey + lOffset, temp, col->len);
                lOffset += col->len;
            }
            lower = ih->lower_bound(lKey);
        }
        if (upper_unmatched_idx > 0) {
            // 用最大值填充右key
            for (size_t idx = upper_unmatched_idx; idx < index_col_names_.size(); idx++) {
                const auto col = tab_.get_col(index_col_names_[idx]);
                char *temp = new char[col->len];
                fill(temp, col->type, col->len, false);
                memmove(rKey + rOffset, temp, col->len);
                rOffset += col->len;
            }
            upper = ih->upper_bound(rKey);
        }

        scan_ = std::make_unique<IxScan>(ih, lower, upper, sm_manager_->get_bpm());
        while (!scan_->is_end()) {
            rid_ = scan_->rid();
            auto rec = fh_->get_record(rid_, context_);
            if (filter_->filter(cols_, rec.get())) break;
            scan_->next();
        }
    }

    void nextTuple() override {
        if (is_end()) return;

        for (scan_->next(); !scan_->is_end(); scan_->next()) {  // 用TableIterator遍历TableHeap中的所有Tuple
            rid_ = scan_->rid();

            auto rec = fh_->get_record(rid_, context_);
            if (filter_->filter(cols_, rec.get())) break;
        }

    }

    std::unique_ptr<RmRecord> Next() override {
        if (is_end()) throw InternalError("IndexScanExecutor::Next is_end() is true");
        return fh_->get_record(rid_, context_);
    }

    size_t tupleLen() override {
        return len_;
    };

    const std::vector<ColMeta> &cols() override {
        return cols_;
    };

    std::string getType() override { return "IndexScanExecutor"; };

    Rid &rid() override { return rid_; }

    bool is_end() override { return scan_->is_end(); }
};