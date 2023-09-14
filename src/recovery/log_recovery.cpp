/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "log_recovery.h"

/**
 * @description: analyze阶段，需要获得脏页表（DPT）和未完成的事务列表（ATT）
 */
void RecoveryManager::analyze() {
    //从最近的一次 Checkpoint Log 的位置开始，顺序扫描所有 Log，恢复出 ATT 和 DPT。
    // ATT：对任意一个事务 Ti ，如果
    //      遇到 Ti 的 Begin Log，就把 Ti 加入 ATT。
    //      遇到 Ti 的 Commit Log/Abort Log，就把 ATT 中的 Ti 去掉。
    // DPT：遇到任意 Redo Log 或 CLR，如果对应的 Page 不在 DPT 中：将它加入 DPT，同时记录 Rec LSN。
    //      已经在 DPT 中：无需处理。
    int offset = 0;
    char log_hdr[LOG_HEADER_SIZE];
    int final_lsn;

    while (disk_manager_->read_log(log_hdr, LOG_HEADER_SIZE, offset) > 0) {
        LogRecord log_rec;
        log_rec.deserialize(log_hdr);
        final_lsn = log_rec.lsn_;

        if (log_rec.log_type_ == LogRecordType::BEGIN || log_rec.log_type_ == LogRecordType::COMMIT
            || log_rec.log_type_ == LogRecordType::ABORT) {
            active_txn_[log_rec.log_tid_] = log_rec.lsn_;

            if (log_rec.log_type_ == LogRecordType::BEGIN) txn_status[log_rec.log_tid_] = TxnStatus::UndoCandidate;
            else if (log_rec.log_type_ == LogRecordType::ABORT) txn_status[log_rec.log_tid_] = TxnStatus::Aborting;
            else txn_status[log_rec.log_tid_] = TxnStatus::Committed;
        } else if (log_rec.log_type_ == LogRecordType::END) {
            txn_status.erase(log_rec.log_tid_);
            active_txn_.erase(log_rec.log_tid_);
        } else {
            char log[log_rec.log_tot_len_ + 1];
            if (log_rec.log_type_ == LogRecordType::UPDATE) {
                disk_manager_->read_log(log, log_rec.log_tot_len_, offset);
                UpdateLogRecord rec;
                rec.deserialize(log);
                // rec.format_print();
                if (dirty_page_.count(rec.rid_.page_no) != 0) {
                    dirty_page_[rec.rid_.page_no] = rec.lsn_;
                }
                active_txn_[rec.log_tid_] = rec.lsn_;
            } else if (log_rec.log_type_ == LogRecordType::INSERT) {
                disk_manager_->read_log(log, log_rec.log_tot_len_, offset);
                InsertLogRecord rec;
                rec.deserialize(log);
                // rec.format_print();
                if (dirty_page_.count(rec.rid_.page_no) != 0) {
                    dirty_page_[rec.rid_.page_no] = rec.lsn_;
                }
                active_txn_[rec.log_tid_] = rec.lsn_;
            } else if (log_rec.log_type_ == LogRecordType::DELETE) {
                disk_manager_->read_log(log, log_rec.log_tot_len_, offset);
                DeleteLogRecord rec;
                rec.deserialize(log);
                // rec.format_print();
                if (dirty_page_.count(rec.rid_.page_no) != 0) {
                    dirty_page_[rec.rid_.page_no] = rec.lsn_;
                }
                active_txn_[rec.log_tid_] = rec.lsn_;
            } else if (log_rec.log_type_ == LogRecordType::PAGE_SET) {
                // redo
                disk_manager_->read_log(log, log_rec.log_tot_len_, offset);
                PageLogRecord rec;
                rec.deserialize(log);
                if (dirty_page_.count(rec.page_no) != 0) {
                    dirty_page_[rec.page_no] = rec.lsn_;
                }
                active_txn_[rec.log_tid_] = rec.lsn_;
            }
        }
        lsn_mapping_[log_rec.lsn_] = offset;
        offset += log_rec.log_tot_len_;
    }
}

std::pair<txn_id_t, lsn_t> get_min_lsn(std::unordered_map<int, lsn_t> dirty_page) {
    int min = 0x7fffffff;
    int txn;
    for (auto &kv: dirty_page) {
        if (kv.second < min) {
            txn = kv.first;
            min = kv.second;
        }
    }
    return std::make_pair(txn, min);
}

/**
 * @description: 重做所有未落盘的操作
 */
