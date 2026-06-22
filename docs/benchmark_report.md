# MiniDB Benchmark Report

## Method

The benchmark was compiled in Release mode with MSVC 19.44 on Windows. It uses deterministic
synthetic rows `(id INT, name VARCHAR, age INT)` and datasets of 10,000 and 100,000 rows.
Dataset generation and row-to-column conversion are excluded from scan timings. Every result is
also written to `benchmarks/results.csv` so runs can be compared without copying console output.

Command:

```powershell
cmake --build build --config Release --target minidb_bench
.\build\benchmarks\Release\minidb_bench.exe
```

## Row Store vs Columnar Results (100,000 rows)

| Operation | Row store | Columnar | Speedup |
|---|---:|---:|---:|
| Full row scan | 12.703 ms | 10.131 ms | 1.25x |
| Single-column projection | 12.871 ms | 0.043 ms | 300.73x |
| `SUM(age)` | 12.750 ms | 0.249 ms | 51.19x |
| `age > 50` filter | 14.336 ms | 1.168 ms | 12.27x |

## Analysis

Full-row scans are close because both layouts must materialize every value. Columnar projection,
aggregation, and filtering are faster because the integer ages are contiguous and no names or
unselected values are decoded. The large projection ratio should be read as an in-memory
microbenchmark result, not as a universal database speedup.

For resource utilization, an age-only column scan touches roughly 400 KB for 100,000 32-bit
integers. The row scan must also traverse IDs, string length fields, name bytes, slot metadata,
and page headers—over 2 MB for this dataset. The columnar layout therefore improves both cache
locality and effective memory bandwidth. Conversely, reconstructing complete rows requires
reading every column and gives only a 1.25x improvement.

## Reproducibility and Limits

- Random ages use a fixed seed (`42`).
- Raw measurements are checked into `benchmarks/results.csv`.
- Results are machine-dependent and should be regenerated on the demonstration machine.
- The current harness reports one timed pass per case; production benchmarking should add warmup,
  repeated trials, percentiles, and OS-level CPU/cache profiling.
