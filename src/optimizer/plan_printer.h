#pragma once
#include "optimizer/optimizer.h"
#include <string>
namespace minidb {
class PlanPrinter {
public:
    static std::string Print(const PlanNodePtr& plan, int depth = 0);
};
}  // namespace minidb
