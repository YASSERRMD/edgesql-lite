# EdgeSQL Lite Design Document

## 1. Overview

EdgeSQL Lite is a deterministic, budget-enforced SQL server designed for edge deployments. The primary design goal is that every query has hard upper bounds on CPU, memory, IO, and time.

## 2. Non-Negotiable Design Constraints

### 2.1 Hard Constraints

| Constraint | Requirement |
|------------|-------------|
| Language | Modern C++ (C++20) |
| Deployment | Single binary |
| JIT Compilation | Forbidden |
| Dynamic Plugins | Forbidden |
| Background GC | Forbidden |
| Memory Growth | Must be bounded |
| Query Execution | Must be bounded |

### 2.2 Target Environment

- Edge VM / edge server
- Limited RAM: 512MB – 2GB
- Possibly no swap
- Intermittent power/network
- Single-core to quad-core CPUs

### 2.3 SQL Scope (Frozen)

**Supported:**
- `CREATE TABLE`
- `INSERT`
- `SELECT`
- `WHERE`
- `ORDER BY`
- `LIMIT`
- Aggregates: `COUNT`, `SUM`, `MIN`, `MAX`

**Not Supported (Initially):**
- `JOIN`
- Subqueries
- Views
- Stored procedures
- Triggers

## 3. Architecture

### 3.1 Component Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                         EdgeSQL Lite                            │
├─────────────────────────────────────────────────────────────────┤
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────┐  │
│  │   HTTP/JSON │  │  Security   │  │     Observability       │  │
│  │   Server    │  │  (Auth/TLS) │  │  (Logs/Metrics/Health)  │  │
│  └──────┬──────┘  └──────┬──────┘  └────────────┬────────────┘  │
│         │                │                      │               │
│         └────────────────┴──────────────────────┘               │
│                          │                                      │
│  ┌───────────────────────┴───────────────────────────────────┐  │
│  │                    Query Handler                          │  │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐  │  │
│  │  │ Tokenizer│─▶│  Parser  │─▶│ Planner  │─▶│ Executor │  │  │
│  │  └──────────┘  └──────────┘  └──────────┘  └──────────┘  │  │
│  └───────────────────────────────────────────────────────────┘  │
│                          │                                      │
│  ┌───────────────────────┴───────────────────────────────────┐  │
│  │                    Core Services                          │  │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────────┐ │  │
│  │  │ Memory Arena │  │ Concurrency  │  │  Thread Pool     │ │  │
│  │  │   Manager    │  │   Control    │  │                  │ │  │
│  │  └──────────────┘  └──────────────┘  └──────────────────┘ │  │
│  └───────────────────────────────────────────────────────────┘  │
│                          │                                      │
│  ┌───────────────────────┴───────────────────────────────────┐  │
│  │                   Storage Engine                          │  │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐  │  │
│  │  │   WAL    │  │  Pages   │  │ Segments │  │ Recovery │  │  │
│  │  └──────────┘  └──────────┘  └──────────┘  └──────────┘  │  │
│  └───────────────────────────────────────────────────────────┘  │
│                          │                                      │
│                    ┌─────┴─────┐                                │
│                    │   Disk    │                                │
│                    └───────────┘                                │
└─────────────────────────────────────────────────────────────────┘
```

### 3.2 Thread Model

```
Main Thread
    │
    ├── Signal Handler (async signals)
    │
    ├── Listener Thread (accepts connections)
    │
    └── Worker Thread Pool (fixed size)
            ├── Worker 1
            ├── Worker 2
            ├── Worker 3
            └── Worker N
```

- **No thread creation per query**
- Fixed worker pool sized to CPU cores
- All workers share a task queue

## 4. Storage Engine

### 4.1 Page Layout

```
┌────────────────────────────────────┐
│           Page (8 KB)              │
├────────────────────────────────────┤
│  PageHeader (16 bytes)             │
│    - magic: uint32_t               │
│    - page_id: uint32_t             │
│    - lsn: uint64_t                 │
├────────────────────────────────────┤
│  Slot Directory                    │
│    - slot_count: uint16_t          │
│    - slots[]: (offset, length)     │
├────────────────────────────────────┤
│  Free Space                        │
├────────────────────────────────────┤
│  Row Records (grow upward)         │
│    - RowRecord 1                   │
│    - RowRecord 2                   │
│    - ...                           │
└────────────────────────────────────┘
```

### 4.2 WAL Format

```
┌─────────────────────────────────────────┐
│            WAL Record                   │
├─────────────────────────────────────────┤
│  lsn: uint64_t                          │
│  type: uint8_t (INSERT/UPDATE/DELETE)   │
│  table_id: uint32_t                     │
│  page_id: uint32_t                      │
│  slot_id: uint16_t                      │
│  payload_size: uint32_t                 │
│  payload: bytes[]                       │
│  checksum: uint32_t (CRC32)             │
└─────────────────────────────────────────┘
```

### 4.3 Durability Guarantees

1. WAL is written first, then data pages
2. `fsync` after each WAL write (configurable batching)
3. Crash recovery replays WAL from last checkpoint
4. Incomplete WAL records are discarded (checksum validation)

## 5. Memory Model

### 5.1 Arena Allocator

```cpp
class Arena {
    uint8_t* base;      // Start of arena
    size_t capacity;     // Total size
    size_t offset;       // Current allocation point
    
