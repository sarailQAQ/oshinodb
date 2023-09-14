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

class SeqScanExecutor : public AbstractExecutor {
private:
    std::string tab_name_;              // 表的名称
    std::vector<Condition> conds_;      // scan的条件
    RmFileHandle *fh_;                  // 表的数据文件句柄
    std::vector<ColMeta> cols_;         // scan后生成的记录的字段
    size_t len_;                        // scan后生成的每条记录的长度
    std::vector<Condition> fed_conds_;  // 同conds_，两个字段相同

    Rid rid_;
    std::unique_ptr<RecScan> scan_;     // table_iterator
    Filter *filter_;

    SmManager *sm_manager_;

public:
    SeqScanExecutor(SmManager *sm_manager, std::string tab_name, std::vector<Condition> conds, Context *context) {
        sm_manager_ = sm_manager;
        tab_name_ = std::move(tab_name);
        conds_ = std::move(conds);
        TabMeta tab = sm_manager_->db_.get_table(tab_name_);
        fh_ = sm_manager_->fhs_.at(tab_name_).get();
        cols_ = tab.cols;
        len_ = cols_.back().offset + cols_.back().len;

        context_ = context;

        fed_conds_ = conds_;
        filter_ = new Filter(fed_conds_);

        // TODO 会遍历所有记录，直接在表上加读锁
        if (!context->lock_mgr_->lock_shared_on_table(context->txn_, fh_->GetFd()))
            throw TransactionAbortException(context->txn_->get_transaction_id(), AbortReason::DEADLOCK_PREVENTION);

    }

    void beginTuple() override {
        scan_ = std::make_unique<RmScan>(fh_);
        for (; !scan_->is_end(); scan_->next()) {  // 用TableIterator遍历TableHeap中的所有Tuple
            rid_ = scan_->rid();
            auto rec = fh_->get_record(rid_, context_);
            if (filter_->filter(cols_,  rec.get()))
                break;
        }
    }

    void nextTuple() override {
        if (scan_->is_end()) return;
        for (scan_->next(); !scan_->is_end(); scan_->next()) {  // 用TableIterator遍历TableHeap中的所有Tuple
            rid_ = scan_->rid();
            auto rec = fh_->get_record(rid_, context_);
            if (filter_->filter(cols_, rec.get())) {
                break;
            }
        }
    }

    std::unique_ptr<RmRecord> Next() override {
        return fh_->get_record(rid_, context_);
    }

    bool is_end() override {
        return scan_->is_end();
    }

    const std::vector<ColMeta> &cols() override {
        return cols_;
    }

    size_t tupleLen() override {
        return len_;
    }

    Rid &rid() override { return rid_; }
};