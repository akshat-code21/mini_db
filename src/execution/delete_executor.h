#pragma once

#include "execution/executor.h"
#include "storage/heap_file.h"
#include "index/b_plus_tree.h"
#include "catalog/schema.h"
#include "optimizer/stats.h"
#include <memory>
#include <string>
#include "execution/execution_context.h"

namespace minidb {

// Delete executor — deletes tuples matching a condition.
class DeleteExecutor : public Executor {
public:
    DeleteExecutor(std::unique_ptr<Executor> child, HeapFile* heap_file,
                   BPlusTree* index, const Schema& schema,
                   StatsManager* stats_mgr, const std::string& table_name,
                   ExecutionContext context = {}, RID table_lock = {});

    void Open() override;
    bool Next(Tuple& tuple, RID& rid) override;
    void Close() override;

    int GetDeletedCount() const { return deleted_count_; }

private:
    std::unique_ptr<Executor> child_;
    HeapFile* heap_file_;
    BPlusTree* index_;
    Schema schema_;
    StatsManager* stats_mgr_;
    std::string table_name_;
    int deleted_count_;
    bool done_;
    ExecutionContext context_;
    RID table_lock_;
    Status status_;
public:
    Status GetStatus() const override { return status_; }
};

}  // namespace minidb
