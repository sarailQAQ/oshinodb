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

#include <mutex>
#include <utility>
#include <vector>
#include <iostream>
#include "log_defs.h"
#include "common/config.h"
#include "record/rm_defs.h"

/* 日志记录对应操作的类型 */
enum LogRecordType : int {
    UPDATE = 0,
    INSERT,
    DELETE,
    BEGIN,
    COMMIT,
    ABORT,
    END,
    PAGE_SET,
    UNDO_NEXT,
    INDEX_PAGE,
    CREATE_INDEX,
    DROP_INDEX
};

enum TxnStatus {
    UndoCandidate,
    Committed,
    Aborting
};

static std::string LogTypeStr[] = {
        "UPDATE",
        "INSERT",
        "DELETE",
        "BEGIN",
        "COMMIT",
        "ABORT",
        "END",
        "PAGE_SET",
        "UNDO_NEXT",
        "INDEX_PAGE",
        "CREATE_INDEX",
        "DROP_INDEX"
};

class LogRecord {
public:
    LogRecordType log_type_;         /* 日志对应操作的类型 */
    lsn_t lsn_;                /* 当前日志的lsn */
    uint32_t log_tot_len_;     /* 整个日志记录的长度 */
    txn_id_t log_tid_;         /* 创建当前日志的事务ID */
    lsn_t prev_lsn_;           /* 事务的前一条日志记录的lsn，用于undo */

    LogRecord() {
        log_type_ = LogRecordType::BEGIN;
        lsn_ = -1;
        log_tot_len_ = 0;
        log_tid_ = -1;
        prev_lsn_ = -1;
    }

    LogRecord(const LogRecord &other) {
        log_type_ = other.log_type_;
        lsn_ = other.lsn_;
        log_tot_len_ = other.log_tot_len_;
        log_tid_ = other.log_tid_;
        prev_lsn_ = other.prev_lsn_;
    }

    // 把日志记录序列化到dest中
    virtual void serialize(char *dest) const {
        memcpy(dest + OFFSET_LOG_TYPE, &log_type_, sizeof(LogRecordType));
        memcpy(dest + OFFSET_LSN, &lsn_, sizeof(lsn_t));
        memcpy(dest + OFFSET_LOG_TOT_LEN, &log_tot_len_, sizeof(uint32_t));
        memcpy(dest + OFFSET_LOG_TID, &log_tid_, sizeof(txn_id_t));
        memcpy(dest + OFFSET_PREV_LSN, &prev_lsn_, sizeof(lsn_t));
    }

    // 从src中反序列化出一条日志记录
    virtual void deserialize(const char *src) {
        log_type_ = *reinterpret_cast<const LogRecordType *>(src);
        lsn_ = *reinterpret_cast<const lsn_t *>(src + OFFSET_LSN);
        log_tot_len_ = *reinterpret_cast<const uint32_t *>(src + OFFSET_LOG_TOT_LEN);
        log_tid_ = *reinterpret_cast<const txn_id_t *>(src + OFFSET_LOG_TID);
        prev_lsn_ = *reinterpret_cast<const lsn_t *>(src + OFFSET_PREV_LSN);
    }

    // used for debug
    virtual void format_print() {
        std::cout << "log type in father_function: " << LogTypeStr[log_type_] << "\n";
        printf("Print Log Record:\n");
        printf("log_type_: %s\n", LogTypeStr[log_type_].c_str());
        printf("lsn: %d\n", lsn_);
        printf("log_tot_len: %d\n", log_tot_len_);
        printf("log_tid: %d\n", log_tid_);
        printf("prev_lsn: %d\n", prev_lsn_);
    }
};

/**
 * For every write operation on the table page, you should write ahead a corresponding log record.
 *
 * For EACH log record, HEADER is like (5 fields in common, 20 bytes in total).
 *---------------------------------------------
 * | size | LSN | transID | prevLSN | LogRecordType |
 *---------------------------------------------
 * For insert type log record
 *---------------------------------------------------------------
 * | HEADER | tuple_rid | tuple_size | tuple_data(char[] array) |
 *---------------------------------------------------------------
 * For delete type (including markdelete, rollbackdelete, applydelete)
 *----------------------------------------------------------------
 * | HEADER | tuple_rid | tuple_size | tuple_data(char[] array) |
 *---------------------------------------------------------------
 * For update type log record
 *-----------------------------------------------------------------------------------
 * | HEADER | tuple_rid | tuple_size | old_tuple_data | tuple_size | new_tuple_data |
 *-----------------------------------------------------------------------------------
 * For new page type log record
 *--------------------------
 * | HEADER | prev_page_id |
 *--------------------------
 */

