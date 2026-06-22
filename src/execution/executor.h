#pragma once

#include "common/types.h"
#include "common/rid.h"
#include "common/status.h"
#include <vector>

namespace minidb {

// Base executor interface (Volcano model: Open/Next/Close)
class Executor {
public:
    virtual ~Executor() = default;

    // Initialize the executor
    virtual void Open() = 0;

    // Get the next tuple. Returns false when no more tuples.
    // Also sets 'rid' to the source record ID (for DELETE support).
    virtual bool Next(Tuple& tuple, RID& rid) = 0;

    // Clean up resources
    virtual void Close() = 0;

    virtual Status GetStatus() const { return Status::OK(); }
};

}  // namespace minidb
