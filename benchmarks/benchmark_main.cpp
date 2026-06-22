#include <iostream>
#include <chrono>
#include <vector>
#include <random>
#include <iomanip>
#include <fstream>
#include <filesystem>

#include "common/config.h"
#include "storage/page_manager.h"
#include "storage/buffer_pool.h"
#include "storage/heap_file.h"
#include "catalog/schema.h"
#include "index/b_plus_tree.h"
#include "extension/columnar_store.h"
#include "extension/col_converter.h"

using namespace minidb;
using Clock = std::chrono::high_resolution_clock;

// ============================================================
// Benchmark Utilities
// ============================================================

struct BenchResult {
    std::string name;
    double time_ms;
    double ops_per_sec;
    size_t row_count;
};

void PrintBenchResult(const BenchResult& r) {
    std::cout << std::left << std::setw(50) << r.name
              << std::right << std::setw(12) << std::fixed << std::setprecision(3) << r.time_ms << " ms"
              << std::setw(15) << std::fixed << std::setprecision(0) << r.ops_per_sec << " ops/s"
              << std::setw(10) << r.row_count << " rows\n";
}

// ============================================================
// Storage Benchmarks
// ============================================================

BenchResult BenchPageInsert(int num_records) {
    std::filesystem::create_directories("bench_data");
    PageManager pm("bench_data/bench_page.db");
    BufferPool bp(&pm, 256);
    HeapFile heap = HeapFile::Create(&bp);

    auto start = Clock::now();
    for (int i = 0; i < num_records; i++) {
        int32_t val = i;
        std::string name = "user_" + std::to_string(i);
        int32_t age = 20 + (i % 60);

        std::string record;
        record.append(reinterpret_cast<const char*>(&val), sizeof(val));
        uint16_t name_len = static_cast<uint16_t>(name.size());
        record.append(reinterpret_cast<const char*>(&name_len), sizeof(name_len));
        record.append(name);
        record.append(reinterpret_cast<const char*>(&age), sizeof(age));

        RID rid;
        heap.InsertRecord(record.data(), static_cast<uint16_t>(record.size()), rid);
    }
    bp.FlushAllPages();
    auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();

    return {"Heap File Insert", elapsed, num_records / (elapsed / 1000.0), static_cast<size_t>(num_records)};
}

BenchResult BenchHeapScan(int num_records) {
    std::filesystem::create_directories("bench_data");
    PageManager pm("bench_data/bench_scan.db");
    BufferPool bp(&pm, 256);
    HeapFile heap = HeapFile::Create(&bp);

    // Insert records
    for (int i = 0; i < num_records; i++) {
        int32_t val = i;
        std::string record;
        record.append(reinterpret_cast<const char*>(&val), sizeof(val));
        uint16_t slen = 5;
        record.append(reinterpret_cast<const char*>(&slen), sizeof(slen));
        record.append("hello");
        record.append(reinterpret_cast<const char*>(&val), sizeof(val));
        RID rid;
        heap.InsertRecord(record.data(), static_cast<uint16_t>(record.size()), rid);
    }
    bp.FlushAllPages();

    // Benchmark scan
    auto start = Clock::now();
    int count = 0;
    heap.Scan([&](const RID&, const char*, uint16_t) -> bool {
        count++;
        return true;
    });
    auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();

    return {"Heap File Sequential Scan", elapsed, count / (elapsed / 1000.0), static_cast<size_t>(count)};
}

// ============================================================
// B+ Tree Benchmarks
// ============================================================

BenchResult BenchBTreeInsert(int num_keys) {
    std::filesystem::create_directories("bench_data");
    PageManager pm("bench_data/bench_btree.db");
    BufferPool bp(&pm, 1024);
    BPlusTree tree(&bp);
    tree.Create();

    auto start = Clock::now();
    for (int i = 0; i < num_keys; i++) {
        tree.Insert(i, RID(i / 100, i % 100));
    }
    auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();

    return {"B+ Tree Insert", elapsed, num_keys / (elapsed / 1000.0), static_cast<size_t>(num_keys)};
}

BenchResult BenchBTreeSearch(int num_keys) {
    std::filesystem::create_directories("bench_data");
    PageManager pm("bench_data/bench_btree2.db");
    BufferPool bp(&pm, 1024);
    BPlusTree tree(&bp);
    tree.Create();

    for (int i = 0; i < num_keys; i++) {
        tree.Insert(i, RID(i / 100, i % 100));
    }

    auto start = Clock::now();
    int found = 0;
    for (int i = 0; i < num_keys; i++) {
        RID rid;
        if (tree.Search(i, rid).ok()) found++;
    }
    auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();

    return {"B+ Tree Point Lookup", elapsed, found / (elapsed / 1000.0), static_cast<size_t>(found)};
}

