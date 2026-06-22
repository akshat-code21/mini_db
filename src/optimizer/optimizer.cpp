#include "optimizer/optimizer.h"
#include <algorithm>

namespace minidb {

Optimizer::Optimizer(Catalog* catalog, IndexManager* index_mgr, StatsManager* stats_mgr)
    : catalog_(catalog), index_mgr_(index_mgr), stats_mgr_(stats_mgr) {}

PlanNodePtr Optimizer::Optimize(std::shared_ptr<ASTNode> ast) {
    switch (ast->type) {
        case ASTNodeType::SELECT:
            return OptimizeSelect(std::get<SelectStmt>(ast->stmt));
        case ASTNodeType::INSERT:
            return OptimizeInsert(std::get<InsertStmt>(ast->stmt));
        case ASTNodeType::DELETE_STMT:
            return OptimizeDelete(std::get<DeleteStmt>(ast->stmt));
        default:
            return nullptr;
    }
}

PlanNodePtr Optimizer::OptimizeSelect(SelectStmt& stmt) {
    PlanNodePtr scan;

    if (stmt.joins.empty()) {
        // Single table query
        scan = MakeTableScan(stmt.from_tables[0], stmt.where_clause);
    } else {
        // Multi-table join
        std::vector<std::string> tables = stmt.from_tables;
        std::vector<ExprPtr> join_conditions;
        for (auto& join : stmt.joins) {
            tables.push_back(join.table_name);
            join_conditions.push_back(join.on_condition);
        }
        scan = BuildJoinPlan(tables, join_conditions, stmt.where_clause);
    }

    // Add projection if not SELECT *
    bool has_star = false;
    for (const auto& expr : stmt.select_list) {
        if (dynamic_cast<StarExpr*>(expr.get())) {
            has_star = true;
            break;
        }
    }

    if (!has_star && !stmt.select_list.empty()) {
        auto proj = std::make_shared<PlanNode>(PlanNodeType::PROJECTION);
        proj->output_exprs = stmt.select_list;
        proj->children.push_back(scan);
        return proj;
    }

    return scan;
}

PlanNodePtr Optimizer::OptimizeInsert(InsertStmt& stmt) {
    auto plan = std::make_shared<PlanNode>(PlanNodeType::INSERT);
    plan->table_name = stmt.table_name;

    // Convert expression lists to tuples
    TableInfo* info = catalog_->GetTable(stmt.table_name);
    if (!info) return nullptr;

    for (const auto& value_exprs : stmt.values_list) {
        Tuple tuple;
        for (size_t i = 0; i < value_exprs.size(); i++) {
            auto* lit = dynamic_cast<LiteralExpr*>(value_exprs[i].get());
            if (lit) {
                if (info->schema.GetColumn(i).type == ColumnType::FLOAT &&
                    std::holds_alternative<int32_t>(lit->value))
                    tuple.push_back(static_cast<double>(std::get<int32_t>(lit->value)));
                else
                    tuple.push_back(lit->value);
            }
        }
        plan->tuples_to_insert.push_back(std::move(tuple));
    }

    return plan;
}

PlanNodePtr Optimizer::OptimizeDelete(DeleteStmt& stmt) {
    auto scan = MakeTableScan(stmt.table_name, stmt.where_clause);

    auto plan = std::make_shared<PlanNode>(PlanNodeType::DELETE_PLAN);
    plan->table_name = stmt.table_name;
    plan->children.push_back(scan);

    return plan;
}

PlanNodePtr Optimizer::MakeTableScan(const std::string& table_name, ExprPtr predicate) {
    // Check if we can use an index
    int32_t key_value;
    if (predicate && index_mgr_->HasIndex(table_name, "") && IsPrimaryKeyEq(table_name, predicate, key_value)) {
        // Use index scan
        TableStats& stats = stats_mgr_->GetStats(table_name);
        double selectivity = CostModel::EstimateSelectivity(predicate, stats);

        if (CostModel::ShouldUseIndex(stats, selectivity)) {
            auto plan = std::make_shared<PlanNode>(PlanNodeType::INDEX_SCAN);
            plan->table_name = table_name;
            plan->use_index = true;
            plan->index_key = key_value;
            plan->predicate = predicate;
            return plan;
        }
    }

    // Default: sequential scan
    auto plan = std::make_shared<PlanNode>(PlanNodeType::SEQ_SCAN);
    plan->table_name = table_name;

    if (predicate) {
        auto filter = std::make_shared<PlanNode>(PlanNodeType::FILTER);
        filter->predicate = predicate;
        filter->children.push_back(plan);
        return filter;
    }

    return plan;
}

PlanNodePtr Optimizer::BuildJoinPlan(const std::vector<std::string>& tables,
                                     const std::vector<ExprPtr>& join_conditions,
                                     ExprPtr where_clause) {
    if (tables.empty()) return nullptr;

    // Simple join ordering: use dynamic programming for small number of tables
    // For 2 tables: try both orderings, pick cheaper
    // For 3+ tables preserve SQL order so ON predicates remain attached correctly.

    // Build scans for each table
    std::vector<PlanNodePtr> scans;
    for (const auto& table : tables) {
        auto scan = std::make_shared<PlanNode>(PlanNodeType::SEQ_SCAN);
        scan->table_name = table;
        scans.push_back(scan);
    }

    // For 2 tables: try both join orders
    if (tables.size() == 2 && join_conditions.size() == 1) {
        TableStats& s0 = stats_mgr_->GetStats(tables[0]);
        TableStats& s1 = stats_mgr_->GetStats(tables[1]);

        double cost_01 = CostModel::NLJoinCost(s0, s1);
        double cost_10 = CostModel::NLJoinCost(s1, s0);

        auto join = std::make_shared<PlanNode>(PlanNodeType::NESTED_LOOP_JOIN);
        join->predicate = join_conditions[0];

        if (cost_01 <= cost_10) {
            join->children.push_back(scans[0]);
            join->children.push_back(scans[1]);
        } else {
            join->children.push_back(scans[1]);
            join->children.push_back(scans[0]);
        }

        // Add WHERE filter if present
        if (where_clause) {
            auto filter = std::make_shared<PlanNode>(PlanNodeType::FILTER);
            filter->predicate = where_clause;
            filter->children.push_back(join);
            return filter;
        }

        return join;
    }

    // General case: left-deep join tree
    PlanNodePtr current = scans[0];
    for (size_t i = 1; i < scans.size(); i++) {
        auto join = std::make_shared<PlanNode>(PlanNodeType::NESTED_LOOP_JOIN);
        if (i - 1 < join_conditions.size()) {
            join->predicate = join_conditions[i - 1];
        }
        join->children.push_back(current);
        join->children.push_back(scans[i]);
        current = join;
    }

    // Add WHERE filter
    if (where_clause) {
        auto filter = std::make_shared<PlanNode>(PlanNodeType::FILTER);
        filter->predicate = where_clause;
        filter->children.push_back(current);
        return filter;
    }

    return current;
}

bool Optimizer::IsPrimaryKeyEq(const std::string& table_name, const ExprPtr& pred, int32_t& key_value) {
    auto* bin = dynamic_cast<BinaryOpExpr*>(pred.get());
    if (!bin || bin->op != BinaryOpType::EQ) return false;

    auto* col = dynamic_cast<ColumnRefExpr*>(bin->left.get());
    auto* lit = dynamic_cast<LiteralExpr*>(bin->right.get());

    if (!col || !lit) {
        // Try swapped
        col = dynamic_cast<ColumnRefExpr*>(bin->right.get());
        lit = dynamic_cast<LiteralExpr*>(bin->left.get());
    }

    if (!col || !lit) return false;

    TableInfo* info = catalog_->GetTable(table_name);
    if (!info) return false;

    int pk_idx = info->schema.GetPrimaryKeyIndex();
    if (pk_idx < 0) return false;

    if (col->column_name == info->schema.GetColumn(pk_idx).name) {
        if (std::holds_alternative<int32_t>(lit->value)) {
            key_value = std::get<int32_t>(lit->value);
            return true;
        }
    }

    return false;
}

}  // namespace minidb
