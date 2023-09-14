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

class DeleteExecutor : public AbstractExecutor {
   private:
    TabMeta tab_;                   // 表的元数据
    std::vector<Condition> conds_;  // delete的条件
    RmFileHandle *fh_;              // 表的数据文件句柄
    std::vector<Rid> rids_;         // 需要删除的记录的位置
    std::string tab_name_;          // 表名称
    SmManager *sm_manager_;

   public:
    DeleteExecutor(SmManager *sm_manager, const std::string &tab_name, std::vector<Condition> conds,
                   std::vector<Rid> rids, Context *context) {
        sm_manager_ = sm_manager;
        tab_name_ = tab_name;
        tab_ = sm_manager_->db_.get_table(tab_name);
        fh_ = sm_manager_->fhs_.at(tab_name).get();
        conds_ = conds;
        rids_ = rids;
        context_ = context;
        if (!context->lock_mgr_->lock_exclusive_on_table(context->txn_, fh_->GetFd()))
            throw TransactionAbortException(context->txn_->get_transaction_id(), AbortReason::DEADLOCK_PREVENTION);
    }

    std::unique_ptr<RmRecord> Next() override {
        fh_ = sm_manager_->fhs_.at(tab_name_).get();

        for (auto rid: rids_) {
            auto old_rec = fh_->get_record(rid, context_);

            // 尝试更新索引
            for (auto& index: tab_.indexes) {
                auto ih = sm_manager_->ihs_.at(sm_manager_->get_ix_manager()->get_index_name(tab_name_, index.cols)).get();
                char *old_key = new char[index.col_tot_len];
                int offset = 0;
                for (auto & col : index.cols) {
                    memcpy(old_key + offset, old_rec->data + col.offset, col.len);
                    offset += col.len;
                }

                auto ok = ih->delete_entry(old_key, context_);
                if (!ok) throw InternalError("UpdateExecutor: delete old_key failed\n");
//                ih->flush();
                delete[] old_key;
            }

//            context_->txn_->add_idx_log(context_->log_mgr_);
            fh_->delete_record(rid, context_);

            // 事务相关操作
            auto log_rec = DeleteLogRecord(context_->txn_->get_transaction_id(), context_->txn_->get_prev_lsn(),
                                           *old_rec, rid, tab_name_);
            auto lsn = context_->log_mgr_->add_log_to_buffer(&log_rec);
            context_->txn_->set_prev_lsn(lsn);
            WriteRecord write_record(WType::DELETE_TUPLE, tab_name_, rid, *old_rec, lsn);
            context_->txn_->append_write_record(&write_record);
        }
        return nullptr;
    }

    Rid &rid() override { return _abstract_rid; }
};