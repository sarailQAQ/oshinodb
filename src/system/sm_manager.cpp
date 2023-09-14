/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "sm_manager.h"

#include <sys/stat.h>
#include <unistd.h>

#include <fstream>

#include "index/ix.h"
#include "record/rm.h"
#include "record_printer.h"

/**
 * @description: 判断是否为一个文件夹
 * @return {bool} 返回是否为一个文件夹
 * @param {string&} db_name 数据库文件名称，与文件夹同名
 */
bool SmManager::is_dir(const std::string &db_name) {
    struct stat st{};
    return stat(db_name.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

/**
 * @description: 创建数据库，所有的数据库相关文件都放在数据库同名文件夹下
 * @param {string&} db_name 数据库名称
 */
void SmManager::create_db(const std::string &db_name) {
    if (is_dir(db_name)) {
        throw DatabaseExistsError(db_name);
    }
    //为数据库创建一个子目录
    std::string cmd = "mkdir " + db_name;
    if (system(cmd.c_str()) < 0) {  // 创建一个名为db_name的目录
        throw UnixError();
    }
    if (chdir(db_name.c_str()) < 0) {  // 进入名为db_name的目录
        throw UnixError();
    }
    //创建系统目录
    auto *new_db = new DbMeta();
    new_db->name_ = db_name;

    // 注意，此处ofstream会在当前目录创建(如果没有此文件先创建)和打开一个名为DB_META_NAME的文件
    std::ofstream ofs(DB_META_NAME);

    // 将new_db中的信息，按照定义好的operator<<操作符，写入到ofs打开的DB_META_NAME文件中
    ofs << *new_db;  // 注意：此处重载了操作符<<

    delete new_db;

    // 创建日志文件
    disk_manager_->create_file(LOG_FILE_NAME);

    // 回到根目录
    if (chdir("..") < 0) {
        throw UnixError();
    }
}

/**
 * @description: 删除数据库，同时需要清空相关文件以及数据库同名文件夹
 * @param {string&} db_name 数据库名称，与文件夹同名
 */
void SmManager::drop_db(const std::string &db_name) {
    if (!is_dir(db_name)) {
        throw DatabaseNotFoundError(db_name);
    }
    std::string cmd = "rm -r " + db_name;
    if (system(cmd.c_str()) < 0) {
        throw UnixError();
    }
}

/**
 * @description: 打开数据库，找到数据库对应的文件夹，并加载数据库元数据和相关文件
 * @param {string&} db_name 数据库名称，与文件夹同名
 */
void SmManager::open_db(const std::string &db_name) {
    if (!is_dir(db_name)) {
        throw DatabaseNotFoundError(db_name);
    }

    if (chdir(db_name.c_str()) < 0) {  // 进入名为db_name的目录
        throw UnixError();
    }
    std::ifstream ifs(DB_META_NAME);
    ifs >> db_;
    // 加载表元数据
    for (auto &entry: db_.tabs_) {
        auto &tab = entry.second;
        fhs_.emplace(tab.name, rm_manager_->open_file(tab.name));
        for (const auto &index: tab.indexes) {
            auto idx_name = ix_manager_->get_index_name(tab.name, index.cols);
            assert(ihs_.count(idx_name) == 0);
            ihs_.emplace(idx_name, ix_manager_->open_index(tab.name, index.cols));
        }
    }
}

/**
 * @description: 把数据库相关的元数据刷入磁盘中
 */
void SmManager::flush_meta() {
    // 默认清空文件
    std::ofstream ofs(DB_META_NAME);
    ofs << db_;
}

/**
 * @description: 关闭数据库并把数据落盘
 */
void SmManager::close_db() {
    flush_meta();
    db_.name_.clear();
    db_.tabs_.clear();
    for (auto &entry: fhs_) {
        rm_manager_->close_file(entry.second.get());
    }
    fhs_.clear();

    for (auto &entry: ihs_) {
        ix_manager_->close_index(entry.second.get());
    }
    ihs_.clear();
    // 返回上级目录
    if (chdir("..") < 0) {
        throw UnixError();
    }
}

/**
 * @description: 显示所有的表,通过测试需要将其结果写入到output.txt,详情看题目文档
 * @param {Context*} context 
 */
void SmManager::show_tables(Context *context) {
    std::fstream outfile;
    outfile.open("output.txt", std::ios::out | std::ios::app);
    outfile << "| Tables |\n";
    RecordPrinter printer(1);
    printer.print_separator(context);
    printer.print_record({"Tables"}, context);
    printer.print_separator(context);
    for (auto &entry: db_.tabs_) {
        auto &tab = entry.second;
        printer.print_record({tab.name}, context);
        outfile << "| " << tab.name << " |\n";
    }
    printer.print_separator(context);
    outfile.close();
}

/**
 * @description: 显示表的元数据
 * @param {string&} tab_name 表名称
 * @param {Context*} context 
 */
void SmManager::desc_table(const std::string &tab_name, Context *context) {
    TabMeta tab = db_.get_table(tab_name);

    std::vector<std::string> captions = {"Field", "Type", "Index"};
    RecordPrinter printer(captions.size());
    // Print header
    printer.print_separator(context);
    printer.print_record(captions, context);
    printer.print_separator(context);
    // Print fields
    for (auto &col: tab.cols) {
        std::vector<std::string> field_info = {col.name, coltype2str(col.type), col.index ? "YES" : "NO"};
        printer.print_record(field_info, context);
    }
    // Print footer
    printer.print_separator(context);
}

/**
 * @description: 创建表
 * @param {string&} tab_name 表的名称
 * @param {vector<ColDef>&} col_defs 表的字段
 * @param {Context*} context 
 */
void SmManager::create_table(const std::string &tab_name, const std::vector<ColDef> &col_defs, Context *context) {
    if (db_.is_table(tab_name)) {
        throw TableExistsError(tab_name);
    }
//    if (!context->lock_mgr_->lock_exclusive_on_table(context->txn_, fhs_.at(tab_name)->GetFd()))
//        throw TransactionAbortException(context->txn_->get_transaction_id(), AbortReason::DEADLOCK_PREVENTION);

    // Create table meta
    int curr_offset = 0;
    TabMeta tab;
    tab.name = tab_name;
    for (auto &col_def: col_defs) {
        ColMeta col = {.tab_name = tab_name,
                .name = col_def.name,
                .type = col_def.type,
                .len = col_def.len,
                .offset = curr_offset,
                .index = false};
        curr_offset += col_def.len;
        tab.cols.push_back(col);
    }
    // Create & open record file
    int record_size = curr_offset;  // record_size就是col meta所占的大小（表的元数据也是以记录的形式进行存储的）
    rm_manager_->create_file(tab_name, record_size);
    db_.tabs_[tab_name] = tab;
    fhs_[tab_name] = rm_manager_->open_file(tab_name);
    fhs_.emplace(tab_name, rm_manager_->open_file(tab_name));

    flush_meta();
}

/**
 * @description: 删除表
 * @param {string&} tab_name 表的名称
 * @param {Context*} context
 */
void SmManager::drop_table(const std::string &tab_name, Context *context) {
    // 表不存在
    if (!db_.is_table(tab_name)) {
        throw TableNotFoundError(tab_name);
    }
    if (!context->lock_mgr_->lock_exclusive_on_table(context->txn_, fhs_.at(tab_name)->GetFd()))
        throw TransactionAbortException(context->txn_->get_transaction_id(), AbortReason::DEADLOCK_PREVENTION);


    TabMeta tab = db_.get_table(tab_name);

    // 删除记录文件
    auto entry = fhs_.find(tab_name);
    rm_manager_->close_file(entry->second.get());
    rm_manager_->destroy_file(tab_name);

    for (auto &index: tab.indexes) {
        ix_manager_->close_index(ihs_.at(ix_manager_->get_index_name(tab.name, index.cols)).get());
        ix_manager_->destroy_index(tab_name, index.cols);

        ihs_.erase(ix_manager_->get_index_name(tab_name, index.cols));
    }
    db_.SetTabMeta(tab_name, tab);
    // 从记录中删除该表信息
    db_.tabs_.erase(tab_name);
    fhs_.erase(tab_name);
}

void SmManager::show_index(const std::string &tab_name, Context *context) {
    std::fstream outfile;
    outfile.open("output.txt", std::ios::out | std::ios::app);
    RecordPrinter printer(3);
    printer.print_separator(context);
    printer.print_record({"Table", "Type", "Index"}, context);
    printer.print_separator(context);
    auto tab = db_.get_table(tab_name);
    for (auto &index: tab.indexes) {
        std::string idx_str = "(";
        bool is_first = true;
        for (const auto &col: index.cols) {
            if (!is_first) idx_str += ",";
            is_first = false;
            idx_str += col.name;
        }
        idx_str += ")";
        outfile << "| " << tab_name << " | unique | " << idx_str << " |\n";
        printer.print_record({tab_name, "unique", idx_str}, context);
    }
    printer.print_separator(context);
    outfile.close();
}

/**
 * @description: 创建索引
 * @param {string&} tab_name 表的名称
 * @param {vector<string>&} col_names 索引包含的字段名称
 * @param {Context*} context
 */
void SmManager::create_index(const std::string &tab_name, const std::vector<std::string> &col_names, Context *context) {
    // done
    auto tab = db_.get_table(tab_name);

    // 加锁
    if (context != nullptr && !context->lock_mgr_->lock_exclusive_on_table(context->txn_, fhs_.at(tab_name)->GetFd()))
        throw TransactionAbortException(context->txn_->get_transaction_id(), AbortReason::DEADLOCK_PREVENTION);

    if (tab.is_index(col_names)) throw IndexExistsError(tab_name, col_names);

    // Create index file
    int len = 0;
    std::vector<ColMeta> cols;
    for (auto &col_name: col_names) {
        auto it = tab.get_col(col_name);
        len += it->len;
        auto x = *it;
        cols.push_back(*it);
    }
    ix_manager_->create_index(tab_name, cols);  // 这里调用了
    // Open index file
    auto ih = ix_manager_->open_index(tab_name, cols);
    // 将所有已经存在的数据写入索引文件
    auto file_handle = fhs_.at(tab_name).get();
    char *key = new char[len];
    for (RmScan rm_scan(file_handle); !rm_scan.is_end(); rm_scan.next()) {
        auto rec = file_handle->get_record(rm_scan.rid(), context);  // rid是record的存储位置，作为value插入到索引里
        int key_offset = 0;
        memset(key, 0, len);
        for (auto &col: cols) {
            memmove(key + key_offset, rec->data + col.offset, col.len);
            key_offset += col.len;
        }
        // record data里以各个属性的offset进行分隔，属性的长度为col len，record里面每个属性的数据作为key插入索引里
        ih->insert_entry(key, rm_scan.rid(), context);
    }
    ih->flush();
    delete[] key;
    IndexMeta idx_meta;
    idx_meta.cols = cols;
    idx_meta.col_tot_len = len;
    idx_meta.col_num = (int) col_names.size();
    idx_meta.tab_name = tab_name;
    tab.indexes.push_back(idx_meta);
    // 单索引，更新值
    if (col_names.size() == 1) tab.get_col(col_names[0])->index = true;
    db_.SetTabMeta(tab_name, tab);

    flush_meta();

    // Store index handle
    auto index_name = ix_manager_->get_index_name(tab_name, cols);
    assert(ihs_.count(index_name) == 0);
    ihs_.emplace(index_name, std::move(ih));

    // 写入事务
    if (context != nullptr) {
        CreateIndexLogRecord rec(context->txn_->get_transaction_id(), context->txn_->get_prev_lsn(),
                                 tab_name, col_names);
        auto lsn = context->log_mgr_->add_log_to_buffer(&rec);
        context->txn_->set_prev_lsn(lsn);
        context->txn_->append_write_record(new WriteRecord(WType::CREATE_INDEX, tab_name, col_names, lsn));
    }

}

/**
 * @description: 删除索引
 * @param {string&} tab_name 表名称
 * @param {vector<string>&} col_names 索引包含的字段名称
 * @param {Context*} context
 */
void SmManager::drop_index(const std::string &tab_name, const std::vector<std::string> &col_names, Context *context) {
    // done
    auto &tab = db_.tabs_[tab_name];
    std::vector<ColMeta> cols;
    for (auto &col_name: col_names) {
        auto it = tab.get_col(col_name);
        cols.push_back(*it);
    }

    drop_index(tab_name, cols, context);
}

/**
 * @description: 删除索引
 * @param {string&} tab_name 表名称
 * @param {vector<ColMeta>&} 索引包含的字段元数据
 * @param {Context*} context
 */
void SmManager::drop_index(const std::string &tab_name, const std::vector<ColMeta> &cols, Context *context) {
    // done
    if (context != nullptr && !context->lock_mgr_->lock_exclusive_on_table(context->txn_, fhs_.at(tab_name)->GetFd()))
        throw TransactionAbortException(context->txn_->get_transaction_id(), AbortReason::DEADLOCK_PREVENTION);

    auto &tab = db_.tabs_[tab_name];
    std::vector<std::string> col_names;
    col_names.reserve(cols.size());
    for (const auto &col: cols) col_names.push_back(col.name);

    if (!tab.is_index(col_names)) throw IndexNotFoundError(tab_name, col_names);

    auto index_name = ix_manager_->get_index_name(tab_name, cols);
    auto ih = ihs_.at(index_name).get();
    ix_manager_->close_index(ih);
    ix_manager_->destroy_index(tab_name, cols);
    ihs_.erase(index_name);
    auto index = tab.get_index_meta(col_names);
    tab.indexes.erase(index);
    if (cols.size() == 1) tab.get_col(cols[0].name)->index = false;
    db_.SetTabMeta(tab_name, tab);
    flush_meta();
    // 写入事务
    if (context != nullptr) {
        DropIndexLogRecord rec(context->txn_->get_transaction_id(), context->txn_->get_prev_lsn(),
                                 tab_name, col_names);
        auto lsn = context->log_mgr_->add_log_to_buffer(&rec);
        context->txn_->set_prev_lsn(lsn);
        context->txn_->append_write_record(new WriteRecord(WType::DROP_INDEX, tab_name, col_names, lsn));
    }
}

//insert -> delete
void SmManager::rollback_insert(const std::string &tab_name, const Rid &rid, Context *context) {
    auto tab = db_.get_table(tab_name);
    auto rec = fhs_.at(tab_name).get()->get_record(rid, context);
    // delete entry
    for (auto &index: tab.indexes) {
        auto key = new char[index.col_tot_len];
        int key_offset = 0;
        memset(key, 0, index.col_tot_len);
        for (auto &col: index.cols) {
            memmove(key + key_offset, rec->data + col.offset, col.len);
            key_offset += col.len;
        }
        auto &ih = ihs_.at(ix_manager_->get_index_name(tab_name, index.cols));
        ih->delete_entry(key, context);
//        ih->flush();
        delete[] key;
    }
    if (context != nullptr) context->txn_->add_idx_log(context->log_mgr_);
    // delete record
    fhs_.at(tab_name).get()->delete_record(rid, context);
}

//delete -> insert
void SmManager::rollback_delete(const std::string &tab_name, Rid &rid, const RmRecord &record, Context *context) {
    auto tab = db_.get_table(tab_name);
    // insert record
    auto new_rid = fhs_.at(tab_name)->insert_record(record.data, context);
    // insert entry
    for (auto &index: tab.indexes) {
        auto key = new char[index.col_tot_len];
        int key_offset = 0;
        memset(key, 0, index.col_tot_len);
        for (auto &col: index.cols) {
            memmove(key + key_offset, record.data + col.offset, col.len);
            key_offset += col.len;
        }
        ihs_.at(ix_manager_->get_index_name(tab_name, index.cols))->insert_entry(key, new_rid, context);
//        ihs_.at(ix_manager_->get_index_name(tab_name, index.cols))->flush();
        delete[] key;
    }
    if (context != nullptr) context->txn_->add_idx_log(context->log_mgr_);
}

//update -> update back (delete then insert)
void SmManager::rollback_update(const std::string &tab_name, const Rid &rid, const RmRecord &record, Context *context) {
    auto tab = db_.get_table(tab_name);
    auto rec = fhs_.at(tab_name).get()->get_record(rid, context);
    // delete entry
    for (auto &index: tab.indexes) {
        auto old_key = new char[index.col_tot_len];
        int old_key_offset = 0;
        auto new_key = new char[index.col_tot_len];
        int new_key_offset = 0;
        memset(new_key, 0, index.col_tot_len);
        memset(old_key, 0, index.col_tot_len);

        for (auto &col: index.cols) {
            memmove(old_key + old_key_offset, rec->data + col.offset, col.len);
            old_key_offset += col.len;

            memmove(new_key + new_key_offset, record.data + col.offset, col.len);
            new_key_offset += col.len;
        }
        if (std::strcmp(old_key, new_key) == 0) continue;
        ihs_.at(ix_manager_->get_index_name(tab_name, index.cols))->delete_entry(old_key, context);
        ihs_.at(ix_manager_->get_index_name(tab_name, index.cols))->insert_entry(new_key, rid, context);
        delete[] old_key;
        delete[] new_key;
    }

    if (context != nullptr) context->txn_->add_idx_log(context->log_mgr_);
    // update record
    fhs_.at(tab_name).get()->update_record(rid, record.data, context);

}

void SmManager::rollback_create_index(const std::string &tab_name, const std::vector<std::string> &col_names,
                                      Context *context) {
    drop_index(tab_name, col_names, context);
}

void SmManager::rollback_drop_index(const std::string &tab_name, const std::vector<std::string> &col_names,
                                    Context *context) {
    create_index(tab_name, col_names, context);
}