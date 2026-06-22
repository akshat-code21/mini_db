#pragma once

#include "catalog/table_info.h"
#include "common/status.h"
#include <string>
#include <vector>

namespace minidb {

class CatalogStorage {
public:
    static Status Save(const std::string& path, const std::vector<TableInfo>& tables);
    static Status Load(const std::string& path, std::vector<TableInfo>& tables);
};

}  // namespace minidb
