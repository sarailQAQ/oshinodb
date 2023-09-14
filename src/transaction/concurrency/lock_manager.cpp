/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "lock_manager.h"

#include <utility>

/**
 * @description: 申请行级共享锁
 * @return {bool} 加锁是否成功
 * @param {Transaction*} txn 要申请锁的事务对象指针
 * @param {Rid&} rid 加锁的目标记录ID 记录所在的表的fd
 * @param {int} tab_fd
 */
bool LockManager::lock_shared_on_record(Transaction* txn, const Rid& rid, int fd) {
    // 1. 通过mutex申请访问全局锁表
    // 2. 检查事务的状态
    // 3. 查找当前事务是否已经申请了目标数据项上的锁，如果存在则根据锁类型进行操作，否则执行下一步操作
    // 4. 将要申请的锁放入到全局锁表中，并通过组模式来判断是否可以成功授予锁
    // 5. 如果成功，更新目标数据项在全局锁表中的信息，否则阻塞当前操作
    // 提示：步骤5中的阻塞操作可以通过条件变量来完成，所有加锁操作都遵循上述步骤，在下面的加锁操作中不再进行注释提示

    return true;
}

/**
 * @description: 申请行级排他锁
 * @return {bool} 加锁是否成功
 * @param {Transaction*} txn 要申请锁的事务对象指针
 * @param {Rid&} rid 加锁的目标记录ID
 * @param {int} tab_fd 记录所在的表的fd
 */
bool LockManager::lock_exclusive_on_record(Transaction* txn, const Rid& rid, int fd) {


    return true;
}

/**
 * @description: 申请表级读锁
 * @return {bool} 返回加锁是否成功
 * @param {Transaction*} txn 要申请锁的事务对象指针
 * @param {int} tab_fd 目标表的fd
 */
bool LockManager::lock_shared_on_table(Transaction* txn, int fd) {
    std::unique_lock<std::mutex> lock(latch_);
    //检查事务状态，处于SHRINKING状态不支持加锁
    if (txn->get_isolation_level() == IsolationLevel::READ_UNCOMMITTED) {
        txn->set_state(TransactionState::ABORTED);
        throw TransactionAbortException(txn->get_transaction_id(), AbortReason::LOCK_ON_SHIRINKING);
    }
    txn->set_state(TransactionState::GROWING); //将事务状态设置为扩张期
    //查找是否申请过目标数据项的锁
    auto lock_data_id = LockDataId(fd, LockDataType::TABLE);
    // 如果事务已经获得了这个锁, 不管什么类型, 都是合法的
    if (txn->get_lock_set()->count(lock_data_id))  return true;
//    // 其他事务获得了这个锁
    if(lock_table_.count(lock_data_id)) {
        if (lock_table_[lock_data_id].mode == GroupLockMode::S) {
            lock_table_[lock_data_id].shared_num++;
            txn->get_lock_set()->insert(lock_data_id);
            return true;
        }
        return false;
    }

    txn->get_lock_set()->insert(lock_data_id);
    //插入到全局锁表，granted_默认false
    lock_table_[lock_data_id] = {1, GroupLockMode::S};
    return true;
}

/**
 * @description: 申请表级写锁
 * @return {bool} 返回加锁是否成功
 * @param {Transaction*} txn 要申请锁的事务对象指针
 * @param {int} tab_fd 目标表的fd
 */
bool LockManager::lock_exclusive_on_table(Transaction* txn, int fd) {
    std::unique_lock<std::mutex> lock(latch_);
    //检查事务状态，处于SHRINKING状态不支持加锁
    if (txn->get_isolation_level() == IsolationLevel::READ_UNCOMMITTED) {
        txn->set_state(TransactionState::ABORTED);
        throw TransactionAbortException(txn->get_transaction_id(), AbortReason::LOCK_ON_SHIRINKING);
    }
    txn->set_state(TransactionState::GROWING); //将事务状态设置为扩张期

    //查找是否申请过目标数据项的锁
    auto lock_data_id = LockDataId(fd, LockDataType::TABLE);
    // 如果事务已经获得了这个锁
    if (txn->get_lock_set()->count(lock_data_id)) {
        auto status = lock_table_[lock_data_id];
        if (status.mode == GroupLockMode::S ){
            // 多个读锁
            if (status.shared_num > 1) return false;
            else {
                status.mode = GroupLockMode::X;
                lock_table_[lock_data_id] = status;
                return true;
            }
        }
        return true;
    }
    // 其他事务获得了这个锁
    if(lock_table_.count(lock_data_id)) return false;

    txn->get_lock_set()->insert(lock_data_id);
    //插入到全局锁表，granted_默认false
    lock_table_[lock_data_id] = LockStatus{0, GroupLockMode::X};
    return true;
}

/**
 * @description: 申请表级意向读锁
 * @return {bool} 返回加锁是否成功
 * @param {Transaction*} txn 要申请锁的事务对象指针
 * @param {int} tab_fd 目标表的fd
 */
bool LockManager::lock_IS_on_table(Transaction* txn, int fd) {

    return true;
}

/**
 * @description: 申请表级意向写锁
 * @return {bool} 返回加锁是否成功
 * @param {Transaction*} txn 要申请锁的事务对象指针
 * @param {int} tab_fd 目标表的fd
 */
bool LockManager::lock_IX_on_table(Transaction* txn, int fd) {

    return true;
}

/**
 * @description: 释放锁
 * @return {bool} 返回解锁是否成功
 * @param {Transaction*} txn 要释放锁的事务对象指针
 * @param {LockDataId} lock_data_id 要释放的锁ID
 */
bool LockManager::unlock(Transaction* txn, LockDataId lock_data_id) {
    std::unique_lock<std::mutex> lock(latch_);
    txn->set_state(TransactionState::SHRINKING);
    if (!lock_table_.count(lock_data_id)) return false;
    if (lock_table_[lock_data_id].mode == GroupLockMode::S) {
        lock_table_[lock_data_id].shared_num--;
        if (lock_table_[lock_data_id].shared_num <= 0) lock_table_.erase(lock_data_id);
    } else lock_table_.erase(lock_data_id);


    return true;
}