#include <iostream>
#include <string>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <chrono>

#include "common/config.h"
#include "common/types.h"
#include "storage/page_manager.h"
#include "storage/buffer_pool.h"
#include "storage/heap_file.h"
#include "catalog/catalog.h"
#include "index/index_manager.h"
#include "sql/lexer.h"
#include "sql/parser.h"
#include "sql/binder.h"
#include "optimizer/optimizer.h"
#include "optimizer/stats.h"
#include "app/system_commands.h"
#include "execution/executor_factory.h"
#include "txn/lock_manager.h"
#include "txn/txn_manager.h"
#include "txn/deadlock_detector.h"
#include "recovery/log_manager.h"
#include "recovery/recovery_manager.h"

using namespace minidb;

// Helper to print a value
std::string ValueToString(const Value& val) {
    if (std::holds_alternative<int32_t>(val)) return std::to_string(std::get<int32_t>(val));
    if (std::holds_alternative<double>(val)) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2) << std::get<double>(val);
        return ss.str();
    }
    if (std::holds_alternative<std::string>(val)) return std::get<std::string>(val);
    if (std::holds_alternative<bool>(val)) return std::get<bool>(val) ? "true" : "false";
    return "NULL";
}

// Print results as a formatted table
void PrintResults(const std::vector<std::string>& headers, const std::vector<Tuple>& rows) {
    if (headers.empty()) return;

    // Calculate column widths
    std::vector<size_t> widths(headers.size());
    for (size_t i = 0; i < headers.size(); i++) {
        widths[i] = headers[i].size();
    }
    for (const auto& row : rows) {
        for (size_t i = 0; i < row.size() && i < widths.size(); i++) {
            widths[i] = std::max(widths[i], ValueToString(row[i]).size());
        }
    }

    // Print separator
    auto printSep = [&]() {
        std::cout << "+";
        for (size_t w : widths) {
            std::cout << std::string(w + 2, '-') << "+";
        }
        std::cout << "\n";
    };

    // Print header
    printSep();
    std::cout << "|";
    for (size_t i = 0; i < headers.size(); i++) {
        std::cout << " " << std::left << std::setw(widths[i]) << headers[i] << " |";
    }
    std::cout << "\n";
    printSep();

    // Print rows
    for (const auto& row : rows) {
        std::cout << "|";
        for (size_t i = 0; i < row.size() && i < widths.size(); i++) {
            std::cout << " " << std::left << std::setw(widths[i]) << ValueToString(row[i]) << " |";
        }
        std::cout << "\n";
    }
    printSep();

    std::cout << rows.size() << " row(s) returned\n";
}

