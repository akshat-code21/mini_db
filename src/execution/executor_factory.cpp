#include "execution/executor_factory.h"
#include "execution/seq_scan.h"
#include "execution/index_scan.h"
#include "execution/filter.h"
#include "execution/projection.h"
#include "execution/nested_loop_join.h"
#include "execution/insert_executor.h"
#include "execution/delete_executor.h"

namespace minidb {

ExecutorFactory::ExecutorFactory(Catalog* catalog, IndexManager* index_mgr, StatsManager* stats_mgr,
                                 ExecutionContext context)
    : catalog_(catalog), index_mgr_(index_mgr), stats_mgr_(stats_mgr), context_(context) {}

std::unique_ptr<Executor> ExecutorFactory::Build(PlanNodePtr plan) {
    if (!plan) return nullptr;

    switch (plan->type) {
        case PlanNodeType::SEQ_SCAN:
            return BuildSeqScan(plan);
        case PlanNodeType::INDEX_SCAN:
            return BuildIndexScan(plan);
        case PlanNodeType::FILTER:
            return BuildFilter(plan);
        case PlanNodeType::PROJECTION:
            return BuildProjection(plan);
        case PlanNodeType::NESTED_LOOP_JOIN:
            return BuildNestedLoopJoin(plan);
        case PlanNodeType::INSERT:
            return BuildInsert(plan);
        case PlanNodeType::DELETE_PLAN:
            return BuildDelete(plan);
        default:
            return nullptr;
    }
}

std::unique_ptr<Executor> ExecutorFactory::BuildSeqScan(PlanNodePtr plan) {
    HeapFile* heap = catalog_->GetHeapFile(plan->table_name);
    TableInfo* info = catalog_->GetTable(plan->table_name);
    if (!heap || !info) return nullptr;

    return std::make_unique<SeqScanExecutor>(heap, info->schema, context_,
                                             RID(info->heap_file_page_id, UINT16_MAX));
}

std::unique_ptr<Executor> ExecutorFactory::BuildIndexScan(PlanNodePtr plan) {
    HeapFile* heap = catalog_->GetHeapFile(plan->table_name);
    TableInfo* info = catalog_->GetTable(plan->table_name);
    BPlusTree* index = index_mgr_->GetIndex(plan->table_name, "");
    if (!heap || !info || !index) return nullptr;

    return std::make_unique<IndexScanExecutor>(index, heap, info->schema, plan->index_key,
                                               context_, RID(info->heap_file_page_id, UINT16_MAX));
}

std::unique_ptr<Executor> ExecutorFactory::BuildFilter(PlanNodePtr plan) {
    if (plan->children.empty()) return nullptr;

    auto child = Build(plan->children[0]);
    Schema schema = GetPlanSchema(plan->children[0]);

    // Determine table name from child
    std::string table_name;
    if (plan->children[0]->type == PlanNodeType::SEQ_SCAN) {
        table_name = plan->children[0]->table_name;
    }

    return std::make_unique<FilterExecutor>(std::move(child), plan->predicate, schema, table_name);
}

std::unique_ptr<Executor> ExecutorFactory::BuildProjection(PlanNodePtr plan) {
    if (plan->children.empty()) return nullptr;

    auto child = Build(plan->children[0]);
    Schema schema = GetPlanSchema(plan->children[0]);

    return std::make_unique<ProjectionExecutor>(std::move(child), plan->output_exprs, schema);
}

std::unique_ptr<Executor> ExecutorFactory::BuildNestedLoopJoin(PlanNodePtr plan) {
    if (plan->children.size() < 2) return nullptr;

    auto outer = Build(plan->children[0]);
    auto inner = Build(plan->children[1]);

    Schema outer_schema = GetPlanSchema(plan->children[0]);
    Schema inner_schema = GetPlanSchema(plan->children[1]);

    return std::make_unique<NestedLoopJoinExecutor>(
        std::move(outer), std::move(inner),
        plan->predicate, outer_schema, inner_schema);
}

std::unique_ptr<Executor> ExecutorFactory::BuildInsert(PlanNodePtr plan) {
    HeapFile* heap = catalog_->GetHeapFile(plan->table_name);
    TableInfo* info = catalog_->GetTable(plan->table_name);
    if (!heap || !info) return nullptr;

    BPlusTree* index = index_mgr_->GetIndex(plan->table_name, "");

    return std::make_unique<InsertExecutor>(
        heap, index, info->schema, stats_mgr_, plan->table_name, plan->tuples_to_insert,
        context_, RID(info->heap_file_page_id, UINT16_MAX));
}

std::unique_ptr<Executor> ExecutorFactory::BuildDelete(PlanNodePtr plan) {
    if (plan->children.empty()) return nullptr;

    auto child = Build(plan->children[0]);
    HeapFile* heap = catalog_->GetHeapFile(plan->table_name);
    TableInfo* info = catalog_->GetTable(plan->table_name);
    BPlusTree* index = index_mgr_->GetIndex(plan->table_name, "");

    if (!heap || !info) return nullptr;

    return std::make_unique<DeleteExecutor>(
        std::move(child), heap, index, info->schema, stats_mgr_, plan->table_name,
        context_, RID(info->heap_file_page_id, UINT16_MAX));
}

Schema ExecutorFactory::GetPlanSchema(PlanNodePtr plan) {
    switch (plan->type) {
        case PlanNodeType::SEQ_SCAN:
        case PlanNodeType::INDEX_SCAN: {
            TableInfo* info = catalog_->GetTable(plan->table_name);
            if (info) {
                auto columns = info->schema.GetColumns();
                for (auto& column : columns) column.table_name = plan->table_name;
                return Schema(columns);
            }
            return Schema();
        }
        case PlanNodeType::FILTER:
        case PlanNodeType::PROJECTION:
            if (!plan->children.empty()) return GetPlanSchema(plan->children[0]);
            return Schema();
        case PlanNodeType::NESTED_LOOP_JOIN: {
            // Combined schema from both sides
            if (plan->children.size() >= 2) {
                Schema outer = GetPlanSchema(plan->children[0]);
                Schema inner = GetPlanSchema(plan->children[1]);
                std::vector<Column> combined;
                for (const auto& c : outer.GetColumns()) combined.push_back(c);
                for (const auto& c : inner.GetColumns()) combined.push_back(c);
                return Schema(combined);
            }
            return Schema();
        }
        default:
            return Schema();
    }
}

}  // namespace minidb
