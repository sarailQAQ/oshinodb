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

class UpdateExecutor : public AbstractExecutor {
private:
    TabMeta tab_;
    std::vector<Condition> conds_;
    RmFileHandle *fh_;
    std::vector<Rid> rids_;
    std::string tab_name_;
    std::vector<SetClause> set_clauses_;
    SmManager *sm_manager_;

public:
    UpdateExecutor(SmManager *sm_manager, const std::string &tab_name, std::vector<SetClause> set_clauses,
                   std::vector<Condition> conds, std::vector<Rid> rids, Context *context) {
        sm_manager_ = sm_manager;
        tab_name_ = tab_name;
        set_clauses_ = set_clauses;
        tab_ = sm_manager_->db_.get_table(tab_name);
        fh_ = sm_manager_->fhs_.at(tab_name).get();
        conds_ = conds;
        rids_ = rids;
        context_ = context;

        // 加意向锁
        if (!context->lock_mgr_->lock_exclusive_on_table(context->txn_, fh_->GetFd()))
            throw TransactionAbortException(context->txn_->get_transaction_id(), AbortReason::DEADLOCK_PREVENTION);

    }

    std::unique_ptr<RmRecord> Next() override {
        fh_ = sm_manager_->fhs_.at(tab_name_).get();

        for (auto rid: rids_) {
            // 加锁
            auto old_rec = fh_->get_record(rid, nullptr); // 不要加锁
            auto new_rec = *old_rec;
            // 根据set条件，修改生成新的记录
            for (auto &set_clause: set_clauses_) {
                auto lhs_col = tab_.get_col(set_clause.lhs.col_name);
                // 尽力的类型转换
                if (lhs_col->type != set_clause.rhs.type) {
                    if (lhs_col->type == TYPE_BIGINT && set_clause.rhs.type == TYPE_INT) {
                        set_clause.rhs.set_bigint(set_clause.rhs.int_val);
                        set_clause.rhs.init_raw(lhs_col->len);
                    } else if (lhs_col->type == TYPE_DATETIME && set_clause.rhs.type == TYPE_STRING) {
                        if (!set_clause.rhs.is_valid_datetime()) throw InternalError("invalid datetime");
                    } else throw IncompatibleTypeError(coltype2str(lhs_col->type), coltype2str(set_clause.rhs.type));
                }
                // 更新record
                memcpy(new_rec.data + lhs_col->offset, set_clause.rhs.to_bytes(), lhs_col->len);
            }

            // 尝试更新索引
            for (auto &index: tab_.indexes) {
                auto ih = sm_manager_->ihs_.at(
                        sm_manager_->get_ix_manager()->get_index_name(tab_name_, index.cols)).get();
                char *old_key = new char[index.col_tot_len], *new_key = new char[index.col_tot_len];
                int offset = 0;
                for (auto &col: index.cols) {
                    memcpy(old_key + offset, old_rec->data + col.offset, col.len);
                    memcpy(new_key + offset, new_rec.data + col.offset, col.len);
                    offset += col.len;
                }
                // 索引没变化
                if (std::strcmp(old_key, new_key) == 0) continue;
                // todo 更新失败回滚
                auto page_no = ih->insert_entry(new_key, rid, context_);
                if (page_no == INVALID_PAGE_ID) throw InternalError("UpdateExecutor: unique index new_key exit");
                auto ok = ih->delete_entry(old_key, context_);
                if (!ok) std::cerr << "UpdateExecutor: delete old_key failed\n";

                auto iid = ih->lower_bound(new_key);
                assert(ih->get_rid(iid) == rid);
                iid = ih->upper_bound(old_key);
                assert(iid == ih->leaf_end());
                delete[] old_key;
                delete[] new_key;
//                ih->flush();
            }

//            context_->txn_->add_idx_log(context_->log_mgr_);
            fh_->update_record(rid, new_rec.data, context_);

            // 写入日志
            auto log_rec = UpdateLogRecord(context_->txn_->get_transaction_id(), context_->txn_->get_prev_lsn(),
                                           new_rec,*old_rec, rid, tab_name_);
            auto lsn = context_->log_mgr_->add_log_to_buffer(&log_rec);
            context_->txn_->set_prev_lsn(lsn);

            WriteRecord write_record(WType::UPDATE_TUPLE, tab_name_, rid, *old_rec, lsn);
            context_->txn_->append_write_record(&write_record);
        }
        return nullptr;
    }

    Rid &rid() override { return _abstract_rid; }
};