class BeginLogRecord : public LogRecord {
public:
    BeginLogRecord() {
        log_type_ = LogRecordType::BEGIN;
        lsn_ = INVALID_LSN;
        log_tot_len_ = LOG_HEADER_SIZE;
        log_tid_ = INVALID_TXN_ID;
        prev_lsn_ = INVALID_LSN;
    }

    BeginLogRecord(txn_id_t txn_id, int prev_lsn) : BeginLogRecord() {
        log_tid_ = txn_id;
        prev_lsn_ = prev_lsn;
    }

    // 序列化Begin日志记录到dest中
    void serialize(char *dest) const override {
        LogRecord::serialize(dest);
    }

    // 从src中反序列化出一条Begin日志记录
    void deserialize(const char *src) override {
        LogRecord::deserialize(src);
    }

    virtual void format_print() override {
        std::cout << "log type in son_function: " << LogTypeStr[log_type_] << "\n";
        LogRecord::format_print();
    }
};

/**
 * commit操作的日志记录
*/
class CommitLogRecord : public LogRecord {
public:
    CommitLogRecord() {
        log_type_ = LogRecordType::COMMIT;
        lsn_ = INVALID_LSN;
        log_tot_len_ = LOG_HEADER_SIZE;
        log_tid_ = INVALID_TXN_ID;
        prev_lsn_ = INVALID_LSN;
    }

    CommitLogRecord(txn_id_t txn_id, int prev_lsn) : CommitLogRecord() {
        log_tid_ = txn_id;
        prev_lsn_ = prev_lsn;
    }

    // 序列化Begin日志记录到dest中
    void serialize(char *dest) const override {
        LogRecord::serialize(dest);
    }

    // 从src中反序列化出一条Begin日志记录
    void deserialize(const char *src) override {
        LogRecord::deserialize(src);
    }

    virtual void format_print() override {
        std::cout << "log type in son_function: " << LogTypeStr[log_type_] << "\n";
        LogRecord::format_print();
    }
};

class EndLogRecord : public LogRecord {
public:
    EndLogRecord() {
        log_type_ = LogRecordType::END;
        lsn_ = INVALID_LSN;
        log_tot_len_ = LOG_HEADER_SIZE;
        log_tid_ = INVALID_TXN_ID;
        prev_lsn_ = INVALID_LSN;
    }

    EndLogRecord(txn_id_t txn_id, int prev_lsn) : EndLogRecord() {
        log_tid_ = txn_id;
        prev_lsn_ = prev_lsn;
    }

    // 序列化Begin日志记录到dest中
    void serialize(char *dest) const override {
        LogRecord::serialize(dest);
    }

    // 从src中反序列化出一条Begin日志记录
    void deserialize(const char *src) override {
        LogRecord::deserialize(src);
    }

    virtual void format_print() override {
        std::cout << "log type in son_function: " << LogTypeStr[log_type_] << "\n";
        LogRecord::format_print();
    }
};

/**
 * abort操作的日志记录
*/
class AbortLogRecord : public LogRecord {
public:
    AbortLogRecord() {
        log_type_ = LogRecordType::ABORT;
        lsn_ = INVALID_LSN;
        log_tot_len_ = LOG_HEADER_SIZE;
        log_tid_ = INVALID_TXN_ID;
        prev_lsn_ = INVALID_LSN;
    }

    AbortLogRecord(txn_id_t txn_id, int prev_lsn) : AbortLogRecord() {
        log_tid_ = txn_id;
        prev_lsn_ = prev_lsn;
    }

    // 序列化Begin日志记录到dest中
    void serialize(char *dest) const override {
        LogRecord::serialize(dest);
    }

    // 从src中反序列化出一条Begin日志记录
    void deserialize(const char *src) override {
        LogRecord::deserialize(src);
    }

    virtual void format_print() override {
        std::cout << "log type in son_function: " << LogTypeStr[log_type_] << "\n";
        LogRecord::format_print();
    }
};

class InsertLogRecord : public LogRecord {
public:
    InsertLogRecord() {
        log_type_ = LogRecordType::INSERT;
        lsn_ = INVALID_LSN;
        log_tot_len_ = LOG_HEADER_SIZE;
        log_tid_ = INVALID_TXN_ID;
        prev_lsn_ = INVALID_LSN;
        table_name_ = nullptr;
    }

