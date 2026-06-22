#include "execution/nested_loop_join.h"

namespace minidb {

NestedLoopJoinExecutor::NestedLoopJoinExecutor(std::unique_ptr<Executor> outer,
                                               std::unique_ptr<Executor> inner,
                                               ExprPtr condition,
                                               const Schema& outer_schema,
                                               const Schema& inner_schema)
    : outer_(std::move(outer)), inner_(std::move(inner)),
      join_condition_(std::move(condition)), outer_schema_(outer_schema),
      inner_schema_(inner_schema), outer_idx_(0), inner_idx_(0), initialized_(false) {}

void NestedLoopJoinExecutor::Open() {
    outer_->Open();
    inner_->Open();
    outer_results_.clear();
    inner_results_.clear();
    Tuple tuple;
    RID rid;
    while (outer_->Next(tuple, rid)) outer_results_.push_back({rid, tuple});
    while (inner_->Next(tuple, rid)) inner_results_.push_back({rid, tuple});
    outer_idx_ = inner_idx_ = 0;
    initialized_ = true;
}

bool NestedLoopJoinExecutor::Next(Tuple& tuple, RID& rid) {
    while (outer_idx_ < outer_results_.size()) {
        while (inner_idx_ < inner_results_.size()) {
            const auto& outer_tuple = outer_results_[outer_idx_].second;
            const auto& inner_tuple = inner_results_[inner_idx_++].second;
            if (!EvalJoinCondition(outer_tuple, inner_tuple)) continue;
            tuple = outer_tuple;
            tuple.insert(tuple.end(), inner_tuple.begin(), inner_tuple.end());
            rid = outer_results_[outer_idx_].first;
            return true;
        }
        ++outer_idx_;
        inner_idx_ = 0;
    }
    return false;
}

void NestedLoopJoinExecutor::Close() {
    outer_->Close();
    inner_->Close();
    outer_results_.clear();
    inner_results_.clear();
}

bool NestedLoopJoinExecutor::EvalJoinCondition(const Tuple& outer_tuple,
                                               const Tuple& inner_tuple) {
    if (!join_condition_) return true;
    auto* binary = dynamic_cast<BinaryOpExpr*>(join_condition_.get());
    if (!binary) return false;
    auto* left = dynamic_cast<ColumnRefExpr*>(binary->left.get());
    auto* right = dynamic_cast<ColumnRefExpr*>(binary->right.get());
    if (!left || !right) return false;

    auto resolve = [&](ColumnRefExpr* column, Value& value) {
        int index = outer_schema_.FindColumn(column->table_name, column->column_name);
        if (index >= 0) { value = outer_tuple[index]; return true; }
        index = inner_schema_.FindColumn(column->table_name, column->column_name);
        if (index >= 0) { value = inner_tuple[index]; return true; }
        return false;
    };

    Value left_value, right_value;
    if (!resolve(left, left_value) || !resolve(right, right_value)) return false;
    switch (binary->op) {
        case BinaryOpType::EQ: return left_value == right_value;
        case BinaryOpType::NEQ: return left_value != right_value;
        default: return false;
    }
}

}  // namespace minidb
