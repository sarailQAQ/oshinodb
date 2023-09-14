/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "analyze.h"

/**
 * @description: 分析器，进行语义分析和查询重写，需要检查不符合语义规定的部分
 * @param {shared_ptr<ast::TreeNode>} parse parser生成的结果集
 * @return {shared_ptr<Query>} Query 
 */
std::shared_ptr<Query> Analyze::do_analyze(std::shared_ptr<ast::TreeNode> parse) {
    std::shared_ptr<Query> query = std::make_shared<Query>();
    if (auto x = std::dynamic_pointer_cast<ast::SelectStmt>(parse)) {
        // 处理表名
        query->tables = std::move(x->tabs);
        /** TODO: 检查表是否存在 */

        std::vector<ColMeta> all_cols;
        get_all_cols(query->tables, all_cols);
        assert(!all_cols.empty());
        // 处理target list，再target list中添加上表名，例如 a.id
        for (auto &sv_sel_col: x->cols) {
            // 处理 COUNT(*)
            std::string col_name = all_cols[0].name, tab_name;
            if (sv_sel_col->col) {
                col_name = sv_sel_col->col->col_name;
                tab_name = sv_sel_col->col->tab_name;
            }
            TabCol sel_col = {.tab_name = tab_name, .col_name = col_name, .func = convert_sv_func(
                    sv_sel_col->sv_func), .alias = sv_sel_col->alias,.is_desc = false};
//            sel_col.alias = ;
            query->cols.push_back(sel_col);
        }

        if (query->cols.empty()) {
            // select all columns
            for (auto &col: all_cols) {
                TabCol sel_col = {.tab_name = col.tab_name, .col_name = col.name};
                query->cols.push_back(sel_col);
            }
        } else {
            // infer table name from column name
            for (auto &sel_col: query->cols) {
                sel_col = check_column(all_cols, sel_col);  // 列元数据校验
            }
        }

        query->order_by_cols.clear();
        if (x->has_sort) {
            for (auto &ord: x->orders) {
                auto col = TabCol{.tab_name=ord->col->tab_name, .col_name=ord->col->col_name, .func=FUNC_NULL, .alias=""};
                col = check_column(all_cols, col);
                col.is_desc = ord->orderby_dir == ast::OrderBy_DESC;
                query->order_by_cols.push_back(col);
            }
        }
        query->limit = x->limit;
        //处理where条件
        get_clause(x->conds, query->conds);
        check_clause(query->tables, query->conds);
    } else if (auto x = std::dynamic_pointer_cast<ast::UpdateStmt>(parse)) {
        /** TODO: */
        for (const auto &sv_set_clause: x->set_clauses) {
            query->set_clauses.push_back({
                                                 .lhs = {.tab_name = "", .col_name = sv_set_clause->col_name},
                                                 .rhs = convert_sv_value(sv_set_clause->val)
                                         });
        }
        get_clause(x->conds, query->conds);
        check_clause({x->tab_name}, query->conds);
    } else if (auto x = std::dynamic_pointer_cast<ast::DeleteStmt>(parse)) {
        //处理where条件
        get_clause(x->conds, query->conds);
        check_clause({x->tab_name}, query->conds);
    } else if (auto x = std::dynamic_pointer_cast<ast::InsertStmt>(parse)) {
        // 处理insert 的values值
        for (auto &sv_val: x->vals) {
            query->values.push_back(convert_sv_value(sv_val));
        }
    } else {
        // do nothing
    }
    query->parse = std::move(parse);
    return query;
}


TabCol Analyze::check_column(const std::vector<ColMeta> &all_cols, TabCol target) {
    if (target.tab_name.empty()) {
        // Table name not specified, infer table name from column name
        std::string tab_name;
        for (auto &col: all_cols) {
            if (col.name == target.col_name) {
                if (!tab_name.empty()) {
                    throw AmbiguousColumnError(target.col_name);
                }
                tab_name = col.tab_name;
            }
        }
        if (tab_name.empty()) {
            throw ColumnNotFoundError(target.col_name);
        }
        target.tab_name = tab_name;
    } else {
        /** TODO: Make sure target column exists */

    }
    return target;
}

void Analyze::get_all_cols(const std::vector<std::string> &tab_names, std::vector<ColMeta> &all_cols) {
    for (auto &sel_tab_name: tab_names) {
        // 这里db_不能写成get_db(), 注意要传指针
        const auto sel_tab_cols = sm_manager_->db_.get_table(sel_tab_name).cols;
        all_cols.insert(all_cols.end(), sel_tab_cols.begin(), sel_tab_cols.end());
    }
}

