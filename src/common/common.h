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

#include <cassert>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include "defs.h"
#include "record/rm_defs.h"


struct TabCol {
    std::string tab_name;
    std::string col_name;
    Func func;
    std::string alias;
    bool is_desc;
    friend bool operator<(const TabCol &x, const TabCol &y) {
        return std::make_pair(x.tab_name, x.col_name) < std::make_pair(y.tab_name, y.col_name);
    }

};

struct Value {
    ColType type;  // type of value
    union {
        int int_val;      // int value
        float float_val;  // float value
        long long bigint_val;
    };
    std::string str_val;  // string value

    std::shared_ptr<RmRecord> raw;  // raw record buffer

    char* buff;

    void set_int(int int_val_) {
        type = TYPE_INT;
        int_val = int_val_;
    }

    void set_float(float float_val_) {
        type = TYPE_FLOAT;
        float_val = float_val_;
    }

    void set_str(std::string str_val_) {
        type = TYPE_STRING;
        str_val = std::move(str_val_);
    }

    void set_datetime(std::string datetime_val_) {
        type = TYPE_DATETIME;
        str_val = datetime_val_;
    }

    void set_bigint(long long bigint_val_) {
        type = TYPE_BIGINT;
        bigint_val = bigint_val_;
    }

    void init_raw(int len) {
        assert(raw == nullptr);
        raw = std::make_shared<RmRecord>(len);
        if (type == TYPE_INT) {
            assert(len == sizeof(int));
            *(int *)(raw->data) = int_val;
        } else if (type == TYPE_FLOAT) {
            assert(len == sizeof(float));
            *(float *)(raw->data) = float_val;
        } else if (type == TYPE_STRING || type == TYPE_DATETIME) {
            if (len < (int)str_val.size()) {
                throw StringOverflowError();
            }
            memset(raw->data, 0, len);
            memcpy(raw->data, str_val.c_str(), str_val.size());
        } else if (type == TYPE_BIGINT) {
            assert(len == sizeof(long long));
            *(long long *)(raw->data) = bigint_val;
        }
    }

    char* to_bytes() {
        if (type == TYPE_INT) {
            buff = new char[sizeof(int)];
            *(int *)(buff) = int_val;
        } else if (type == TYPE_FLOAT) {
            buff = new char[sizeof(float )];
            *(float *)(buff) = float_val;
        } else if (type == TYPE_STRING || type == TYPE_DATETIME) {
            buff = new char[str_val.size()];
            memcpy(buff, str_val.c_str(), str_val.size());
        } else if (type == TYPE_BIGINT) {
            buff = new char[sizeof(long long)];
            *(long long *)(buff) = bigint_val;
        } else throw InternalError("Value::to_bytes: unknown type error");
        return buff;
    }

    bool is_valid_datetime() const {
        std::string dateTimeStr(str_val);

        // 检查字符串长度是否正确
        if (dateTimeStr.length() != 19) {
            return false;
        }

        // 检查年份范围
        int year = std::stoi(dateTimeStr.substr(0, 4));
        if (year < 1000 || year > 9999) {
            return false;
        }

        // 检查月份范围
        int month = std::stoi(dateTimeStr.substr(5, 2));
        if (month < 1 || month > 12) {
            return false;
        }

        // 检查日期范围
        int day = std::stoi(dateTimeStr.substr(8, 2));
        if (day < 1) {
            return false;
        }

        int maxDay = 31; // 最大天数，默认为31

        // 根据月份设置最大天数
        if (month == 2) { // 2月份特殊处理
            // 判断是否为闰年
            bool isLeapYear = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
            if (isLeapYear) {
                maxDay = 29;
            } else {
                maxDay = 28;
            }
        } else if (month == 4 || month == 6 || month == 9 || month == 11) {
            maxDay = 30;
        }

        if (day > maxDay) {
            return false;
        }

        // 检查小时范围
        int hour = std::stoi(dateTimeStr.substr(11, 2));
        if (hour < 0 || hour > 23) {
            return false;
        }

        // 检查分钟范围
        int minute = std::stoi(dateTimeStr.substr(14, 2));
        if (minute < 0 || minute > 59) {
            return false;
        }

        // 检查秒数范围
        int second = std::stoi(dateTimeStr.substr(17, 2));
        if (second < 0 || second > 59) {
            return false;
        }

        return true;
    }
};

enum CompOp { OP_EQ, OP_NE, OP_LT, OP_GT, OP_LE, OP_GE };

struct Condition {
    TabCol lhs_col;   // left-hand side column
    CompOp op;        // comparison operator
    bool is_rhs_val;  // true if right-hand side is a value (not a column)
    TabCol rhs_col;   // right-hand side column
    Value rhs_val;    // right-hand side value
};

struct SetClause {
    TabCol lhs;
    Value rhs;
};