    InsertLogRecord(txn_id_t txn_id, int prev_lsn, RmRecord &insert_value, const Rid &rid, std::string table_name)
            : InsertLogRecord() {
        prev_lsn_ = prev_lsn;
        log_tid_ = txn_id;
        insert_value_ = insert_value;
        rid_ = rid;
        log_tot_len_ += sizeof(int);
        log_tot_len_ += insert_value_.size;
        log_tot_len_ += sizeof(Rid);
        table_name_size_ = table_name.length() + 1;
        table_name_ = new char[table_name_size_];
        memcpy(table_name_, table_name.c_str(), table_name_size_);
        log_tot_len_ += sizeof(size_t) + table_name_size_;
        undo_next = -1;
    }

    ~InsertLogRecord() {
        delete[] table_name_;
        table_name_ = nullptr;
    }

    // 把insert日志记录序列化到dest中
    void serialize(char *dest) const override {
        LogRecord::serialize(dest);
        int offset = OFFSET_LOG_DATA;
        memcpy(dest + offset, &insert_value_.size, sizeof(int));
        offset += sizeof(int);
        memcpy(dest + offset, insert_value_.data, insert_value_.size);
        offset += insert_value_.size;
        memcpy(dest + offset, &rid_, sizeof(Rid));
        offset += sizeof(Rid);
        memcpy(dest + offset, &table_name_size_, sizeof(size_t));
        offset += sizeof(size_t);
        memcpy(dest + offset, table_name_, table_name_size_);
        offset += table_name_size_;
        memmove(dest + offset, &undo_next, sizeof(undo_next));
    }

    // 从src中反序列化出一条Insert日志记录
    void deserialize(const char *src) override {
        LogRecord::deserialize(src);
        insert_value_.Deserialize(src + OFFSET_LOG_DATA);
        int offset = OFFSET_LOG_DATA + insert_value_.size + sizeof(int);
        rid_ = *reinterpret_cast<const Rid *>(src + offset);
        offset += sizeof(Rid);
        table_name_size_ = *reinterpret_cast<const size_t *>(src + offset);
        offset += sizeof(size_t);
        table_name_ = new char[table_name_size_];
        memcpy(table_name_, src + offset, table_name_size_);
        offset += table_name_size_;
        undo_next = *(lsn_t *) (src + offset);
    }

    void format_print() override {
        printf("insert record\n");
        LogRecord::format_print();
        printf("insert_value: %s\n", insert_value_.data);
        printf("insert rid: %d, %d\n", rid_.page_no, rid_.slot_no);
        printf("table name: %s\n", table_name_);
    }

    RmRecord insert_value_;     // 插入的记录
    Rid rid_;                   // 记录插入的位置
    char *table_name_;          // 插入记录的表名称
    size_t table_name_size_;    // 表名称的大小
    lsn_t undo_next;
};

/**
 * TODO: delete操作的日志记录
*/
class DeleteLogRecord : public LogRecord {
public:
    DeleteLogRecord() {
        log_type_ = LogRecordType::DELETE;
        lsn_ = INVALID_LSN;
        log_tot_len_ = LOG_HEADER_SIZE;
        log_tid_ = INVALID_TXN_ID;
        prev_lsn_ = INVALID_LSN;
        table_name_ = nullptr;
    }

    DeleteLogRecord(txn_id_t txn_id, int prev_lsn, RmRecord &delete_value, const Rid &rid, std::string table_name)
            : DeleteLogRecord() {
        prev_lsn_ = prev_lsn;
        log_tid_ = txn_id;
        delete_value_ = delete_value;
        rid_ = rid;
        log_tot_len_ += sizeof(int);
        log_tot_len_ += delete_value.size;
        log_tot_len_ += sizeof(Rid);
        table_name_size_ = table_name.length() + 1;
        table_name_ = new char[table_name_size_];
        memcpy(table_name_, table_name.c_str(), table_name_size_);
        log_tot_len_ += sizeof(size_t) + table_name_size_;
        undo_next = -1;
    }

    ~DeleteLogRecord() {
        delete[] table_name_;
    }

