#include "app/system_commands.h"
#include "extension/col_converter.h"
#include "optimizer/plan_printer.h"
#include "sql/lexer.h"
#include "sql/parser.h"
#include <iostream>
namespace minidb {
bool SystemCommands::TryExecute(const std::string& line, Catalog& catalog,
                                Binder& binder, Optimizer& optimizer) {
    if (line == "help") {
        std::cout << "SQL: CREATE TABLE, INSERT, SELECT [WHERE/JOIN], DELETE, EXPLAIN SELECT\n"
                  << "Types: INT, FLOAT, VARCHAR(n), BOOL\n"
                  << "Commands: \\tables, \\schema T, \\columnar T, exit\n";
        return true;
    }
    if (line == "\\tables") {
        auto names = catalog.GetTableNames();
        std::cout << "Tables (" << names.size() << "):\n";
        for (const auto& name : names)
            std::cout << "  " << name << " (" << catalog.GetTable(name)->row_count << " rows)\n";
        return true;
    }
    if (line.rfind("\\schema ", 0) == 0) {
        std::string name = line.substr(8); auto* table = catalog.GetTable(name);
        if (!table) { std::cout << "Error: Table '" << name << "' not found\n"; return true; }
        for (const auto& column : table->schema.GetColumns()) {
            std::cout << column.name << ' ' << column_type_to_string(column.type);
            if (column.type == ColumnType::VARCHAR) std::cout << '(' << column.max_length << ')';
            if (column.is_primary_key) std::cout << " PRIMARY KEY";
            std::cout << '\n';
        }
        return true;
    }
    if (line.rfind("\\columnar ", 0) == 0) {
        std::string name = line.substr(10);
        auto* table = catalog.GetTable(name); auto* heap = catalog.GetHeapFile(name);
        if (!table || !heap) { std::cout << "Error: Table '" << name << "' not found\n"; return true; }
        auto store = ColumnConverter::Convert(heap, name, table->schema);
        std::cout << "Columnar copy: " << store->GetRowCount() << " rows, "
                  << table->schema.GetColumnCount() << " contiguous columns\n";
        return true;
    }
    if (line.rfind("EXPLAIN ", 0) == 0 || line.rfind("explain ", 0) == 0) {
        Lexer lexer(line.substr(8)); Parser parser(lexer.Tokenize()); Status status;
        auto ast = parser.Parse(status);
        if (status.ok()) status = binder.Bind(ast);
        if (!status.ok()) std::cout << "Explain Error: " << status.message() << '\n';
        else std::cout << PlanPrinter::Print(optimizer.Optimize(ast));
        return true;
    }
    return false;
}
}  // namespace minidb