// ============================================================
// Row-Store vs Columnar Benchmarks (Extension Track A)
// ============================================================

void GenerateTestData(HeapFile& heap, const Schema& schema, int num_rows) {
    std::mt19937 rng(42);
    for (int i = 0; i < num_rows; i++) {
        Tuple tuple = {
            int32_t(i),                              // id
            std::string("user_" + std::to_string(i)), // name
            int32_t(20 + rng() % 60),                // age
        };
        std::string data = schema.SerializeTuple(tuple);
        RID rid;
        heap.InsertRecord(data.data(), static_cast<uint16_t>(data.size()), rid);
    }
}

BenchResult BenchRowStoreScan(HeapFile& heap, const Schema& schema, int num_rows) {
    auto start = Clock::now();
    int count = 0;
    heap.Scan([&](const RID&, const char* data, uint16_t len) -> bool {
        Tuple tuple = schema.DeserializeTuple(data, len);
        count++;
        return true;
    });
    auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    return {"Row-Store Full Scan (SELECT *)", elapsed, count / (elapsed / 1000.0), static_cast<size_t>(count)};
}

BenchResult BenchColumnarScan(ColumnarStore& col_store, int num_rows) {
    auto start = Clock::now();
    int count = 0;
    for (size_t i = 0; i < col_store.GetRowCount(); i++) {
        Tuple tuple = col_store.MaterializeRow(i);
        count++;
    }
    auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    return {"Columnar Full Scan (SELECT *)", elapsed, count / (elapsed / 1000.0), static_cast<size_t>(count)};
}

BenchResult BenchRowStoreProjection(HeapFile& heap, const Schema& schema, int num_rows) {
    auto start = Clock::now();
    int count = 0;
    heap.Scan([&](const RID&, const char* data, uint16_t len) -> bool {
        Tuple tuple = schema.DeserializeTuple(data, len);
        // Access only column 2 (age)
        int32_t age = std::get<int32_t>(tuple[2]);
        (void)age;
        count++;
        return true;
    });
    auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    return {"Row-Store Projection (SELECT age)", elapsed, count / (elapsed / 1000.0), static_cast<size_t>(count)};
}

BenchResult BenchColumnarProjection(ColumnarStore& col_store, int num_rows) {
    auto start = Clock::now();
    const auto& col = col_store.ScanColumn(2); // age column
    volatile int64_t checksum = 0; // consume values so the compiler cannot remove the scan
    for (int32_t value : col.int_data) checksum += value;
    (void)checksum;
    int count = static_cast<int>(col.int_data.size());
    auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    return {"Columnar Projection (SELECT age)", elapsed, count / (elapsed / 1000.0), static_cast<size_t>(count)};
}

BenchResult BenchRowStoreAggregation(HeapFile& heap, const Schema& schema, int num_rows) {
    auto start = Clock::now();
    int64_t sum = 0;
    int count = 0;
    heap.Scan([&](const RID&, const char* data, uint16_t len) -> bool {
        Tuple tuple = schema.DeserializeTuple(data, len);
        sum += std::get<int32_t>(tuple[2]); // SUM(age)
        count++;
        return true;
    });
    auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    return {"Row-Store Aggregation (SUM(age))", elapsed, count / (elapsed / 1000.0), static_cast<size_t>(count)};
}

BenchResult BenchColumnarAggregation(ColumnarStore& col_store, int num_rows) {
    auto start = Clock::now();
    int64_t sum = col_store.SumInt(2); // SUM(age)
    (void)sum;
    auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    return {"Columnar Aggregation (SUM(age))", elapsed, col_store.GetRowCount() / (elapsed / 1000.0), col_store.GetRowCount()};
}

BenchResult BenchRowStoreFilter(HeapFile& heap, const Schema& schema, int num_rows) {
    auto start = Clock::now();
    int count = 0;
    heap.Scan([&](const RID&, const char* data, uint16_t len) -> bool {
        Tuple tuple = schema.DeserializeTuple(data, len);
        if (std::get<int32_t>(tuple[2]) > 50) count++; // WHERE age > 50
        return true;
    });
    auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    return {"Row-Store Filter (WHERE age > 50)", elapsed, num_rows / (elapsed / 1000.0), static_cast<size_t>(count)};
}

BenchResult BenchColumnarFilter(ColumnarStore& col_store, int num_rows) {
    auto start = Clock::now();
    auto matches = col_store.FilterColumn(2, ">", Value(int32_t(50)));
    auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    return {"Columnar Filter (WHERE age > 50)", elapsed, num_rows / (elapsed / 1000.0), matches.size()};
}

