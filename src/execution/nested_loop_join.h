#pragma once

#include "execution/executor.h"
#include "sql/ast.h"
#include "catalog/schema.h"
#include <memory>

namespace minidb {

// Nested loop join executor.
class NestedLoopJoinExecutor : public Executor {
public:
    NestedLoopJoinExecutor(std::unique_ptr<Executor> outer,
                           std::unique_ptr<Executor> inner,
                           ExprPtr join_condition,
                           const Schema& outer_schema,
                           const Schema& inner_schema);

    void Open() override;
    bool Next(Tuple& tuple, RID& rid) override;
    void Close() override;
    Status GetStatus() const override {
        if (!outer_->GetStatus().ok()) return outer_->GetStatus();
        return inner_->GetStatus();
    }

private:
    bool EvalJoinCondition(const Tuple& outer_tuple, const Tuple& inner_tuple);

    std::unique_ptr<Executor> outer_;
    std::unique_ptr<Executor> inner_;
    ExprPtr join_condition_;
    Schema outer_schema_;
    Schema inner_schema_;

    // State
    std::vector<std::pair<RID, Tuple>> outer_results_;
    std::vector<std::pair<RID, Tuple>> inner_results_;
    size_t outer_idx_;
    size_t inner_idx_;
    bool initialized_;
};

}  // namespace minidb
