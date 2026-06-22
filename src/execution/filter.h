#pragma once

#include "execution/executor.h"
#include "sql/ast.h"
#include "catalog/schema.h"
#include <memory>
#include <string>

namespace minidb {

// Filter executor — evaluates WHERE clause predicates.
class FilterExecutor : public Executor {
public:
    FilterExecutor(std::unique_ptr<Executor> child, ExprPtr predicate,
                   const Schema& schema, const std::string& table_name = "");

    void Open() override;
    bool Next(Tuple& tuple, RID& rid) override;
    void Close() override;
    Status GetStatus() const override { return child_->GetStatus(); }

    // Evaluate an expression against a tuple
    static Value EvalExpr(const ExprPtr& expr, const Tuple& tuple,
                          const Schema& schema, const std::string& table_name = "");
    static bool EvalPredicate(const ExprPtr& expr, const Tuple& tuple,
                              const Schema& schema, const std::string& table_name = "");

private:
    std::unique_ptr<Executor> child_;
    ExprPtr predicate_;
    Schema schema_;
    std::string table_name_;
};

}  // namespace minidb