void Analyze::get_clause(const std::vector<std::shared_ptr<ast::BinaryExpr>> &sv_conds, std::vector<Condition> &conds) {
    conds.clear();
    for (auto &expr: sv_conds) {
        Condition cond;
        cond.lhs_col = {.tab_name = expr->lhs->tab_name, .col_name = expr->lhs->col_name};
        cond.op = convert_sv_comp_op(expr->op);
        if (auto rhs_val = std::dynamic_pointer_cast<ast::Value>(expr->rhs)) {
            cond.is_rhs_val = true;
            cond.rhs_val = convert_sv_value(rhs_val);
        } else if (auto rhs_col = std::dynamic_pointer_cast<ast::Col>(expr->rhs)) {
            cond.is_rhs_val = false;
            cond.rhs_col = {.tab_name = rhs_col->tab_name, .col_name = rhs_col->col_name};
        }
        conds.push_back(cond);
    }
}

void Analyze::check_clause(const std::vector<std::string> &tab_names, std::vector<Condition> &conds) {
    // auto all_cols = get_all_cols(tab_names);
    std::vector<ColMeta> all_cols;
    get_all_cols(tab_names, all_cols);
    // Get raw values in where clause
    for (auto &cond: conds) {
        // Infer table name from column name
        cond.lhs_col = check_column(all_cols, cond.lhs_col);
        if (!cond.is_rhs_val) {
            cond.rhs_col = check_column(all_cols, cond.rhs_col);
        }
        TabMeta lhs_tab = sm_manager_->db_.get_table(cond.lhs_col.tab_name);
        auto lhs_col = lhs_tab.get_col(cond.lhs_col.col_name);
        ColType lhs_type = lhs_col->type;
        ColType rhs_type;
        if (cond.is_rhs_val) {
            if (lhs_type != cond.rhs_val.type) {
                // 尝试对 bigint 列进行对应的数值转换
                if (lhs_type == TYPE_BIGINT) {
                    if (cond.rhs_val.type == TYPE_INT) {
                        // 可以赋值、比较
                        cond.rhs_val.set_bigint(cond.rhs_val.int_val);
                        cond.rhs_val.init_raw(sizeof(long long));
                    } else if (cond.rhs_val.type == TYPE_FLOAT) {
                        // 只能赋值
                        cond.rhs_val.init_raw(sizeof(float));
                    } else
                        throw IncompatibleTypeError(coltype2str(lhs_type), coltype2str(cond.rhs_val.type));
                } else if (lhs_type == TYPE_DATETIME && cond.rhs_val.type == TYPE_STRING) {
                    // datetime 列进行转换，改变类型即可
                    if (!cond.rhs_val.is_valid_datetime()) throw InternalError("invalid datetime");
                    cond.rhs_val.type = TYPE_DATETIME;
                    cond.rhs_val.init_raw(lhs_col->len);
                } else
                    throw IncompatibleTypeError(coltype2str(lhs_type), coltype2str(cond.rhs_val.type));
            } else cond.rhs_val.init_raw(lhs_col->len);
            rhs_type = cond.rhs_val.type;
        } else {
            TabMeta rhs_tab = sm_manager_->db_.get_table(cond.rhs_col.tab_name);
            auto rhs_col = rhs_tab.get_col(cond.rhs_col.col_name);
            rhs_type = rhs_col->type;
        }
        if (lhs_type != rhs_type) {
            std::cerr << "[warning]Incompatible Type\n";
//            throw IncompatibleTypeError(coltype2str(lhs_type), coltype2str(rhs_type));
        }
    }
}


Value Analyze::convert_sv_value(const std::shared_ptr<ast::Value> sv_val) {
    Value val;
    if (auto int_lit = std::dynamic_pointer_cast<ast::IntLit>(sv_val)) {
        val.set_int(int_lit->val);
    } else if (auto float_lit = std::dynamic_pointer_cast<ast::FloatLit>(sv_val)) {
        val.set_float(float_lit->val);
    } else if (auto str_lit = std::dynamic_pointer_cast<ast::StringLit>(sv_val)) {
        val.set_str(str_lit->val);
    } else if (auto bigint_lit = std::dynamic_pointer_cast<ast::BigintLit>(sv_val)) {
        val.set_bigint(bigint_lit->val);
    } else {
        throw InternalError("Unexpected sv value type");
    }
    return val;
}

CompOp Analyze::convert_sv_comp_op(ast::SvCompOp op) {
    std::map<ast::SvCompOp, CompOp> m = {
            {ast::SV_OP_EQ, OP_EQ},
            {ast::SV_OP_NE, OP_NE},
            {ast::SV_OP_LT, OP_LT},
            {ast::SV_OP_GT, OP_GT},
            {ast::SV_OP_LE, OP_LE},
            {ast::SV_OP_GE, OP_GE},
    };
    return m.at(op);
}

Func Analyze::convert_sv_func(ast::SvFunc fun) {
    if (fun == ast::SV_FUNC_NULL) return FUNC_NULL;
    else if (fun == ast::SV_FUNC_COUNT) return FUNC_COUNT;
    else if (fun == ast::SV_FUNC_MAX) return FUNC_MAX;
    else if (fun == ast::SV_FUNC_MIN) return FUNC_MIN;
    else if (fun == ast::SV_FUNC_SUM) return FUNC_SUM;
    else throw InternalError("Unexpected sv func");
}