    // 把insert日志记录序列化到dest中
    void serialize(char *dest) const override {
        LogRecord::serialize(dest);
        int offset = OFFSET_LOG_DATA;
        memcpy(dest + offset, &delete_value_.size, sizeof(int));
        offset += sizeof(int);
        memcpy(dest + offset, delete_value_.data, delete_value_.size);
        offset += delete_value_.size;
        memcpy(dest + offset, &rid_, sizeof(Rid));
        offset += sizeof(Rid);
        memcpy(dest + offset, &table_name_size_, sizeof(size_t));
        offset += sizeof(size_t);
        memcpy(dest + offset, table_name_, table_name_size_);
        offset += (int) table_name_size_;
        memmove(dest + offset, &undo_next, sizeof(undo_next));
    }

    // 从src中反序列化出一条Insert日志记录
    void deserialize(const char *src) override {
        LogRecord::deserialize(src);
        delete_value_.Deserialize(src + OFFSET_LOG_DATA);
        int offset = OFFSET_LOG_DATA + delete_value_.size + sizeof(int);
        rid_ = *reinterpret_cast<const Rid *>(src + offset);
        offset += sizeof(Rid);
        table_name_size_ = *reinterpret_cast<const size_t *>(src + offset);
        offset += sizeof(size_t);
        table_name_ = new char[table_name_size_];
        memcpy(table_name_, src + offset, table_name_size_);
        offset += table_name_size_;
        undo_next = *(lsn_t *) (src + offset);
    }

    void format_print() override {
        printf("delete record\n");
        LogRecord::format_print();
        printf("delete_value: %s\n", delete_value_.data);
        printf("delete rid: %d, %d\n", rid_.page_no, rid_.slot_no);
        printf("table name: %s\n", table_name_);
    }

    RmRecord delete_value_;     // 插入的记录
    Rid rid_{};                   // 记录插入的位置
    char *table_name_;          // 插入记录的表名称
    size_t table_name_size_{};    // 表名称的大小
    lsn_t undo_next;
};

/**
 * TODO: update操作的日志记录
*/
class UpdateLogRecord : public LogRecord {
public:
    UpdateLogRecord() {
        log_type_ = LogRecordType::UPDATE;
        lsn_ = INVALID_LSN;
        log_tot_len_ = LOG_HEADER_SIZE;
        log_tid_ = INVALID_TXN_ID;
        prev_lsn_ = INVALID_LSN;
        table_name_ = nullptr;
    }

    UpdateLogRecord(txn_id_t txn_id, int prev_lsn, RmRecord &new_value, RmRecord &old_value, const Rid &rid,
                    std::string table_name)
            : UpdateLogRecord() {
        prev_lsn_ = prev_lsn;
        log_tid_ = txn_id;
        new_value_ = new_value;
        old_value_ = old_value;
        rid_ = rid;
        log_tot_len_ += 2 * sizeof(int);
        log_tot_len_ += old_value_.size;
        log_tot_len_ += new_value_.size;
        log_tot_len_ += sizeof(Rid);
        table_name_size_ = table_name.length() + 1;
        table_name_ = new char[table_name_size_];
        memcpy(table_name_, table_name.c_str(), table_name_size_);
        log_tot_len_ += sizeof(size_t) + table_name_size_;
        undo_next = -1;
    }

    ~UpdateLogRecord() {
        delete[] table_name_;
    }

    // 把insert日志记录序列化到dest中
    void serialize(char *dest) const override {
        LogRecord::serialize(dest);
        int offset = OFFSET_LOG_DATA;
        memcpy(dest + offset, &old_value_.size, sizeof(int));
        offset += sizeof(int);
        memcpy(dest + offset, old_value_.data, old_value_.size);
        offset += old_value_.size;
        memcpy(dest + offset, &new_value_.size, sizeof(int));
        offset += sizeof(int);
        memcpy(dest + offset, new_value_.data, new_value_.size);
        offset += new_value_.size;
        memcpy(dest + offset, &rid_, sizeof(Rid));
        offset += sizeof(Rid);
        memcpy(dest + offset, &table_name_size_, sizeof(size_t));
        offset += sizeof(size_t);
        memcpy(dest + offset, table_name_, table_name_size_);
        offset += table_name_size_;
        memmove(dest + offset, &undo_next, sizeof(undo_next));
    }

    // 从src中反序列化出一条Insert日志记录
    void deserialize(const char *src) override {
        LogRecord::deserialize(src);
        old_value_.Deserialize(src + OFFSET_LOG_DATA);
        int offset = OFFSET_LOG_DATA + old_value_.size + sizeof(int);
        new_value_.Deserialize(src + offset);
        offset = offset + new_value_.size + sizeof(int);
        rid_ = *reinterpret_cast<const Rid *>(src + offset);
        offset += sizeof(Rid);
        table_name_size_ = *reinterpret_cast<const size_t *>(src + offset);
        offset += sizeof(size_t);
        table_name_ = new char[table_name_size_];
        memcpy(table_name_, src + offset, table_name_size_);
        offset += table_name_size_;
        undo_next = *(lsn_t *) (src + offset);
    }

