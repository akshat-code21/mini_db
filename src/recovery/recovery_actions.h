#pragma once
#include "catalog/catalog.h"
#include "index/index_manager.h"
#include "recovery/log_record.h"
namespace minidb {
class RecoveryActions {
public:
    RecoveryActions(Catalog* catalog, IndexManager* indexes) : catalog_(catalog), indexes_(indexes) {}
    void EnsureInserted(const LogRecord& record);
    void EnsureDeleted(const LogRecord& record);
private:
    bool Find(const LogRecord& record, RID& rid);
    Catalog* catalog_;
    IndexManager* indexes_;
};
}  // namespace minidb
