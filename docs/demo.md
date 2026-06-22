# MiniDB Demonstration Checklist

## Core SQL and Index Selection

```sql
CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR(50), age INT);
INSERT INTO users VALUES (1, 'Alice', 30), (2, 'Bob', 25);
SELECT * FROM users WHERE age > 26;
EXPLAIN SELECT * FROM users WHERE id = 1;
DELETE FROM users WHERE id = 2;
```

For a small table the cost model may show a sequential scan. Insert at least 100 rows, rerun
`EXPLAIN`, and it will select `INDEX_SCAN` when its estimated cost is lower.

## Persistence and Recovery

1. Create a table and insert committed rows.
2. Close MiniDB normally and restart; the catalog and rows remain.
3. For the automated crash cases run:

```powershell
ctest --test-dir build -C Release -R RecoveryTest --output-on-failure
```

`RedoesCommittedInsert` simulates a durable committed log whose data page was not written.
`UndoesUncommittedInsert` simulates a dirty uncommitted page. Both verify heap and index state.

## Serializable 2PL and Deadlock

```powershell
ctest --test-dir build -C Release -R TxnTest --output-on-failure
```

The real-deadlock test makes two transactions lock resources in opposite order, detects the
wait-for cycle, aborts the youngest transaction, and allows the survivor to finish.

## Columnar Extension and Benchmarks

```text
\columnar users
```

Then run `minidb_bench` and show `benchmarks/results.csv` together with
`docs/benchmark_report.md`.
