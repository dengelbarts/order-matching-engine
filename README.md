# Order Matching Engine

A high-performance limit order matching engine implemented in modern C++17.

## Status

🚧 **Work in Progress** - Currently implementing Phase 1: Core Matching Engine

### Implementation Progress

- [ ] Phase 1: Core Matching Engine (Days 1-10)
  - [x] Day 1: Project Setup & Build System
  - [ ] Day 2: Price Representation & Core Enums
  - [ ] Day 3: Order Struct
  - [ ] Day 4: PriceLevel Class
  - [ ] Day 5-10: OrderBook & Matching Logic
- [ ] Phase 2: Extended Order Types (Days 11-15)
- [ ] Phase 3: Performance Optimization (Days 16-20)
- [ ] Phase 4: Multithreading, Polish & Ship (Days 21-25)

## Features (Planned)

- Price-time priority matching
- Order types: Limit, Market, IOC, FOK
- Order amendments and cancellations
- Lock-free SPSC queue for multi-threading
- Memory pooling for zero-allocation hot path
- Target: 150,000+ orders/second throughput

## Building
```bash
# Configure
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build

# Run tests
cd build && ctest --output-on-failure

# Run main executable
./build/ome_main
```

## Requirements

- CMake 3.14+
- C++17 compatible compiler (GCC 7+, Clang 5+, MSVC 2017+)
- Google Test (fetched automatically)

## License

MIT License - see [LICENSE](LICENSE) file for details.

## Documentation

See [docs/DESIGN.md](docs/DESIGN.md) for architectural decisions and implementation details.