    void* allocate(size_t size);
    void reset();        // Called at query end
};
```

### 5.2 Memory Budgets

| Scope | Limit | Enforcement |
|-------|-------|-------------|
| Global | Configured at startup | Process exits if exceeded |
| Per-Query | Configured per query | Query aborted immediately |
| Temporary | Part of query budget | Included in query accounting |

### 5.3 No Hidden Allocations

- No `malloc` in execution path except through arena
- All STL containers avoided or replaced with arena-aware versions
- String operations use arena storage

## 6. Query Execution

### 6.1 Budget Enforcement

```cpp
struct QueryBudget {
    uint64_t max_instructions;   // CPU budget
    size_t max_memory_bytes;     // Memory budget
    std::chrono::milliseconds max_time;  // Time budget
};

struct QueryContext {
    QueryBudget budget;
    uint64_t instructions_used = 0;
    size_t memory_used = 0;
    std::chrono::steady_clock::time_point deadline;
};
```

### 6.2 Instruction Counting

Every executor operation increments the instruction counter:

```cpp
inline void check_budget(QueryContext& ctx) {
    if (++ctx.instructions_used > ctx.budget.max_instructions) {
        throw BudgetExceeded(BudgetType::CPU);
    }
    if (std::chrono::steady_clock::now() > ctx.deadline) {
        throw BudgetExceeded(BudgetType::TIME);
    }
}
```

### 6.3 Pull-Based Execution

```
┌──────────────┐
│   Result     │ ◀── pull
├──────────────┤
│   Filter     │ ◀── pull
├──────────────┤
│   Scan       │ ◀── read pages
├──────────────┤
│   Table      │
└──────────────┘
```

## 7. Concurrency Model

### 7.1 Single Writer, Multiple Readers

```
                    ┌─────────────────┐
                    │  RW Lock        │
                    ├─────────────────┤
 Reader 1 ─────────▶│  shared_lock    │
 Reader 2 ─────────▶│  shared_lock    │
 Reader N ─────────▶│  shared_lock    │
                    │                 │
 Writer ───────────▶│  unique_lock    │ (blocks all readers)
                    └─────────────────┘
```

### 7.2 Locking Granularity

- Table-level locks (initially)
- Page-level locks (future)
- Row-level locks (not planned)

## 8. Server Protocol

### 8.1 HTTP/JSON API

**Query Request:**
```http
POST /query HTTP/1.1
Content-Type: application/json
X-API-Key: <key>

{
    "sql": "SELECT * FROM users WHERE age > 25",
    "budget": {
        "max_cpu": 10000,
        "max_memory_mb": 64,
        "max_time_ms": 1000
    }
}
```

**Success Response:**
```json
{
    "columns": ["id", "name", "age"],
    "rows": [[1, "Alice", 30]],
    "stats": {
        "cpu_used": 1234,
        "memory_used_bytes": 4096,
        "time_ms": 2
    }
}
```

**Error Response:**
```json
{
    "error": "MEMORY_BUDGET_EXCEEDED",
    "message": "Query exceeded memory budget",
    "used_memory_bytes": 67108864,
    "limit_bytes": 67108864
}
```

## 9. Security

### 9.1 Authentication

- API key in `X-API-Key` header
- Keys stored hashed in config file
- Constant-time comparison to prevent timing attacks

### 9.2 Authorization

| Role | Permissions |
|------|-------------|
| read | SELECT |
| write | SELECT, INSERT, CREATE TABLE |
| admin | All operations + config API |

### 9.3 OS-Level Hardening

- Drop privileges after binding to port
- Chroot to data directory (optional)
- Recommended ulimits in documentation

## 10. Metrics & Observability

### 10.1 Exposed Metrics

| Metric | Type | Description |
|--------|------|-------------|
| queries_total | Counter | Total queries received |
| queries_aborted_cpu | Counter | Aborted due to CPU budget |
| queries_aborted_memory | Counter | Aborted due to memory budget |
| queries_aborted_time | Counter | Aborted due to time budget |
| query_duration_ms | Histogram | Query execution time |
| memory_used_bytes | Gauge | Current memory usage |

### 10.2 Health Endpoint

```http
GET /health

{
    "status": "healthy",
    "uptime_seconds": 3600,
    "queries_total": 12345,
    "memory_used_mb": 128,
    "memory_limit_mb": 512
}
```

## 11. Configuration

```toml
[server]
bind_address = "0.0.0.0"
port = 8080
worker_threads = 4

[storage]
data_dir = "/var/lib/edgesql"
wal_sync_mode = "fsync"  # none, fsync, fdatasync
page_size = 8192

[memory]
global_limit_mb = 512
default_query_limit_mb = 64

[budget]
default_max_instructions = 1000000
default_max_time_ms = 5000

[security]
require_auth = true
api_keys_file = "/etc/edgesql/keys.conf"
tls_enabled = false
tls_cert = "/etc/edgesql/cert.pem"
tls_key = "/etc/edgesql/key.pem"

[logging]
level = "info"  # debug, info, warn, error
format = "json"
file = "/var/log/edgesql/edgesql.log"
```

## 12. Deployment

### 12.1 Docker Image

- Base: scratch or alpine:latest
- Size target: <50MB
- Statically linked binary

### 12.2 Recommended ulimits

```bash
ulimit -n 65536    # Open files
ulimit -m 524288   # Memory (KB)
ulimit -c 0        # Core dumps disabled
```

### 12.3 systemd Service

```ini
[Unit]
Description=EdgeSQL Lite
After=network.target

[Service]
Type=simple
ExecStart=/usr/bin/edgesql-lite --config /etc/edgesql/edgesql.conf
Restart=on-failure
User=edgesql
Group=edgesql
LimitNOFILE=65536
MemoryMax=512M

[Install]
WantedBy=multi-user.target
```
