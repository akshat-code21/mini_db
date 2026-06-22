#pragma once

#include "execution/executor.h"
#include "index/b_plus_tree.h"
#include "storage/heap_file.h"
#include "catalog/schema.h"
#include "execution/execution_context.h"

namespace minidb {

// Index scan — uses B+ Tree to find records by key.
class IndexScanExecutor : public Executor {
public:
    IndexScanExecutor(BPlusTree* index, HeapFile* heap_file, const Schema& schema, int32_t key,
                      ExecutionContext context = {}, RID table_lock = {});

    void Open() override;
    bool Next(Tuple& tuple, RID& rid) override;
    void Close() override;

private:
    BPlusTree* index_;
    HeapFile* heap_file_;
    Schema schema_;
    int32_t key_;

    bool found_;
    RID result_rid_;
    Tuple result_tuple_;
    bool consumed_;
    ExecutionContext context_;
    RID table_lock_;
    Status status_;
public:
    Status GetStatus() const override { return status_; }
};

}  // namespace minidb
