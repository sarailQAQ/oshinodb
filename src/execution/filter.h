//
// Created by Sarail Tenma on 2023/7/10.
//
#pragma once



#include <utility>
#include <vector>

#include "execution_defs.h"
#include "execution_manager.h"
#include "executor_abstract.h"

class Filter {
private:
    std::vector<Condition> conds_;

    std::vector<ColMeta>::const_iterator get_col(const std::vector<ColMeta> &rec_cols, const TabCol &target) {
        auto pos = std::find_if(rec_cols.begin(), rec_cols.end(), [&](const ColMeta &col) {
            return col.tab_name == target.tab_name && col.name == target.col_name;
        });
        if (pos == rec_cols.end()) {
            throw ColumnNotFoundError(target.tab_name + '.' + target.col_name);
        }
        return pos;
    }

    static bool judge(char *lhs, char *rhs, int len, ColType type, CompOp op) {
        int cmp = compare(lhs, rhs, len, type);

        if (op == OP_EQ) {
            return cmp == 0;
        } else if (op == OP_NE) {
            return cmp != 0;
        } else if (op == OP_LT) {
            return cmp < 0;
        } else if (op == OP_GT) {
            return cmp > 0;
        } else if (op == OP_LE) {
            return cmp <= 0;
        } else if (op == OP_GE) {
            return cmp >= 0;
        } else {
            throw InternalError("Unexpected op type");
        }
    }

public:
    Filter() = default;

    Filter(std::vector<Condition>& conds) { reset_conditions(conds); }

    ~Filter() = default;

    void reset_conditions(std::vector<Condition>& conds) {
        conds_ = conds;
    }

    static int compare(char *lhs, char *rhs, int len, ColType type) {
        int cmp;
        switch (type) {
            case TYPE_INT: {
                int ia = *(int *)lhs;
                int ib = *(int *)rhs;
                cmp = (ia < ib) ? -1 : ((ia > ib) ? 1 : 0);
                break;
            }
            case TYPE_BIGINT: {
                long long ia = *(long long *)lhs;
                long long ib = *(long long *)rhs;
                cmp = (ia < ib) ? -1 : ((ia > ib) ? 1 : 0);
                break;
            }
            case TYPE_FLOAT: {
                float fa = *(float *)lhs;
                float fb = *(float *)rhs;
                cmp = (fa < fb) ? -1 : ((fa > fb) ? 1 : 0);
                break;
            }
            case TYPE_DOUBLE: {
                long double fa = *(long double *)lhs;
                long double fb = *(long double *)rhs;
                cmp = (fa < fb) ? -1 : ((fa > fb) ? 1 : 0);
                break;
            }
            case TYPE_STRING:
                cmp = memcmp(lhs, rhs, len);
                break;
            case TYPE_DATETIME:
                cmp = memcmp(lhs, rhs, len);
                break;
            default:
                throw InternalError("Unexpected data type");
        }
        return cmp;
    }

    bool filter_single(std::vector<ColMeta> &rec_cols, Condition &cond, const RmRecord *rec) {
        auto lhs_col = get_col(rec_cols, cond.lhs_col);
        char *lhs = rec->data + lhs_col->offset;
        char *rhs;
        ColType rhs_type, lhs_type = lhs_col->type;
        if (cond.is_rhs_val) {
            rhs_type = cond.rhs_val.type;
            rhs = cond.rhs_val.raw->data;
        } else {
            // rhs is a column
            auto rhs_col = get_col(rec_cols, cond.rhs_col);
            rhs_type = rhs_col->type;
            rhs = rec->data + rhs_col->offset;
        }
        if (rhs_type != lhs_type) {
            std::cerr << "type changed\n";
            if (rhs_type == ColType::TYPE_STRING || lhs_col->type == ColType::TYPE_STRING)
                throw IncompatibleTypeError(coltype2str(lhs_col->type), coltype2str(rhs_type));
            for (auto& x : std::vector<std::pair<ColType, char**> >{
                std::make_pair(lhs_type, &lhs),
                std::make_pair(rhs_type, &rhs),
            }) {
                if (x.first == ColType::TYPE_DOUBLE) continue;

                auto type = x.first;
                auto src = x.second;
                long double res = 0;

                if (type == ColType::TYPE_INT) {
                    int ia = *(int *)*src;
                    res = ia;
                } else if (type == ColType::TYPE_BIGINT) {
                    long long lla = *(long long*)*src;
                    res = (long double)(lla);
                } else if (type == ColType::TYPE_FLOAT) {
                    float fa = *(float *)*src;
                    res = fa;
                }

                auto dst = new char[sizeof (res)];
                memcpy(dst, &res, sizeof (res));
                *src = dst;
            }
            lhs_type = rhs_type = TYPE_DOUBLE;
        }
        return judge(lhs, rhs, lhs_col->len, lhs_type, cond.op);
    }

    bool filter_join(std::vector<ColMeta> &left_cols, RmRecord *lrec, std::vector<ColMeta> &right_cols, RmRecord *rrec) {
        return std::all_of(conds_.begin(), conds_.end(), [&](Condition &cond) {
            if (cond.is_rhs_val) return filter_single(left_cols, cond, lrec);

            auto lhs_col = get_col(left_cols, cond.lhs_col);
            auto rhs_col = get_col(right_cols, cond.rhs_col);
            char *lhs = lrec->data + lhs_col->offset, *rhs = rrec->data + rhs_col->offset;

            ColType rhs_type = rhs_col->type, lhs_type = lhs_col->type;
            int len = lhs_col->len;
            if (rhs_type != lhs_type) {
                std::cerr << "type changed\n";
                if (rhs_type == ColType::TYPE_STRING || lhs_col->type == ColType::TYPE_STRING)
                    throw IncompatibleTypeError(coltype2str(lhs_col->type), coltype2str(rhs_type));
                if (rhs_type == ColType::TYPE_INT) {
                    float fa = *(int *)rhs;
                    auto data = new char[sizeof(float )];
                    memcpy(data, &fa, sizeof(fa));
                    rhs = data;
                    rhs_type = ColType::TYPE_FLOAT;
                }
                if (lhs_type == ColType::TYPE_INT || lhs_type == ColType::TYPE_BIGINT) {
                    float fa = *(int *)lhs;
                    auto data = new char[sizeof(float )];
                    memcpy(data, &fa, sizeof(fa));
                    lhs = data;
                    lhs_type = ColType::TYPE_FLOAT;
                }
                len = sizeof (float );
            }

            return judge(lhs, rhs, len, lhs_type, cond.op);
        });
    }

    bool filter(std::vector<ColMeta> &rec_cols, RmRecord *rec) {
        return std::all_of(conds_.begin(), conds_.end(),[&](Condition &cond) {
            return filter_single(rec_cols, cond, rec);
        });
    }
};