    void format_print() override {
        printf("update record\n");
        LogRecord::format_print();
        printf("update_old_value: %s\n", old_value_.data);
        printf("update_new_value: %s\n", new_value_.data);
        printf("update rid: %d, %d\n", rid_.page_no, rid_.slot_no);
        printf("table name: %s\n", table_name_);
    }

    RmRecord old_value_;        // 修改前的记录
    RmRecord new_value_;        // 修改后的记录
    Rid rid_{};                   // 修改记录的位置
    char *table_name_;          // 插入记录的表名称
    size_t table_name_size_{};    // 表名称的大小
    lsn_t undo_next;
};

class PageLogRecord : public LogRecord {
public:
    PageLogRecord() {
        log_type_ = LogRecordType::PAGE_SET;
        lsn_ = INVALID_LSN;
        log_tot_len_ = LOG_HEADER_SIZE;
        log_tid_ = INVALID_TXN_ID;
        prev_lsn_ = INVALID_LSN;
        old_page = nullptr;
        new_page = nullptr;
    }

    PageLogRecord(txn_id_t txn_id, int prev_lsn, const std::string &tab_name_, size_t page_no_, char *old_data_)
            : PageLogRecord() {
        log_tid_ = txn_id;
        prev_lsn_ = prev_lsn;

        tab_name = tab_name_;
        log_tot_len_ += sizeof(size_t);
        log_tot_len_ += tab_name.size();

        page_no = page_no_;
        log_tot_len_ += sizeof(page_no);
        log_tot_len_ += 2 * PAGE_SIZE;

        old_page = new char[PAGE_SIZE];
        memmove(old_page, old_data_, PAGE_SIZE);
    }

    ~PageLogRecord() {
        delete[] old_page;
        old_page = nullptr;
        delete[] new_page;
        new_page = nullptr;
    }

    // 序列化Page日志记录到dest中
    void serialize(char *dest) const override {
        LogRecord::serialize(dest);
        size_t offset = LOG_HEADER_SIZE;
        auto tab_name_size = tab_name.size();

        memmove(dest + offset, &tab_name_size, sizeof(tab_name_size));
        offset += sizeof(tab_name_size);

        memmove(dest + offset, tab_name.c_str(), tab_name_size);
        offset += tab_name_size;

        memmove(dest + offset, &page_no, sizeof(size_t));
        offset += sizeof(size_t);

        memmove(dest + offset, old_page, PAGE_SIZE);
        offset += PAGE_SIZE;
        memmove(dest + offset, new_page, PAGE_SIZE);
        offset += PAGE_SIZE;
    }

    // 从src中反序列化出一条Page日志记录
    void deserialize(const char *src) override {
        LogRecord::deserialize(src);

        size_t offset = LOG_HEADER_SIZE;
        size_t tab_name_size = *(size_t *) (src + offset);
        offset += sizeof(tab_name_size);

        char *tab_name_temp = new char[tab_name_size + 1];
        memmove(tab_name_temp, src + offset, tab_name_size);
        tab_name_temp[tab_name_size] = '\0';
        tab_name = tab_name_temp;
//        delete[] tab_name_temp;
        offset += tab_name_size;

        page_no = *(size_t *) (src + offset);
        offset += sizeof(page_no);

        old_page = new char[PAGE_SIZE];
        memmove(old_page, src + offset, PAGE_SIZE);
        offset += PAGE_SIZE;
        new_page = new char[PAGE_SIZE];
        memmove(new_page, src + offset, PAGE_SIZE);
        offset += PAGE_SIZE;
    }

    void format_print() override {
        std::cout << "log type in son_function: " << LogTypeStr[log_type_] << "\n";
        LogRecord::format_print();
    }

    void set_new_page(char *src) {
        new_page = new char[PAGE_SIZE];
        memmove(new_page, src, PAGE_SIZE);
    }

    std::string tab_name;

    size_t page_no;

    char *old_page;
    char *new_page;
};

