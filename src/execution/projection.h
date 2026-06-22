#pragma once

#include "execution/executor.h"
#include "sql/ast.h"
#include "catalog/schema.h"
#include <memory>
#include <vector>

namespace minidb {

// Projection executor — selects specific columns from tuples.
class ProjectionExecutor : public Executor {
public:
    ProjectionExecutor(std::unique_ptr<Executor> child,
                       const std::vector<ExprPtr>& output_exprs,
                       const Schema& input_schema);

    void Open() override;
    bool Next(Tuple& tuple, RID& rid) override;
    void Close() override;
    Status GetStatus() const override { return child_->GetStatus(); }

private:
    std::unique_ptr<Executor> child_;
    std::vector<ExprPtr> output_exprs_;
    Schema input_schema_;
};

}  // namespace minidb
