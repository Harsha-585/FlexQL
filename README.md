# FlexQL

A lightweight, disk-persistent SQL database implemented in C++17.

**Author**: PERLA.HARSHAVARDHAN  
**Roll**: 25CS60R72

## Features

- **SQL Support**: CREATE TABLE, INSERT, SELECT (with JOIN, WHERE, ORDER BY), DELETE
- **Disk Persistence**: All data stored on disk with buffer pool caching
- **B+ Tree Indexing**: Automatic indexing on primary key columns
- **Expiration Support**: Built-in TTL via EXPIRES_AT column
- **High Performance**: ~1M rows/second insertion throughput

## Build

```bash
make clean
make
```

## Run

Start the server:
```bash
./flexql-server
```

Run the benchmark:
```bash
./benchmark
```

## Performance

- **Bulk Insert**: 979,623 rows/second (10M rows in ~10 seconds)
- **Point Lookup**: <1ms (B+ tree indexed)
- **Range Scan**: <1ms
- **JOIN**: <1ms (hash join)

## Documentation

See [DESIGN_DOCUMENT.md](DESIGN_DOCUMENT.md) or [FlexQL_Design_Document.pdf](FlexQL_Design_Document.pdf) for detailed architecture documentation.

## Project Structure

```
FlexQL/
├── include/           # Header files
│   ├── common/        # Types, constants
│   ├── storage/       # Page, Pager, Record, Catalog
│   ├── index/         # B+ Tree
│   ├── cache/         # LRU Buffer Pool
│   ├── parser/        # SQL Lexer & Parser
│   ├── query/         # Query Executor
│   └── network/       # TCP Server
├── src/               # Implementation files
├── FlexQL_Benchmark_Unit_Tests-main/  # Test suite
├── Makefile
├── DESIGN_DOCUMENT.md
└── FlexQL_Design_Document.pdf
```

## License

MIT License
