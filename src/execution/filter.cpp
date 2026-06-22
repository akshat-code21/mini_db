#include "execution/filter.h"

namespace minidb {

FilterExecutor::FilterExecutor(std::unique_ptr<Executor> child, ExprPtr predicate,
                               const Schema& schema, const std::string& table_name)
    : child_(std::move(child)), predicate_(predicate), schema_(schema), table_name_(table_name) {}

void FilterExecutor::Open() {
    child_->Open();
}

bool FilterExecutor::Next(Tuple& tuple, RID& rid) {
    while (child_->Next(tuple, rid)) {
        if (EvalPredicate(predicate_, tuple, schema_, table_name_)) {
            return true;
        }
    }
    return false;
}

void FilterExecutor::Close() {
    child_->Close();
}

Value FilterExecutor::EvalExpr(const ExprPtr& expr, const Tuple& tuple,
                                const Schema& schema, const std::string& table_name) {
    if (auto* lit = dynamic_cast<LiteralExpr*>(expr.get())) {
        return lit->value;
    }

    if (auto* col = dynamic_cast<ColumnRefExpr*>(expr.get())) {
        int idx = schema.FindColumn(col->table_name, col->column_name);
        if (idx >= 0 && idx < static_cast<int>(tuple.size())) {
            return tuple[idx];
        }
        return Value(0);  // Default
    }

    if (auto* bin = dynamic_cast<BinaryOpExpr*>(expr.get())) {
        Value left = EvalExpr(bin->left, tuple, schema, table_name);
        Value right = EvalExpr(bin->right, tuple, schema, table_name);

        // Handle AND/OR
        if (bin->op == BinaryOpType::AND) {
            return Value(std::get<bool>(left) && std::get<bool>(right));
        }
        if (bin->op == BinaryOpType::OR) {
            return Value(std::get<bool>(left) || std::get<bool>(right));
        }

        // Comparison operations
        bool result = false;

        // Compare based on types
        if (std::holds_alternative<int32_t>(left) && std::holds_alternative<int32_t>(right)) {
            int32_t l = std::get<int32_t>(left), r = std::get<int32_t>(right);
            switch (bin->op) {
                case BinaryOpType::EQ:  result = (l == r); break;
                case BinaryOpType::NEQ: result = (l != r); break;
                case BinaryOpType::LT:  result = (l < r); break;
                case BinaryOpType::GT:  result = (l > r); break;
                case BinaryOpType::LTE: result = (l <= r); break;
                case BinaryOpType::GTE: result = (l >= r); break;
                default: break;
            }
        } else if (std::holds_alternative<double>(left) || std::holds_alternative<double>(right)) {
            double l = std::holds_alternative<double>(left) ? std::get<double>(left)
                       : static_cast<double>(std::get<int32_t>(left));
            double r = std::holds_alternative<double>(right) ? std::get<double>(right)
                       : static_cast<double>(std::get<int32_t>(right));
            switch (bin->op) {
                case BinaryOpType::EQ:  result = (l == r); break;
                case BinaryOpType::NEQ: result = (l != r); break;
                case BinaryOpType::LT:  result = (l < r); break;
                case BinaryOpType::GT:  result = (l > r); break;
                case BinaryOpType::LTE: result = (l <= r); break;
                case BinaryOpType::GTE: result = (l >= r); break;
                default: break;
            }
        } else if (std::holds_alternative<std::string>(left) && std::holds_alternative<std::string>(right)) {
            const auto& l = std::get<std::string>(left);
            const auto& r = std::get<std::string>(right);
            switch (bin->op) {
                case BinaryOpType::EQ:  result = (l == r); break;
                case BinaryOpType::NEQ: result = (l != r); break;
                case BinaryOpType::LT:  result = (l < r); break;
                case BinaryOpType::GT:  result = (l > r); break;
                case BinaryOpType::LTE: result = (l <= r); break;
                case BinaryOpType::GTE: result = (l >= r); break;
                default: break;
            }
        }

        return Value(result);
    }

    return Value(false);
}

bool FilterExecutor::EvalPredicate(const ExprPtr& expr, const Tuple& tuple,
                                    const Schema& schema, const std::string& table_name) {
    Value result = EvalExpr(expr, tuple, schema, table_name);
    if (std::holds_alternative<bool>(result)) {
        return std::get<bool>(result);
    }
    return false;
}

}  // namespace minidb
