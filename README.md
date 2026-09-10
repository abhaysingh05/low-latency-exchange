# Low-Latency Exchange

A C++20 low-latency exchange engine with Linux epoll TCP ingress, lock-free SPSC queues, and FIFO price-time order matching.

## Architecture

```
Network Thread (epoll)  ──►  SPSC Queue  ──►  Engine Thread (spin loop)
                         ◄──  SPSC Queue  ◄──  OrderBook (price-time FIFO)
```

**Two-thread design:**
- **Network Thread**: Edge-triggered epoll accepts TCP connections, deserializes binary order messages, and pushes them into the lock-free inbound SPSC queue.
- **Engine Thread**: Tight spin loop pops orders from the inbound queue, processes them through the matching engine and order book, and pushes responses into the outbound queue.

## Key Features

- **Lock-free SPSC queues** with cache-line-aligned atomics and acquire-release memory ordering
- **Intrusive doubly-linked list** order queues for O(1) insert/remove with zero allocation
- **Pre-allocated object pool** - zero `malloc` on the hot path
- **Flat array price levels** indexed by tick for O(1) best-bid/ask lookup
- **Binary wire protocol** - `memcpy`-based serialization, no parsing overhead
- **Edge-triggered epoll** with `TCP_NODELAY` and non-blocking I/O

## Build

```bash
# Build everything (exchange, benchmarks, tests)
make all

# Build individual targets
make exchange       # Exchange server binary
make bench-bins     # Benchmark binaries
make test-bin       # Test binary

# Run tests
make test

# Run benchmarks
make bench

# Clean
make clean
```

**Requirements:** GCC 12+ or Clang 14+ with C++20 support, Linux (for epoll).

## Usage

```bash
# Start exchange on default port 9000
./build/exchange

# Start on custom port
./build/exchange 8080
```

## Project Structure

```
include/exchange/
  types.hpp           - Core types (Order, Trade, OrderRequest/Response)
  spsc_queue.hpp      - Lock-free SPSC ring buffer
  object_pool.hpp     - Pre-allocated fixed-size object pool
  order_book.hpp      - FIFO price-time order book
  matching_engine.hpp - Engine that dispatches to OrderBook
  protocol.hpp        - Binary wire protocol
  tcp_server.hpp      - Epoll-based TCP server

src/
  order_book.cpp      - Order book implementation
  matching_engine.cpp - Engine spin loop
  tcp_server.cpp      - TCP server implementation
  main.cpp            - Entry point

bench/
  bench_common.hpp    - Benchmark harness (timing, percentiles)
  bench_order_book.cpp - Order book microbenchmarks
  bench_throughput.cpp - End-to-end throughput benchmarks

tests/
  test_all.cpp        - Functional correctness tests
```

## Benchmarks

Run with CPU pinning for best results:

```bash
taskset -c 0 ./build/bench_order_book
taskset -c 0 ./build/bench_throughput
```

## License

MIT
