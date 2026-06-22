# MiniDB Architecture Documentation

## Module Dependency Graph

```
main.cpp (REPL)
    ├── sql/lexer        → Tokenizes SQL input
    ├── sql/parser       → Produces AST from tokens
    ├── sql/binder       → Resolves names via catalog
    ├── optimizer        → Produces physical plan
    │   ├── cost_model   → Selectivity + cost estimation
    │   └── stats        → Table statistics
    ├── executor_factory → Builds executor tree from plan
    │   ├── seq_scan     → Reads from heap file
    │   ├── index_scan   → Reads via B+ tree
    │   ├── filter       → Evaluates WHERE predicates
    │   ├── projection   → Column selection
    │   ├── nlj          → Nested loop join
    │   ├── insert       → Writes to heap + index
    │   └── delete       → Removes from heap + index
    ├── catalog          → Table registry + schemas
    │   └── heap_file    → Record storage
    ├── index_manager    → B+ tree lifecycle
    │   └── b_plus_tree  → Index operations
    ├── txn_manager      → Transaction lifecycle
    │   ├── lock_manager → S2PL lock table
    │   └── deadlock_det → Wait-for graph DFS
    └── recovery_manager → WAL analysis, redo, and undo
        └── log_manager  → WAL file I/O
```

## Design Decisions

### 1. Slotted Pages vs Fixed-Length Records
We chose **slotted pages** to support variable-length records (VARCHAR). The slot directory enables O(1) record access by slot number, and deleted slots can be reused.

### 2. Hand-Written Parser vs Parser Generator
A **hand-written recursive descent parser** was chosen for simplicity, transparency, and ease of debugging. The grammar is small enough that a parser generator (flex/bison) would add unnecessary complexity.

### 3. Volcano Model vs Vectorized Execution
The **Volcano (iterator) model** was chosen for the core engine for its simplicity and composability. The columnar extension provides batch/vectorized processing for analytical queries.

### 4. LRU vs Clock Replacement
**LRU** was chosen for the buffer pool replacement policy. While Clock is more efficient for real systems, LRU is simpler to implement correctly and easier to explain during viva.

### 5. Table-Level Strict 2PL
The SQL path uses one reserved RID per table as a table lock. Scans acquire shared locks and
INSERT/DELETE acquire exclusive locks until commit. This is deliberately conservative: it makes
serializability and phantom prevention straightforward to demonstrate, at the cost of lower
same-table write concurrency.

### 6. Recovery Boundary
The catalog is persisted, heap pages are the durable source of truth, and primary indexes are
rebuilt at startup. WAL records describe logical primary-key INSERT/DELETE changes, so redo and
undo do not depend on a record retaining the same physical RID.

### 7. Columnar Extension Design
The columnar store is **additive** — it doesn't replace the core row-store but provides an alternative scan path. This design:
- Preserves all core functionality
- Enables direct A/B comparison for benchmarking
- Demonstrates the performance trade-offs between row and column storage
