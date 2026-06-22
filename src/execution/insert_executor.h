#pragma once

#include "execution/executor.h"
#include "storage/heap_file.h"
#include "index/b_plus_tree.h"
#include "catalog/schema.h"
#include "optimizer/stats.h"
#include "common/status.h"
#include "execution/execution_context.h"
#include <vector>

namespace minidb {

// Insert executor — inserts tuples into a table.
class InsertExecutor : public Executor {
public:
    InsertExecutor(HeapFile* heap_file, BPlusTree* index, const Schema& schema,
                   StatsManager* stats_mgr, const std::string& table_name,
                   const std::vector<Tuple>& tuples,
                   ExecutionContext context = {}, RID table_lock = {});

    void Open() override;
    bool Next(Tuple& tuple, RID& rid) override;
    void Close() override;

    int GetInsertedCount() const { return inserted_count_; }

private:
    HeapFile* heap_file_;
    BPlusTree* index_;
    Schema schema_;
    StatsManager* stats_mgr_;
    std::string table_name_;
    std::vector<Tuple> tuples_;
    int inserted_count_;
    bool done_;
    ExecutionContext context_;
    RID table_lock_;
    Status status_;
public:
    Status GetStatus() const override { return status_; }
};

}  // namespace minidb
