# MiniDB — A Mini Relational Database Engine

A fully functional relational database system built from scratch in **C++17**, implementing core database internals including storage management, B+ tree indexing, SQL query execution, cost-based optimization, transaction management with 2PL, WAL-based crash recovery, and a columnar storage extension for analytical query performance.

**Extension Track: A — Performance (Columnar Storage Layer)**

## Team Information

**Team Name:** `<TEAM_NAME>`

### Team Members

| Name | Roll Number | Email |
|------|-------------|-------|
| `<Name>` | `<Roll Number>` | `<Email>` |
| `<Name>` | `<Roll Number>` | `<Email>` |
| `<Name>` | `<Roll Number>` | `<Email>` |

---

## 1. Project Overview

### Problem Statement
Build a working relational database engine from foundational components, integrating storage, indexing, query processing, transactions, and recovery into a coherent system.

### Goals
- Implement all core database components (storage, indexing, SQL, optimization, transactions, recovery)
- Demonstrate understanding of database internals through clean architecture
- Build a columnar storage extension (Track A) for analytical query performance
- Benchmark row-store vs columnar performance

### Chosen Extension Track
**Track A — Performance**: Columnar storage layer that stores data column-by-column for cache-friendly analytical queries. Provides significant speedup for projection, aggregation, and filter operations compared to the row-store baseline.

---

