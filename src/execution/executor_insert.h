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
#include "exception"

class InsertExecutor : public AbstractExecutor {
private:
    TabMeta tab_;                   // 表的元数据
    std::vector<Value> values_;     // 需要插入的数据
    RmFileHandle *fh_;              // 表的数据文件句柄
    std::string tab_name_;          // 表名称
    Rid rid_;                       // 插入的位置，由于系统默认插入时不指定位置，因此当前rid_在插入后才赋值
    SmManager *sm_manager_;

public:
    InsertExecutor(SmManager *sm_manager, const std::string &tab_name, std::vector<Value> values, Context *context) {
        sm_manager_ = sm_manager;
        tab_ = sm_manager_->db_.get_table(tab_name);
        values_ = values;
        tab_name_ = tab_name;
        if (values.size() != tab_.cols.size()) {
            throw InvalidValueCountError();
        }
        try {
            fh_ = sm_manager_->fhs_.at(tab_name).get();
        } catch (std::out_of_range &e) {

        }
        context_ = context;

        if (!context->lock_mgr_->lock_exclusive_on_table(context->txn_, fh_->GetFd()))
            throw TransactionAbortException(context->txn_->get_transaction_id(), AbortReason::DEADLOCK_PREVENTION);
    };

    std::unique_ptr<RmRecord> Next() override {
        // 生成 RmRecord，并提供尽力的类型转换
        RmRecord rec(fh_->get_file_hdr().record_size);
        for (size_t i = 0; i < values_.size(); i++) {
            auto &col = tab_.cols[i];
            auto val = values_[i];
            if (col.type != val.type) {
                if (col.type == TYPE_BIGINT && val.type == TYPE_INT) {
                    val.set_bigint(val.int_val);
                } else if (col.type == TYPE_DATETIME && val.type == TYPE_STRING) {
                    if (!val.is_valid_datetime()) throw InternalError("invalid datetime");
                } else throw IncompatibleTypeError(coltype2str(col.type), coltype2str(val.type));
            }
            val.init_raw(col.len);
            memcpy(rec.data + col.offset, val.raw->data, col.len);
        }

        // 插入表
        rid_ = fh_->insert_record(rec.data, context_);

        // 插入索引
        for (auto &index: tab_.indexes) {
            auto ih = sm_manager_->ihs_.at(sm_manager_->get_ix_manager()->get_index_name(tab_name_, index.cols)).get();
            char *key = new char[index.col_tot_len];
            int offset = 0;
            for (auto &col: index.cols) {
                memcpy(key + offset, rec.data + col.offset, col.len);
                offset += col.len;
            }
            auto page_no = ih->insert_entry(key, rid_, context_);
            if (page_no == INVALID_PAGE_ID) {
                // TODO 插入失败回滚
                fh_->delete_record(rid_, context_);
                throw InternalError("unique index key exit");
            }
//            ih->flush();
            delete[] key;
        }
//        context_->txn_->add_idx_log(context_->log_mgr_);
        // 写入事务
        auto log_rec = InsertLogRecord(context_->txn_->get_transaction_id(), context_->txn_->get_prev_lsn(),
                                       rec, rid_, tab_name_);
        auto lsn = context_->log_mgr_->add_log_to_buffer(&log_rec);
        context_->txn_->set_prev_lsn(lsn);
        WriteRecord write_record(WType::INSERT_TUPLE, tab_name_, rid_, lsn);
        context_->txn_->append_write_record(&write_record);
        return nullptr;
    }

    Rid &rid() override { return rid_; }
};