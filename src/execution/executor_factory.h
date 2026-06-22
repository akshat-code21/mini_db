#pragma once

#include "execution/executor.h"
#include "optimizer/optimizer.h"
#include "catalog/catalog.h"
#include "index/index_manager.h"
#include "optimizer/stats.h"
#include "execution/execution_context.h"
#include <memory>

namespace minidb {

// Factory that builds an executor tree from a physical plan.
class ExecutorFactory {
public:
    ExecutorFactory(Catalog* catalog, IndexManager* index_mgr, StatsManager* stats_mgr,
                    ExecutionContext context = {});

    void SetContext(ExecutionContext context) { context_ = context; }

    std::unique_ptr<Executor> Build(PlanNodePtr plan);

private:
    std::unique_ptr<Executor> BuildSeqScan(PlanNodePtr plan);
    std::unique_ptr<Executor> BuildIndexScan(PlanNodePtr plan);
    std::unique_ptr<Executor> BuildFilter(PlanNodePtr plan);
    std::unique_ptr<Executor> BuildProjection(PlanNodePtr plan);
    std::unique_ptr<Executor> BuildNestedLoopJoin(PlanNodePtr plan);
    std::unique_ptr<Executor> BuildInsert(PlanNodePtr plan);
    std::unique_ptr<Executor> BuildDelete(PlanNodePtr plan);

    // Get combined schema for a plan subtree (for joins)
    Schema GetPlanSchema(PlanNodePtr plan);

    Catalog* catalog_;
    IndexManager* index_mgr_;
    StatsManager* stats_mgr_;
    ExecutionContext context_;
};

}  // namespace minidb