## 2. System Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                     SQL REPL (CLI)                           │
├─────────────────────────────────────────────────────────────┤
│  Lexer → Parser → Binder → Optimizer → Executor Factory     │
├──────────────┬──────────────┬───────────────────────────────┤
│  Execution   │  Transaction │  Recovery                     │
│  Engine      │  Manager     │  Manager                      │
│  (Volcano)   │  (S2PL)      │  (WAL Redo/Undo)             │
├──────────────┼──────────────┼───────────────────────────────┤
│  B+ Tree     │  Lock        │  Log                          │
│  Index       │  Manager     │  Manager                      │
├──────────────┴──────────────┴───────────────────────────────┤
│              Buffer Pool (LRU Replacement)                   │
├─────────────────────────────────────────────────────────────┤
│              Page Manager (Disk I/O)                         │
├─────────────────────────────────────────────────────────────┤
│              Heap Files (Slotted Pages)                      │
├─────────────────────────────────────────────────────────────┤
│  Extension: Columnar Store │ Columnar Scan │ Converter      │
└─────────────────────────────────────────────────────────────┘
```

### Major Modules

| Module | Description |
|--------|-------------|
| **Storage Engine** | Page-based heap files with slotted pages, page manager for disk I/O, buffer pool with LRU |
| **B+ Tree Index** | Primary key index with insert (node splitting), delete, point lookup, and range scan |
| **SQL Frontend** | Hand-written recursive descent parser, lexer, and semantic binder |
| **Cost-Based Optimizer** | Selectivity estimation, join order selection (DP), access path selection (seq vs index scan) |
| **Execution Engine** | Volcano-style iterator model with SeqScan, IndexScan, Filter, Projection, NLJoin, Insert, Delete |
| **Transaction Manager** | Strict Two-Phase Locking (S2PL), deadlock detection via wait-for graph |
| **Recovery Manager** | WAL with analysis, committed redo, and loser undo |
| **Columnar Extension** | Column-oriented storage, columnar scan, row-to-column converter |

---

## 3. Storage Layer

### Page Format
- **Slotted pages** (4KB each) with header, slot directory, and record area
- Header: page_id, slot_count, free_space pointers, LSN, next_page_id
- Slot directory grows downward; records grow upward from page end
- Deleted slots are reused for new insertions

### Heap Files
- Linked list of pages with free-space tracking
- Insert finds first page with enough space or allocates new
- Full table scan iterates through the linked list

### Buffer Pool
- Fixed-size pool (1024 frames default) with LRU replacement
- Pin counting prevents eviction of in-use pages
- Dirty page tracking with flush-on-eviction

---

## 4. Indexing

### B+ Tree Design
- Integer keys (primary key) mapped to RIDs (page_id, slot_num)
- Leaf nodes: sorted keys + RIDs + next-leaf pointer for range scans
- Internal nodes: separator keys + child page pointers

### Node Structure
- Maximum keys per node determined by page size
- Leaf: `[header][keys...][RIDs...][next_leaf]`
- Internal: `[header][keys...][children...]`

### Operations
- **Search**: Traverse from root to leaf using binary search at each level
- **Insert**: Find leaf, insert in sorted order, split if full (propagate up)
- **Delete**: Find and remove key (lazy — no merge/redistribute)
- **Range Scan**: Find start leaf, follow next-leaf pointers

---

## 5. Query Execution

### Parser
- Hand-written recursive descent parser
- Supports: `CREATE TABLE`, `INSERT`, `SELECT` (with `JOIN`, `WHERE`), `DELETE`
- Expression parser with precedence: OR < AND < comparison < primary

### Query Plan Generation
- Optimizer produces physical plan tree from bound AST
- Plan nodes: SEQ_SCAN, INDEX_SCAN, FILTER, PROJECTION, NESTED_LOOP_JOIN, INSERT, DELETE

### Operator Execution (Volcano Model)
```
Open()  → Initialize state
Next()  → Return next tuple (pull-based)
Close() → Release resources
```

---

## 6. Optimizer

### Cost Estimation
- Sequential scan cost = number of pages
- Index scan cost = tree height + selectivity × pages
- Nested loop join cost = outer_pages + outer_rows × inner_cost

### Selectivity Estimation
- Equality: `1 / distinct_values`
- Range: `1/3` (uniform distribution assumption)
- AND: `sel(A) × sel(B)`
- OR: `sel(A) + sel(B) - sel(A) × sel(B)`

### Join Ordering
- For 2 tables: estimate both nested-loop orders and choose the cheaper outer table
- For 3+ tables: retain SQL order in a left-deep plan so each `ON` predicate remains correct

---

## 7. Transactions & Concurrency

### Locking Strategy
- **Strict Two-Phase Locking (S2PL)**: locks are held until COMMIT/ABORT
- Lock modes: SHARED (for reads) and EXCLUSIVE (for writes)
- Lock granularity: table-level, represented by a reserved RID

### Isolation Guarantees
- **Serializable isolation**: table S/X locks make conflicting schedules serializable
- Table locks also prevent phantoms; the deliberate trade-off is lower write concurrency

### Deadlock Handling
- **Wait-for graph** constructed from lock table
- Periodic DFS cycle detection
- Victim selection: abort the youngest transaction in the cycle

---

## 8. Recovery

### WAL Design
- Log records: BEGIN, INSERT, DELETE, UPDATE, COMMIT, ABORT, CLR, CHECKPOINT
- Each record contains: LSN, txn_id, prev_lsn, table, RID, before/after images
- Force-flush on COMMIT (write-ahead logging protocol)

### Log Records
- Serialized with length-prefix for variable-size records
- Before-image stored for DELETE (for undo)
- After-image stored for INSERT (for redo)

### Crash Recovery
1. **Analysis**: scan WAL and identify committed and active transactions
2. **Redo**: idempotently replay committed logical changes using primary keys
3. **Undo**: reverse active transaction changes
4. Flush recovered pages and catalog, then clear the WAL checkpoint

Catalog metadata is persisted separately. Primary indexes are rebuilt from heap data at startup,
which keeps recovery simple and avoids logging B+ tree structural changes.

---

## 9. Extension Track A — Columnar Storage

### Motivation
Row-store (heap file) stores all columns of a record together — efficient for full-row access but wasteful for analytical queries that only need a few columns. Columnar storage stores each column separately, enabling:
- **Better cache utilization** for column scans
- **Reduced I/O** when projecting few columns
- **Faster aggregations** operating on contiguous arrays

### Design
- `ColumnarStore`: Stores each column as a contiguous typed array (int[], double[], string[])
- `ColumnarScanExecutor`: Volcano-style iterator reading from columnar storage
- `ColumnConverter`: Converts row-store data to columnar format
- Late materialization: filter on columns first, materialize full rows only for matches

### Results
The checked-in Release benchmark shows the expected trade-off: full-row scans are similar,
while projection, aggregation, and filtering benefit from contiguous typed arrays. The extension
can be demonstrated interactively with `\columnar <table>`.

---

## 10. Benchmarks

### Experimental Setup
- Language/toolchain: C++17, MSVC 19.44 Release (`/O2`), Windows
- Page size: 4KB, Buffer pool: 256-1024 frames
- Dataset: Synthetic table with (id INT, name VARCHAR, age INT)
- Timings exclude dataset generation and column conversion

### Key Results

| Operation (100K rows) | Row-store | Columnar | Speedup |
|-----------|-----------|----------|---------|
| Full scan | 12.703 ms | 10.131 ms | 1.25x |
| Projection | 12.871 ms | 0.043 ms | 300.73x |
| SUM aggregation | 12.750 ms | 0.249 ms | 51.19x |
| Filter | 14.336 ms | 1.168 ms | 12.27x |

Raw results are in `benchmarks/results.csv`; methodology and analysis are in
`docs/benchmark_report.md`.

---

## 11. Limitations

### Missing Features
- No UPDATE statement (only INSERT/DELETE)
- No GROUP BY / ORDER BY / HAVING
- No multi-column indexes
- B+ Tree delete doesn't merge/redistribute underflowing nodes

### Scalability Limits
- Single-threaded REPL; lock/recovery components are thread-safe and concurrency-tested
- Table locking favors clarity and serializability over record-level write concurrency
- Nested loop join only (no hash/sort-merge join)

### Future Improvements
- Hash join and sort-merge join operators
- Catalog transactions for DDL changes
- Multi-column and composite indexes
- UPDATE statement support
- Query plan caching

---

## 12. How to Run

### Dependencies
- C++17 compiler (GCC 9+, Clang 10+, or MSVC 2019+)
- CMake 3.16+
- GoogleTest (fetched automatically via CMake FetchContent)

### Build Steps

```bash
# Clone and navigate to project directory
cd project

# Configure and build (works with MSVC, GCC, or Clang)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j

# Test
ctest --test-dir build -C Release --output-on-failure

# Windows executables
.\build\src\Release\minidb.exe
.\build\benchmarks\Release\minidb_bench.exe

# GCC/Clang single-config executables
./build/src/minidb
./build/benchmarks/minidb_bench
```

### Example Commands

```sql
-- Create a table
CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR(255), age INT);

-- Insert data
INSERT INTO users VALUES (1, 'Alice', 30);
INSERT INTO users VALUES (2, 'Bob', 25);
INSERT INTO users VALUES (3, 'Charlie', 35);

-- Query data
SELECT * FROM users;
SELECT * FROM users WHERE age > 28;

-- Join query
CREATE TABLE orders (id INT PRIMARY KEY, user_id INT, amount INT);
INSERT INTO orders VALUES (1, 1, 100);
INSERT INTO orders VALUES (2, 2, 200);
SELECT users.name, orders.amount FROM users JOIN orders ON users.id = orders.user_id;

-- Delete data
DELETE FROM users WHERE id = 1;

-- System commands
\tables
\schema users
exit
```