class UndoNextLogRecord : public LogRecord {
public:
    UndoNextLogRecord() {
        log_type_ = LogRecordType::UNDO_NEXT;
        lsn_ = INVALID_LSN;
        log_tot_len_ = LOG_HEADER_SIZE;
        log_tid_ = INVALID_TXN_ID;
        prev_lsn_ = INVALID_LSN;
    }

    UndoNextLogRecord(txn_id_t txn_id, int prev_lsn, lsn_t undo_next_) : UndoNextLogRecord() {
        log_tid_ = txn_id;
        prev_lsn_ = prev_lsn;
        undo_next = undo_next_;
        log_tot_len_ += sizeof(undo_next);
    }

    // 序列化Begin日志记录到dest中
    void serialize(char *dest) const override {
        LogRecord::serialize(dest);
        memmove(dest + LOG_HEADER_SIZE, &undo_next, sizeof(undo_next));
    }

    // 从src中反序列化出一条Begin日志记录
    void deserialize(const char *src) override {
        LogRecord::deserialize(src);
        undo_next = *(lsn_t *) (src + LOG_HEADER_SIZE);
    }

    virtual void format_print() override {
        std::cout << "log type in son_function: " << LogTypeStr[log_type_] << "\n";
        LogRecord::format_print();
    }

    lsn_t undo_next;
};

class IndexPagesLogRecord : public LogRecord {
public:
    IndexPagesLogRecord() {
        log_type_ = LogRecordType::INDEX_PAGE;
        lsn_ = INVALID_LSN;
        log_tot_len_ = LOG_HEADER_SIZE;
        log_tid_ = INVALID_TXN_ID;
        prev_lsn_ = INVALID_LSN;
        file_hdr = nullptr;
        hdr_len = 0;
    }

    IndexPagesLogRecord(txn_id_t txn_id, int prev_lsn, std::string idx_name_) : IndexPagesLogRecord() {
        log_tid_ = txn_id;
        prev_lsn_ = prev_lsn;
        idx_name = std::move(idx_name_);
        log_tot_len_ += sizeof(size_t) + idx_name.size() + sizeof(size_t);
    }

    IndexPagesLogRecord(const IndexPagesLogRecord &other) : LogRecord(other) {
        log_tot_len_ = other.log_tot_len_;
        log_tid_ = other.log_tid_;
        lsn_ = other.lsn_;
        prev_lsn_ = other.prev_lsn_;
        log_type_ = other.log_type_;

        idx_name = other.idx_name;
        page_ids = other.page_ids;

        for (auto page: other.pages) {
            auto p = new char[PAGE_SIZE];
            memcpy(p, page, PAGE_SIZE);
            pages.push_back(p);
        }

        hdr_len = other.hdr_len;
        file_hdr = new char[hdr_len];
        memcpy(file_hdr, other.file_hdr, hdr_len);
    }

    ~IndexPagesLogRecord() {
        for (auto page: pages) delete[] page;
        pages.clear();
        delete[] file_hdr;
        file_hdr = nullptr;
    }

    // 序列化Begin日志记录到dest中
    void serialize(char *dest) const override {
        LogRecord::serialize(dest);

        size_t offset = LOG_HEADER_SIZE;
        auto idx_name_size = idx_name.size();
        memmove(dest + offset, &idx_name_size, sizeof(idx_name_size));
        offset += sizeof(idx_name_size);

        memmove(dest + offset, idx_name.c_str(), idx_name_size);
        offset += idx_name_size;

        size_t n = pages.size();
        memmove(dest + offset, &n, sizeof(n));
        offset += sizeof(n);

        for (auto page_id: page_ids) {
            memcpy(dest, &page_id, sizeof(page_id));
            offset += sizeof(page_id);
        }

        for (auto page: pages) {
            memmove(dest + offset, page, PAGE_SIZE);
            offset += PAGE_SIZE;
        }

        memmove(dest, &hdr_len, sizeof(hdr_len));
        offset += sizeof(hdr_len);

        if (file_hdr == nullptr) throw InternalError("no file hdr in index pages log");
        memmove(dest + offset, file_hdr, hdr_len);
        offset += PAGE_SIZE;
    }