int main(int argc, char* argv[]) {
    std::cout << "╔══════════════════════════════════════════════╗\n";
    std::cout << "║            MiniDB v1.0.0                    ║\n";
    std::cout << "║   A Mini Relational Database Engine         ║\n";
    std::cout << "║   Extension Track A: Columnar Storage       ║\n";
    std::cout << "╚══════════════════════════════════════════════╝\n";
    std::cout << "\n";

    // Create data directory
    std::filesystem::create_directories(DATA_DIR);

    // Initialize storage
    std::string db_file = std::string(DATA_DIR) + "/minidb.db";
    PageManager page_manager(db_file);
    BufferPool buffer_pool(&page_manager);

    // Initialize catalog and indexes
    Catalog catalog(&buffer_pool, CATALOG_FILE);
    IndexManager index_mgr(&buffer_pool);
    for (const auto& name : catalog.GetTableNames()) {
        TableInfo* info = catalog.GetTable(name);
        if (info && info->index_root_page_id != INVALID_PAGE_ID) {
            index_mgr.CreateIndex(name, "");
            auto* index = index_mgr.GetIndex(name, "");
            auto* heap = catalog.GetHeapFile(name);
            if (index && heap) heap->Scan([&](const RID& rid, const char* data, uint16_t length) {
                auto tuple = info->schema.DeserializeTuple(data, length);
                index->Insert(info->schema.GetPrimaryKey(tuple), rid);
                return true;
            });
            if (index) catalog.SetIndexRoot(name, index->GetRootPageId());
        }
    }
    StatsManager stats_mgr;

    // Initialize transaction subsystem
    LockManager lock_mgr;
    TxnManager txn_mgr(&lock_mgr, &catalog);
    DeadlockDetector deadlock_detector(&lock_mgr);

    // Initialize recovery
    LogManager log_mgr(WAL_FILE);
    RecoveryManager recovery_mgr(&log_mgr, &buffer_pool, &catalog, &index_mgr);

    // Check for crash recovery
    if (recovery_mgr.NeedsRecovery()) {
        std::cout << "[System] Performing crash recovery...\n";
        recovery_mgr.Recover();
    }

    // Initialize optimizer and executor factory
    Optimizer optimizer(&catalog, &index_mgr, &stats_mgr);
    ExecutorFactory exec_factory(&catalog, &index_mgr, &stats_mgr);

    for (const auto& name : catalog.GetTableNames()) {
        TableStats stats;
        if (auto* heap = catalog.GetHeapFile(name)) {
            stats.page_count = heap->GetPageCount();
            TableInfo* info = catalog.GetTable(name);
            heap->Scan([&](const RID&, const char* data, uint16_t length) {
                auto tuple = info->schema.DeserializeTuple(data, length);
                stats.UpdateOnInsert(info->schema.GetPrimaryKey(tuple));
                return true;
            });
        }
        stats_mgr.SetStats(name, stats);
    }

    // Binder
    Binder binder(&catalog);

    std::cout << "Type SQL commands or 'help' for usage. Use 'exit' to quit.\n\n";

    // REPL loop
    std::string line;
    while (true) {
        std::cout << "minidb> ";
        if (!std::getline(std::cin, line)) break;

        // Trim whitespace
        size_t start = line.find_first_not_of(" \t\n\r");
        if (start == std::string::npos) continue;
        line = line.substr(start);
        size_t end = line.find_last_not_of(" \t\n\r;");
        if (end != std::string::npos) line = line.substr(0, end + 1);

        if (line.empty()) continue;

        if (line == "exit" || line == "quit" || line == "\\q") {
            std::cout << "Goodbye!\n";
            break;
        }

        if (SystemCommands::TryExecute(line, catalog, binder, optimizer)) continue;

        // Parse SQL
        auto start_time = std::chrono::high_resolution_clock::now();

        Lexer lexer(line);
        auto tokens = lexer.Tokenize();

        Parser parser(tokens);
        Status status;
        auto ast = parser.Parse(status);

        if (!status.ok()) {
            std::cout << "Parse Error: " << status.message() << "\n";
            continue;
        }

        // Handle CREATE TABLE specially (before binding)
        if (ast->type == ASTNodeType::CREATE_TABLE) {
            auto& stmt = std::get<CreateTableStmt>(ast->stmt);

            status = binder.Bind(ast);
            if (!status.ok()) {
                std::cout << "Bind Error: " << status.message() << "\n";
                continue;
            }

            // Check for duplicates
            if (catalog.TableExists(stmt.table_name)) {
                std::cout << "Error: Table '" << stmt.table_name << "' already exists\n";
                continue;
            }

            // Build schema
            std::vector<Column> columns;
            for (const auto& def : stmt.columns) {
                columns.push_back(Column(def.name, def.type, def.max_length, def.is_primary_key));
            }
            Schema schema(columns);

            // Create table
            Status cs = catalog.CreateTable(stmt.table_name, schema);
            if (!cs.ok()) {
                std::cout << "Error: " << cs.message() << "\n";
                continue;
            }

            // Create primary key index
            int pk_idx = schema.GetPrimaryKeyIndex();
            if (pk_idx >= 0) {
                index_mgr.CreateIndex(stmt.table_name, "");
                if (auto* index = index_mgr.GetIndex(stmt.table_name, ""))
                    catalog.SetIndexRoot(stmt.table_name, index->GetRootPageId());
            }

            // Initialize stats
            TableStats ts;
            stats_mgr.SetStats(stmt.table_name, ts);
            buffer_pool.FlushAllPages();

            auto elapsed = std::chrono::high_resolution_clock::now() - start_time;
            auto ms = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
            std::cout << "Table '" << stmt.table_name << "' created (" << ms << " μs)\n";
            continue;
        }

        // Bind
        status = binder.Bind(ast);
        if (!status.ok()) {
            std::cout << "Bind Error: " << status.message() << "\n";
            continue;
        }

        // Every SQL statement is an auto-commit strict-2PL transaction.
        Transaction* txn = txn_mgr.Begin();
        recovery_mgr.LogBegin(txn->GetTxnId());
        exec_factory.SetContext({txn, &lock_mgr, &recovery_mgr});

        // Optimize
        auto plan = optimizer.Optimize(ast);
        if (!plan) {
            recovery_mgr.LogAbort(txn->GetTxnId());
            txn_mgr.Abort(txn);
            std::cout << "Error: Failed to create execution plan\n";
            continue;
        }

        // Build executor
        auto executor = exec_factory.Build(plan);
        if (!executor) {
            recovery_mgr.LogAbort(txn->GetTxnId());
            txn_mgr.Abort(txn);
            std::cout << "Error: Failed to build executor\n";
            continue;
        }

        // Execute
        executor->Open();

        if (!executor->GetStatus().ok()) {
            std::string table_name;
            if (ast->type == ASTNodeType::INSERT) table_name = std::get<InsertStmt>(ast->stmt).table_name;
            if (ast->type == ASTNodeType::DELETE_STMT) table_name = std::get<DeleteStmt>(ast->stmt).table_name;
            txn_mgr.Abort(txn, catalog.GetHeapFile(table_name), index_mgr.GetIndex(table_name, ""));
            recovery_mgr.LogAbort(txn->GetTxnId());
            std::cout << "Execution Error: " << executor->GetStatus().message() << "\n";
            executor->Close();
            continue;
        }

        if (ast->type == ASTNodeType::SELECT) {
            auto& stmt = std::get<SelectStmt>(ast->stmt);

            // Determine column headers
            std::vector<std::string> headers;
            bool is_star = false;
            for (const auto& expr : stmt.select_list) {
                if (dynamic_cast<StarExpr*>(expr.get())) {
                    is_star = true;
                    break;
                }
                if (auto* col = dynamic_cast<ColumnRefExpr*>(expr.get())) {
                    if (!col->table_name.empty())
                        headers.push_back(col->table_name + "." + col->column_name);
                    else
                        headers.push_back(col->column_name);
                }
            }

            if (is_star) {
                // Get all column names from tables
                for (const auto& table : stmt.from_tables) {
                    TableInfo* info = catalog.GetTable(table);
                    if (info) {
                        for (size_t i = 0; i < info->schema.GetColumnCount(); i++) {
                            headers.push_back(info->schema.GetColumn(i).name);
                        }
                    }
                }
                for (const auto& join : stmt.joins) {
                    TableInfo* info = catalog.GetTable(join.table_name);
                    if (info) {
                        for (size_t i = 0; i < info->schema.GetColumnCount(); i++) {
                            headers.push_back(info->schema.GetColumn(i).name);
                        }
                    }
                }
            }

            // Collect results
            std::vector<Tuple> rows;
            Tuple tuple;
            RID rid;
            while (executor->Next(tuple, rid)) {
                rows.push_back(tuple);
            }

            PrintResults(headers, rows);
        } else if (ast->type == ASTNodeType::INSERT) {
            Tuple result;
            RID rid;
            executor->Next(result, rid);
            int count = std::get<int32_t>(result[0]);

            // Update catalog row count
            auto& stmt = std::get<InsertStmt>(ast->stmt);
            for (int i = 0; i < count; i++) {
                catalog.IncrementRowCount(stmt.table_name);
            }

            auto elapsed = std::chrono::high_resolution_clock::now() - start_time;
            auto ms = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
            std::cout << count << " row(s) inserted (" << ms << " μs)\n";
        } else if (ast->type == ASTNodeType::DELETE_STMT) {
            Tuple result;
            RID rid;
            executor->Next(result, rid);
            int count = std::get<int32_t>(result[0]);

            auto& stmt = std::get<DeleteStmt>(ast->stmt);
            for (int i = 0; i < count; i++) {
                catalog.DecrementRowCount(stmt.table_name);
            }

            auto elapsed = std::chrono::high_resolution_clock::now() - start_time;
            auto ms = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
            std::cout << count << " row(s) deleted (" << ms << " μs)\n";
        }

        executor->Close();
        recovery_mgr.LogCommit(txn->GetTxnId());
        txn_mgr.Commit(txn);

        // Persist roots because a B+ tree split may replace its root.
        for (const auto& name : catalog.GetTableNames()) {
            if (auto* index = index_mgr.GetIndex(name, ""))
                catalog.SetIndexRoot(name, index->GetRootPageId());
        }
    }

    // Flush everything before exit
    buffer_pool.FlushAllPages();
    log_mgr.Flush();
    log_mgr.Clear();  // Clean-shutdown checkpoint: pages are durable before WAL removal.

    return 0;
}