void RecoveryManager::redo() {
    // 找到 DPT 中最小的 Rec LSN，将它作为起始点，顺序扫描 Log 并处理来重放历史（实际上就是 Redo 所有事务的 Redo Log 以及 CLR 对应的更新操作）。
    // 当且仅当 Log LSN > Page LSN，一个 Log 对应的更新操作才能在对应的 Page 上 Redo。（由于 Buffer Pool 可能在 Checkpoint 后对 Page 进行刷盘，所以 Log LSN < Page LSN 的可能性是很大的。）
    std::pair<txn_id_t, lsn_t> min_pair = get_min_lsn(dirty_page_);

    int offset = lsn_mapping_[min_pair.second];
    char log_hdr[LOG_HEADER_SIZE];

    while (disk_manager_->read_log(log_hdr, LOG_HEADER_SIZE, offset) > 0) {
        LogRecord log_rec;
        log_rec.deserialize(log_hdr);
        char log[log_rec.log_tot_len_ + 1];
        if (log_rec.log_type_ == LogRecordType::PAGE_SET) {
            disk_manager_->read_log(log, log_rec.log_tot_len_, offset);
            PageLogRecord rec;
            rec.deserialize(log);
            auto file_handle = sm_manager_->fhs_.at(rec.tab_name).get();

            if (rec.page_no == 0) {
                if (file_handle->file_hdr_.lsn < rec.lsn_)
                    file_handle->file_hdr_ = *(RmFileHdr *) rec.new_page;
            } else {
                auto page_handle = file_handle->fetch_page_handle(rec.page_no);
                if (page_handle.page->get_page_lsn() < rec.lsn_) {
                    memmove(page_handle.page, rec.new_page, PAGE_SIZE);
                    buffer_pool_manager_->unpin_page(page_handle.page->get_page_id(), true);
                } else buffer_pool_manager_->unpin_page(page_handle.page->get_page_id(), false);
            }
        } else if (log_rec.log_type_ == LogRecordType::INDEX_PAGE) {
            disk_manager_->read_log(log, log_rec.log_tot_len_, offset);
            IndexPagesLogRecord rec;
            rec.deserialize(log);

            auto ih = sm_manager_->ihs_.at(rec.idx_name).get();
            for (int i = 0; i < rec.pages.size(); i++) {
                auto node = ih->fetch_node(rec.page_ids[i].page_no);
                if (node->page->get_page_lsn() < rec.lsn_) {
                    memmove(node->page->get_data(), rec.pages[i], PAGE_SIZE);
                    buffer_pool_manager_->unpin_page(node->get_page_id(), true);
                } else buffer_pool_manager_->unpin_page(node->get_page_id(), false);
            }


            if (ih->file_hdr_->lsn < rec.lsn_)
                ih->file_hdr_->deserialize(rec.file_hdr);
        }

        offset += (int )log_rec.log_tot_len_;
    }
}

std::pair<txn_id_t, lsn_t> get_max_lsn(const std::unordered_map<txn_id_t, lsn_t>& active_txn) {
    int max = -1;
    int txn;
    for (auto &kv: active_txn) {
        if (kv.second > max) {
            txn = kv.first;
            max = kv.second;
        }
    }
    return std::make_pair(txn, max);
}

/**
 * @description: 回滚未完成的事务
 */
void RecoveryManager::undo() {
    // 找到 ATT 中最大的 Last LSN，Undo 它对应的事务，Undo 完成后把该事务从 ATT 中移除。
    // 重复上面步骤，直到 ATT 为空。
    while (!active_txn_.empty()) {
        std::pair<txn_id_t, lsn_t> max_pair = get_max_lsn(active_txn_);
        auto status = txn_status[max_pair.first];
        if (status == TxnStatus::Committed) continue;
        int offset = lsn_mapping_[max_pair.second];

        int prev_lsn;
        int tid;
        while (true) {
            char log_hdr[LOG_HEADER_SIZE + 1];
            disk_manager_->read_log(log_hdr, LOG_HEADER_SIZE, offset);
            LogRecord log_rec;
            log_rec.deserialize(log_hdr);
            tid = log_rec.log_tid_;

            char log[log_rec.log_tot_len_ + 1];
            if (status == TxnStatus::Aborting) {
                status = TxnStatus::UndoCandidate;
                if (log_rec.log_type_ == LogRecordType::UNDO_NEXT) {
                    disk_manager_->read_log(log, (int )log_rec.log_tot_len_, offset);
                    UndoNextLogRecord rec;
                    rec.deserialize(log);
                    if (rec.undo_next < 0) break;
                    offset = lsn_mapping_[rec.undo_next];
                    continue;
                }
//                if (log_rec.log_type_ != LogRecordType::ABORT) throw InternalError("invalid clr log record in undo phase");
            }
            if (log_rec.log_type_ == LogRecordType::UPDATE) {
                disk_manager_->read_log(log, (int )log_rec.log_tot_len_, offset);
                UpdateLogRecord rec;
                rec.deserialize(log);

                prev_lsn = rec.lsn_;
                sm_manager_->rollback_update(rec.table_name_, rec.rid_, rec.old_value_, nullptr);
            } else if (log_rec.log_type_ == LogRecordType::DELETE) {
                disk_manager_->read_log(log, (int )log_rec.log_tot_len_, offset);
                DeleteLogRecord rec;
                rec.deserialize(log);
                prev_lsn = rec.lsn_;
                sm_manager_->rollback_delete(rec.table_name_, rec.rid_, rec.delete_value_, nullptr);
            } else if (log_rec.log_type_ == LogRecordType::INSERT) {
                disk_manager_->read_log(log, (int )log_rec.log_tot_len_, offset);
                InsertLogRecord rec;
                rec.deserialize(log);

                prev_lsn = rec.lsn_;
                sm_manager_->rollback_insert(rec.table_name_, rec.rid_, nullptr);
            } else if (log_rec.log_type_ == LogRecordType::CREATE_INDEX) {
                disk_manager_->read_log(log, (int)log_rec.log_tot_len_, offset);
                CreateIndexLogRecord rec;
                rec.deserialize(log);

                sm_manager_->rollback_create_index(rec.tab_name, rec.col_names, nullptr);
            } else if (log_rec.log_type_ == LogRecordType::DROP_INDEX) {
                disk_manager_->read_log(log, (int)log_rec.log_tot_len_, offset);
                DropIndexLogRecord rec;
                rec.deserialize(log);

                sm_manager_->rollback_drop_index(rec.tab_name, rec.col_names, nullptr);
            }
            // todo CLR 日志
            if (log_rec.prev_lsn_ == -1) break;
            offset = lsn_mapping_[log_rec.prev_lsn_];
        }

        active_txn_.erase(max_pair.first);
        txn_status.erase(max_pair.first);
    }
}