    // 从src中反序列化出一条Begin日志记录
    void deserialize(const char *src) override {
        LogRecord::deserialize(src);

        size_t offset = LOG_HEADER_SIZE;
        size_t idx_name_size = *(size_t *) (src + offset);
        offset += sizeof(idx_name_size);

        auto idx_name_temp = new char[idx_name_size + 1]();
        memmove(idx_name_temp, src + offset, idx_name_size);
        idx_name = idx_name_temp;
        offset += idx_name_size;

        size_t n = *(size_t *) (src + offset);
        offset += sizeof(n);

        for (int i = 0; i < n; i++) {
            PageId page_id = *(PageId *) (src + offset);
            page_ids.push_back(page_id);
            offset += sizeof(page_id);
        }

        for (int i = 0; i < n; i++) {
            auto p = new char[PAGE_SIZE];
            memmove(p, src + offset, PAGE_SIZE);
            pages.push_back(p);
            offset += PAGE_SIZE;
        }

        hdr_len = *(int *) (src + offset);
        offset += sizeof(int);

        file_hdr = new char[hdr_len];
        memmove(file_hdr, src + offset, PAGE_SIZE);
    }

    void format_print() override {
        std::cout << "log type in son_function: " << LogTypeStr[log_type_] << "\n";
        LogRecord::format_print();
    }

    void add_page(Page *page) {
        log_tot_len_ += PAGE_SIZE + sizeof(PageId);
        auto p = new char[PAGE_SIZE];
        memmove(p, page->get_data(), PAGE_SIZE);
        pages.push_back(p);
        page_ids.push_back(page->get_page_id());
    }

    void add_file_hdr(char *hdr, int len) {
        hdr_len = len;
        file_hdr = new char[len];
        memmove(file_hdr, hdr, hdr_len);

        log_tot_len_ += sizeof(hdr_len) + hdr_len;
    }

    std::string idx_name;
    std::vector<PageId> page_ids;
    std::vector<char *> pages;
    int hdr_len;
    char *file_hdr;
};

class CreateIndexLogRecord : public LogRecord {
public:
    CreateIndexLogRecord() {
        log_type_ = LogRecordType::CREATE_INDEX;
        lsn_ = INVALID_LSN;
        log_tot_len_ = LOG_HEADER_SIZE;
        log_tid_ = INVALID_TXN_ID;
        prev_lsn_ = INVALID_LSN;

    }

    CreateIndexLogRecord(txn_id_t txn_id, int prev_lsn, std::string tab_name_, const std::vector<std::string> &col_names_)
    : CreateIndexLogRecord() {
        log_tid_ = txn_id;
        prev_lsn_ = prev_lsn;
        tab_name = std::move(tab_name_);
        col_names = col_names_;

        log_tot_len_ += (col_names.size() + 2) * sizeof(size_t);
        log_tot_len_ += col_names.size();
        for (auto& col : col_names) log_tot_len_ += col.size();
    }

    CreateIndexLogRecord(const CreateIndexLogRecord &other) : LogRecord(other) {
        tab_name = other.tab_name;
        col_names = other.col_names;
    }

    // 序列化Begin日志记录到dest中
    void serialize(char *dest) const override {
        LogRecord::serialize(dest);

        size_t offset = LOG_HEADER_SIZE;

        size_t idx_name_size = tab_name.size();
        memmove(dest + offset, &idx_name_size, sizeof(idx_name_size));
        offset += sizeof(idx_name_size);

        memmove(dest + offset, tab_name.c_str(), idx_name_size);
        offset += idx_name_size;

        size_t n = col_names.size();
        memmove(dest + offset, &n, sizeof (n));
        offset += sizeof (n);

        for (auto& col : col_names) {
            size_t len = col.size();
            memmove(dest + offset, &len, sizeof (len));
            offset += sizeof (len);

            memmove(dest + offset, col.c_str(), col.size());
            offset += col.size();
        }
    }

    // 从src中反序列化出一条Begin日志记录
    void deserialize(const char *src) override {
        LogRecord::deserialize(src);

        size_t offset = LOG_HEADER_SIZE;
        size_t idx_name_size = *(size_t *) (src + offset);
        offset += sizeof(idx_name_size);

        auto idx_name_temp = new char[idx_name_size + 1]();
        memmove(idx_name_temp, src + offset, idx_name_size);
        tab_name = idx_name_temp;
        offset += idx_name_size;

        size_t n = *(size_t*)(src + offset);
        offset += sizeof(n);

        for (int i = 0; i < n; i++) {
            size_t len = *(size_t*)(src + offset);
            offset += sizeof(len);

            auto col_ = new char[len + 1];
            memmove(col_, src + offset, len);
            col_[len] = '\0';
            std::string col_str;
            col_str.assign(col_);
            col_names.push_back(col_str);
            delete[] col_;
            offset += len;
        }
    }