// ============================================================
// Main
// ============================================================

int main() {
    std::filesystem::remove_all("bench_data");
    std::cout << "╔══════════════════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║                        MiniDB Benchmark Suite                               ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════════════════════╝\n\n";

    std::vector<BenchResult> all_results;

    // Storage benchmarks
    std::cout << "=== Storage Engine Benchmarks ===\n";
    for (int n : {1000, 10000, 100000}) {
        auto r = BenchPageInsert(n);
        PrintBenchResult(r);
        all_results.push_back(r);
    }
    for (int n : {1000, 10000, 100000}) {
        auto r = BenchHeapScan(n);
        PrintBenchResult(r);
        all_results.push_back(r);
    }

    // B+ Tree benchmarks
    std::cout << "\n=== B+ Tree Index Benchmarks ===\n";
    for (int n : {1000, 10000, 50000}) {
        auto r = BenchBTreeInsert(n);
        PrintBenchResult(r);
        all_results.push_back(r);

        r = BenchBTreeSearch(n);
        PrintBenchResult(r);
        all_results.push_back(r);
    }

    // Row-Store vs Columnar benchmarks
    std::cout << "\n=== Extension Track A: Row-Store vs Columnar ===\n";
    for (int n : {10000, 100000}) {
        std::cout << "\n--- " << n << " rows ---\n";

        std::filesystem::create_directories("bench_data");
        PageManager pm("bench_data/bench_col.db");
        BufferPool bp(&pm, 512);
        HeapFile heap = HeapFile::Create(&bp);

        Schema schema({
            Column("id", ColumnType::INT, 0, true),
            Column("name", ColumnType::VARCHAR, 255),
            Column("age", ColumnType::INT),
        });

        GenerateTestData(heap, schema, n);
        bp.FlushAllPages();

        // Convert to columnar
        auto col_store = ColumnConverter::Convert(&heap, "bench_table", schema);

        // Run comparisons
        auto r1 = BenchRowStoreScan(heap, schema, n);
        auto r2 = BenchColumnarScan(*col_store, n);
        PrintBenchResult(r1); PrintBenchResult(r2);
        std::cout << "  -> Columnar speedup: " << std::fixed << std::setprecision(2)
                  << r1.time_ms / r2.time_ms << "x\n\n";
        all_results.push_back(r1); all_results.push_back(r2);

        r1 = BenchRowStoreProjection(heap, schema, n);
        r2 = BenchColumnarProjection(*col_store, n);
        PrintBenchResult(r1); PrintBenchResult(r2);
        std::cout << "  -> Columnar speedup: " << std::fixed << std::setprecision(2)
                  << r1.time_ms / std::max(r2.time_ms, 0.001) << "x\n\n";
        all_results.push_back(r1); all_results.push_back(r2);

        r1 = BenchRowStoreAggregation(heap, schema, n);
        r2 = BenchColumnarAggregation(*col_store, n);
        PrintBenchResult(r1); PrintBenchResult(r2);
        std::cout << "  -> Columnar speedup: " << std::fixed << std::setprecision(2)
                  << r1.time_ms / std::max(r2.time_ms, 0.001) << "x\n\n";
        all_results.push_back(r1); all_results.push_back(r2);

        r1 = BenchRowStoreFilter(heap, schema, n);
        r2 = BenchColumnarFilter(*col_store, n);
        PrintBenchResult(r1); PrintBenchResult(r2);
        std::cout << "  -> Columnar speedup: " << std::fixed << std::setprecision(2)
                  << r1.time_ms / std::max(r2.time_ms, 0.001) << "x\n\n";
        all_results.push_back(r1); all_results.push_back(r2);

    }

    // Summary
    std::cout << "\n=== Benchmark Summary ===\n";
    std::cout << std::left << std::setw(50) << "Benchmark"
              << std::right << std::setw(15) << "Time (ms)"
              << std::setw(18) << "Throughput" << std::setw(10) << "Rows" << "\n";
    std::cout << std::string(93, '-') << "\n";
    for (const auto& r : all_results) {
        PrintBenchResult(r);
    }

    std::filesystem::create_directories("benchmarks");
    std::ofstream csv("benchmarks/results.csv");
    csv << "benchmark,time_ms,ops_per_sec,rows\n";
    for (const auto& r : all_results)
        csv << '"' << r.name << "\"," << r.time_ms << ',' << r.ops_per_sec << ',' << r.row_count << '\n';
    csv.close();
    std::filesystem::remove_all("bench_data");

    return 0;
}
