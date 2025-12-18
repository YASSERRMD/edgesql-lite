<p align="center">
  <img src="assets/logo.png" alt="EdgeSQL Lite Logo" width="300">
</p>

# EdgeSQL Lite

A deterministic, budget-enforced SQL server for edge systems.

## Overview

EdgeSQL Lite is a lightweight SQL database designed specifically for edge deployments. Every query has hard upper bounds on CPU, memory, IO, and time, making it predictable and safe for resource-constrained environments.

## Key Features

- **Deterministic Execution**: Hard limits on CPU instructions, memory, and time per query
- **Budget Enforcement**: Queries are aborted cleanly when they exceed their resource budget
- **Crash Safe**: Append-only storage with WAL for durability
- **Edge Optimized**: Designed for 512MB-2GB RAM environments
- **Single Binary**: No external dependencies, easy deployment
- **HTTP/JSON API**: Simple, debuggable protocol

## Supported SQL

```sql
-- Data Definition
CREATE TABLE users (id INT, name TEXT, age INT);

-- Data Manipulation
INSERT INTO users VALUES (1, 'Alice', 30);

-- Queries
SELECT * FROM users WHERE age > 25 ORDER BY name LIMIT 10;

-- Aggregates
SELECT COUNT(*), SUM(age), MIN(age), MAX(age) FROM users;
```

## Design Constraints

| Constraint | Value |
|------------|-------|
| Language | C++20 |
| Binary | Single static |
| JIT | None |
| Background GC | None |
| Memory Growth | Bounded |
| Query Execution | Budget-enforced |

## Building

### Prerequisites

- CMake 3.20+
- C++20 compatible compiler (GCC 10+, Clang 12+)

### Build Steps

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### Static Build

```bash
cmake -DSTATIC_BUILD=ON ..
make -j$(nproc)
```

## Running

```bash
./edgesql-lite --config /etc/edgesql/edgesql.conf
```

## Configuration

See `config/edgesql.conf.example` for configuration options.

## API

### Query Endpoint

```bash
curl -X POST http://localhost:8080/query \
  -H "Content-Type: application/json" \
  -H "X-API-Key: your-api-key" \
  -d '{"sql": "SELECT * FROM users LIMIT 10"}'
```

### Response

```json
{
  "columns": ["id", "name", "age"],
  "rows": [
    [1, "Alice", 30],
    [2, "Bob", 25]
  ],
  "stats": {
    "cpu_used": 1234,
    "memory_used": 4096,
    "time_ms": 2
  }
}
```

### Budget Exceeded Response

```json
{
  "error": "CPU_BUDGET_EXCEEDED",
  "used_cpu": 12300,
  "limit": 10000
}
```

## Health Check

```bash
curl http://localhost:8080/health
```

## License

MIT License