    void format_print() override {
        std::cout << "log type in son_function: " << LogTypeStr[log_type_] << "\n";
        LogRecord::format_print();
    }

    std::string tab_name;
    std::vector<std::string> col_names;
};

class DropIndexLogRecord : public LogRecord {
public:
    DropIndexLogRecord() {
        log_type_ = LogRecordType::DROP_INDEX;
        lsn_ = INVALID_LSN;
        log_tot_len_ = LOG_HEADER_SIZE;
        log_tid_ = INVALID_TXN_ID;
        prev_lsn_ = INVALID_LSN;

    }

    DropIndexLogRecord(txn_id_t txn_id, int prev_lsn, std::string tab_name_, const std::vector<std::string> &col_names_)
            : DropIndexLogRecord() {
        log_tid_ = txn_id;
        prev_lsn_ = prev_lsn;
        tab_name = std::move(tab_name_);
        col_names = col_names_;

        log_tot_len_ += (col_names.size() + 2) * sizeof(size_t);
        log_tot_len_ += col_names.size();
        for (auto& col : col_names) log_tot_len_ += col.size();
    }

    DropIndexLogRecord(const DropIndexLogRecord &other) : LogRecord(other) {
        tab_name = other.tab_name;
        col_names = other.col_names;
    }

    // 序列化Begin日志记录到dest中
    void serialize(char *dest) const override {
        LogRecord::serialize(dest);

        size_t offset = LOG_HEADER_SIZE;

        auto idx_name_size = tab_name.size();
        memmove(dest + offset, &idx_name_size, sizeof(idx_name_size));
        offset += sizeof(idx_name_size);

        memmove(dest + offset, tab_name.c_str(), idx_name_size);
        offset += idx_name_size;

        size_t n = col_names.size();
        memmove(dest + offset, &n, sizeof (n));
        offset += sizeof (n);

        for (auto& col : col_names) {
            size_t len = col.size();
            memmove(dest + offset, &len, sizeof (len));
            offset += sizeof (len);

            memmove(dest + offset, col.c_str(), col.size());
            offset += col.size();
        }
    }

    // 从src中反序列化出一条Begin日志记录
    void deserialize(const char *src) override {
        LogRecord::deserialize(src);

        size_t offset = LOG_HEADER_SIZE;
        size_t idx_name_size = *(size_t *) (src + offset);
        offset += sizeof(idx_name_size);

        auto idx_name_temp = new char[idx_name_size + 1]();
        memmove(idx_name_temp, src + offset, idx_name_size);
        tab_name = idx_name_temp;
        offset += idx_name_size;

        size_t n = *(size_t*)(src + offset);
        offset += sizeof(n);

        for (int i = 0; i < n; i++) {
            size_t len = *(size_t*)(src + offset);
            offset += sizeof(len);

            char col_[len + 1];
            memmove(col_, src + offset, len);
            col_[len] = '\0';
            std::string col_str = col_;
            col_names.push_back(col_str);
            offset += len;
        }
    }

    void format_print() override {
        std::cout << "log type in son_function: " << LogTypeStr[log_type_] << "\n";
        LogRecord::format_print();
    }

    std::string tab_name;
    std::vector<std::string> col_names;
};

/* 日志缓冲区，只有一个buffer，因此需要阻塞地去把日志写入缓冲区中 */
class LogBuffer {
public:
    LogBuffer() {
        offset_ = 0;
        memset(buffer_, 0, sizeof(buffer_));
    }

    bool is_full(int append_size) {
        if (offset_ + append_size > LOG_BUFFER_SIZE)
            return true;
        return false;
    }

    char buffer_[LOG_BUFFER_SIZE + 1];
    int offset_;    // 写入log的offset
};

/* 日志管理器，负责把日志写入日志缓冲区，以及把日志缓冲区中的内容写入磁盘中 */
class LogManager {
public:
    LogManager(DiskManager *disk_manager) { disk_manager_ = disk_manager; }

    lsn_t add_log_to_buffer(LogRecord *log_record);

    void flush_log_to_disk();

    LogBuffer *get_log_buffer() { return &log_buffer_; }

// private:
    std::atomic<lsn_t> global_lsn_{0};  // 全局lsn，递增，用于为每条记录分发lsn
    std::mutex latch_;                  // 用于对log_buffer_的互斥访问
    LogBuffer log_buffer_;              // 日志缓冲区
    lsn_t persist_lsn_;                 // 记录已经持久化到磁盘中的最后一条日志的日志号
    DiskManager *disk_manager_;
};
