#include "optimizer/plan_printer.h"
namespace minidb {
std::string PlanPrinter::Print(const PlanNodePtr& plan, int depth) {
    if (!plan) return {};
    static const char* names[] = {"SEQ_SCAN", "INDEX_SCAN", "FILTER", "PROJECTION",
                                  "NESTED_LOOP_JOIN", "INSERT", "DELETE"};
    std::string result(depth * 2, ' ');
    result += names[static_cast<int>(plan->type)];
    if (!plan->table_name.empty()) result += " [" + plan->table_name + "]";
    if (plan->type == PlanNodeType::INDEX_SCAN)
        result += " key=" + std::to_string(plan->index_key);
    result += '\n';
    for (const auto& child : plan->children) result += Print(child, depth + 1);
    return result;
}
}  // namespace minidb
