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

#include <map>
#include <unordered_map>
#include "log_manager.h"
#include "storage/disk_manager.h"
#include "system/sm_manager.h"

class RedoLogsInPage {
public:
    RedoLogsInPage() { table_file_ = nullptr; }
    RmFileHandle* table_file_;
    std::vector<lsn_t> redo_logs_;   // 在该page上需要redo的操作的lsn
};

 // class ATT: Active Transaction Table
 class ATT {
 public:
     ATT() {}

     ~ATT() {}

     // add a new transaction entry to the table
     void add_transaction(int transactionId, int lastLSN) {
         TransactionEntry entry;
         entry.transactionId = transactionId;
         entry.lastLSN = lastLSN;
         table.push_back(entry);
     }

     // remove a transaction entry from the table
     void remove_transaction(int transactionId) {
         for (auto it = table.begin(); it != table.end(); ++it) {
             if (it->transactionId == transactionId) {
                 table.erase(it);
                 break;
             }
         }
     }

     // update a transaction entry's lastLSN
     void update_lastLSN(int transactionId, int lastLSN) {
         for (auto& entry : table) {
             if (entry.transactionId == transactionId) {
                 entry.lastLSN = lastLSN;
                 break;
             }
         }
     }

     // get a transaction entry's lastLSN
     int get_lastLSN(int transactionId) {
         for (auto& entry : table) {
             if (entry.transactionId == transactionId) {
                 return entry.lastLSN;
             }
         }
         return -1; // not found
     }

     int get_maxLSN(){
         int max = -1;
         for(auto entry: table){
             if(entry.lastLSN>max) max=entry.lastLSN;
         }
         return max;
     }

 private:
     // a struct to represent a transaction entry
     struct TransactionEntry {
         int transactionId; // unique identifier of the transaction
         int lastLSN; // the LSN of the most recent log record for this transaction
     };

     std::vector<TransactionEntry> table; // a vector to store the transaction entries
 };

 // class DPT: Dirty Page Table
 class DPT {
 public:
     DPT() {}

     ~DPT() {}

     // add a new page entry to the table
     void add_page(int pageId, int recLSN) {
         PageEntry entry;
         entry.pageId = pageId;
         entry.recLSN = recLSN;
         table.push_back(entry);
     }

     bool exist_page(int pageId){
         for(auto &page_entry: table){
             if(page_entry.pageId == pageId) return true;
         }
         return false;
     }

     // remove a page entry from the table
     void remove_page(int pageId) {
         for (auto it = table.begin(); it != table.end(); ++it) {
             if (it->pageId == pageId) {
                 table.erase(it);
                 break;
             }
         }
     }

     // update a page entry's recLSN
     void update_recLSN(int pageId, int recLSN) {
         for (auto& entry : table) {
             if (entry.pageId == pageId) {
                 entry.recLSN = recLSN;
                 break;
             }
         }
     }

     // get a page entry's recLSN
     int get_recLSN(int pageId) {
         for (auto& entry : table) {
             if (entry.pageId == pageId) {
                 return entry.recLSN;
             }
         }
         return -1; // not found
     }

     int get_minLSN(){
         int min = 0x7fffffff;
         for(auto entry: table){
             if(entry.recLSN<min) min=entry.recLSN;
         }
         return min;
     }

 private:
     // a struct to represent a page entry
     struct PageEntry {
         int pageId; // unique identifier of the page
         int recLSN; // the LSN of the first log record that caused the page to be dirty
     };

     std::vector<PageEntry> table; // a vector to store the page entries
 };


class RecoveryManager {
public:
    RecoveryManager(DiskManager* disk_manager, BufferPoolManager* buffer_pool_manager, SmManager* sm_manager) {
        disk_manager_ = disk_manager;
        buffer_pool_manager_ = buffer_pool_manager;
        sm_manager_ = sm_manager;
    }

    void analyze();
    void redo();
    void undo();
private:
    LogBuffer buffer_;                                              // 读入日志
    DiskManager* disk_manager_;                                     // 用来读写文件
    BufferPoolManager* buffer_pool_manager_;                        // 对页面进行读写
    SmManager* sm_manager_;                                         // 访问数据库元数据

    /** Maintain active transactions and its corresponding latest lsn. */
    std::unordered_map<txn_id_t, lsn_t> active_txn_;
    std::unordered_map<txn_id_t, TxnStatus> txn_status;
    /** Mapping the log sequence number to log file offset for undos. */
    std::unordered_map<lsn_t, int> lsn_mapping_;
    // DPT
    std::unordered_map<int , lsn_t> dirty_page_;
};

