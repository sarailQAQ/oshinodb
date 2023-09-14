/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "transaction_manager.h"
#include "record/rm_file_handle.h"
#include "system/sm_manager.h"

std::unordered_map<txn_id_t, Transaction *> TransactionManager::txn_map = {};

/**
 * @description: 事务的开始方法
 * @return {Transaction*} 开始事务的指针
 * @param {Transaction*} txn 事务指针，空指针代表需要创建新事务，否则开始已有事务
 * @param {LogManager*} log_manager 日志管理器指针
 */
Transaction *TransactionManager::begin(Transaction *txn, LogManager *log_manager) {
    // done
    // 1. 判断传入事务参数是否为空指针
    // 2. 如果为空指针，创建新事务
    // 3. 把开始事务加入到全局事务表中
    // 4. 返回当前事务指针
    if (txn == nullptr) {
        txn = new Transaction(next_txn_id_++, IsolationLevel::SERIALIZABLE);
        txn->set_state(TransactionState::DEFAULT);
    }
    std::unique_lock<std::mutex> lock(latch_);
    auto log_rec = BeginLogRecord(txn->get_transaction_id(), txn->get_prev_lsn());
    auto lsn = log_manager->add_log_to_buffer(&log_rec);
    txn->set_prev_lsn(lsn);

    txn_map[txn->get_transaction_id()] = txn;
    return txn;
}

/**
 * @description: 事务的提交方法
 * @param {Transaction*} txn 需要提交的事务
 * @param {LogManager*} log_manager 日志管理器指针
 */
void TransactionManager::commit(Transaction *txn, LogManager *log_manager) {
    // done
    // 1. 如果存在未提交的写操作，提交所有的写操作
    // 2. 释放所有锁
    // 3. 释放事务相关资源，eg.锁集
    // 4. 把事务日志刷入磁盘中
    // 5. 更新事务状态
    if (txn == nullptr) return;

    add_idx_log(txn, log_manager);

    auto commit_rec = CommitLogRecord(txn->get_transaction_id(), txn->get_prev_lsn());
    auto lsn = log_manager->add_log_to_buffer(&commit_rec);
    txn->set_prev_lsn(lsn);
    auto end_rec = EndLogRecord(txn->get_transaction_id(), txn->get_prev_lsn());
    log_manager->add_log_to_buffer(&end_rec);
    log_manager->flush_log_to_disk();

    unpin_pages(txn);

    txn->get_write_set()->clear();

    auto lockset = txn->get_lock_set();
    //释放所有锁
    for (auto it = lockset->begin(); it != lockset->end(); ++it) lock_manager_->unlock(txn, *it);
    lockset->clear();

    txn->set_state(TransactionState::COMMITTED); //更新事务状态
}

inline void add_undo_log(Transaction *txn, LogManager *log_manager, lsn_t undo_next) {
    auto undo_rec = UndoNextLogRecord(txn->get_transaction_id(), txn->get_prev_lsn(), undo_next);
    auto lsn = log_manager->add_log_to_buffer(&undo_rec);
    txn->set_prev_lsn(lsn);
}

/**
 * @description: 事务的终止（回滚）方法
 * @param {Transaction *} txn 需要回滚的事务
 * @param {LogManager} *log_manager 日志管理器指针
 */
void TransactionManager::abort(Transaction *txn, LogManager *log_manager) {
    // Todo:
    // 1. 回滚所有写操作
    // 2. 释放所有锁
    // 3. 清空事务相关资源，eg.锁集
    // 4. 把事务日志刷入磁盘中
    // 5. 更新事务状态
    if (txn == nullptr) throw InternalError("TransactionManager::abort transaction point is nullptr");

    add_idx_log(txn, log_manager);

    auto log_rec = AbortLogRecord(txn->get_transaction_id(), txn->get_prev_lsn());
    auto lsn = log_manager->add_log_to_buffer(&log_rec);
    txn->set_prev_lsn(lsn);
    log_manager->flush_log_to_disk();

    auto write_set = txn->get_write_set().get();
    std::vector<lsn_t> lsns;
    for (auto &item: *write_set) lsns.push_back(item.get_lsn());
    if (std::is_sorted(lsns.begin(), lsns.end())) std::cerr << "unordered write set\n";

    auto context = new Context(lock_manager_, log_manager, txn);

    while (!write_set->empty()) {
        auto item = write_set->back();
        write_set->pop_back();

        switch (item.GetWriteType()) {
            case WType::INSERT_TUPLE: {
                sm_manager_->rollback_insert(item.GetTableName(), item.GetRid(), context);
                break;
            }
            case WType::UPDATE_TUPLE: {
                sm_manager_->rollback_update(item.GetTableName(), item.GetRid(), item.GetRecord(), context);
                break;
            }
            case WType::DELETE_TUPLE: {
                sm_manager_->rollback_delete(item.GetTableName(), item.GetRid(), item.GetRecord(), context);
                break;
            }
            case WType::CREATE_INDEX: {
                sm_manager_->rollback_create_index(item.GetTableName(), item.GetColNames(), context);
                break;
            }
            case WType::DROP_INDEX: {
                sm_manager_->rollback_drop_index(item.GetTableName(), item.GetColNames(), context);
                break;
            }
            default:
                break;
        }

        auto idx = std::lower_bound(lsns.begin(), lsns.end(), item.get_lsn()) - lsns.begin();
        auto undo_next = idx == 0 ? -1 : lsns[idx - 1];
        add_undo_log(txn, log_manager, undo_next);
        add_idx_log(txn, log_manager);
//            log_manager->flush_log_to_disk();

    }
    delete context;

    auto end_rec = EndLogRecord(txn->get_transaction_id(), txn->get_prev_lsn());
    log_manager->add_log_to_buffer(&end_rec);
    log_manager->flush_log_to_disk();

    unpin_pages(txn);

    auto lockset = txn->get_lock_set();
    for (auto it: *lockset) { //释放所有锁
        lock_manager_->unlock(txn, it);
    }
    lockset->clear();

    txn->set_state(TransactionState::ABORTED); //更新事务状态
}