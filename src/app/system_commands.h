#pragma once
#include "catalog/catalog.h"
#include "optimizer/optimizer.h"
#include "sql/binder.h"
#include <string>
namespace minidb {
class SystemCommands {
public:
    static bool TryExecute(const std::string& line, Catalog& catalog,
                           Binder& binder, Optimizer& optimizer);
};
}  // namespace minidb
