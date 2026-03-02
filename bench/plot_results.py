#!/usr/bin/env python3

import json
import sys

def ns_to_us(ns):
    return ns / 1000.0

def format_items_per_second(ips):
    if ips >= 1_000_000:
        return f"{ips / 1_000_000:.2f}M ops/s"
    elif ips >= 1_000:
        return f"{ips / 1000:.1f}K ops/s"
    return f"{ips:.0f} ops/s"

def main():
    if len(sys.argv) < 2:
        print("Usage: plot_results.py <results.json>")
        sys.exit(1)

    with open(sys.argv[1]) as f:
        data = json.load(f)

    context = data.get("context", {})
    benchmarks = data.get("benchmarks", [])

    print("=" * 70)
    print("  Order Matching Engine - Benchmark Results")
    print("=" * 70)
    if context:
        print(f"  Date:       {context.get('date', 'N/A')}")
        print(f"  Host:       {context.get('host_name', 'N/A')}")
        print(f"  CPUs:       {context.get('num_cpus', 'N/A')}")
        print(f"  MHz:        {context.get('mhz_per_cpu', 'N/A')}")
        print(f"  Compiler:   {context.get('library_build_type', 'N/A')}")
    print()

    print(f"  {'Benchmark':<42} {'Time (ms)':>10} {'Throughput':>16}")
    print("  " + "-" * 70)

    for bm in benchmarks:
        name = bm["name"]
        raw = bm["real_time"]
        unit = bm.get("time_unit", "ns")
        if unit == "ns":
            real_time_ms = raw / 1e6
        elif unit == "us":
            real_time_ms = raw / 1e3
        elif unit == "ms":
            real_time_ms = raw
        else:  # seconds
            real_time_ms = raw * 1e3
        ips = bm.get("items_per_second", 0)
        throughput = format_items_per_second(ips) if ips else "N/A"
        print(f"  {name:<42} {real_time_ms:>10.1f} {throughput:>16}")

    print("=" * 70)

if __name__ == "__main__":
    main()