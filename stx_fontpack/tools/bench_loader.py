"""Time the shipped stx_fontpack loader on a built font pack, and record where.

Runs stx_fontpack_bench (tools/fontpack_bench.cpp) the way the font pack
experiment was measured (stx_fontpack/PROVENANCE.md):

- every group, the eager groups together and every group in turn, the median
  of 25 rounds in one process, at 1 thread (serial_executor), 4 threads and
  every hardware thread (the tools' std::thread executor);
- the eager groups in 20 fresh processes, one set_open each, at 1 thread and
  at every hardware thread.

It writes <output>/loader-timings.md (the machine, compiler and flags, and the
tables for docs/user/factory-bundle.md) and the raw output beside it. Run it
on the measuring machine with nothing else busy, after a release build:

    python3 -B stx_fontpack/tools/bench_loader.py \\
        --binary <build>/stx_fontpack/stx_fontpack_bench \\
        --pack <build>/generated/bundled_fonts/bundled_fonts.fpk \\
        --build <build> --output build/fontpack-loader-timings
"""
import argparse
import datetime
import os
import platform
import re
import statistics
import subprocess
import sys
from pathlib import Path

THREAD_COUNTS = [("1 thread", "1"), ("4 threads", "4"), ("all threads", "0")]
FRESH_COUNTS = [("1 thread", "1"), ("all threads", "0")]
CASE = re.compile(r"^case (\S+)\s+rounds (\d+) median ([\d.]+) ms fastest ([\d.]+) ms "
                  r"slowest ([\d.]+) ms scratch ([\d.]+)%$")
FRESH = re.compile(r"^fresh eager-groups workers (\d+) ([\d.]+) ms$")


def run(command):
    result = subprocess.run(command, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        sys.exit(f"{' '.join(command)} failed ({result.returncode}):\n{result.stdout}{result.stderr}")
    return result.stdout


def machine_lines():
    lines = [f"- Host: {platform.system()} {platform.release()} {platform.machine()}"]
    if platform.system() == "Darwin":
        for key in ("hw.model", "machdep.cpu.brand_string", "hw.ncpu", "hw.perflevel0.physicalcpu",
                    "hw.perflevel1.physicalcpu", "hw.memsize"):
            result = subprocess.run(["sysctl", "-n", key], capture_output=True, text=True, check=False)
            if result.returncode == 0:
                lines.append(f"- {key}: {result.stdout.strip()}")
    elif platform.system() == "Linux":
        model = ""
        for line in Path("/proc/cpuinfo").read_text().splitlines():
            if line.startswith("model name"):
                model = line.split(":", 1)[1].strip()
                break
        lines.append(f"- CPU: {model}, {os.cpu_count()} logical processors")
    else:
        lines.append(f"- CPU: {platform.processor()}, {os.cpu_count()} logical processors")
    load = os.getloadavg() if hasattr(os, "getloadavg") else None
    if load is not None:
        lines.append(f"- Load average before the run: {load[0]:.2f} {load[1]:.2f} {load[2]:.2f}")
    return lines


def build_lines(build):
    cache = {}
    cache_path = build / "CMakeCache.txt"
    if cache_path.exists():
        for line in cache_path.read_text(errors="replace").splitlines():
            match = re.match(r"^([A-Za-z0-9_]+):[A-Z]+=(.*)$", line)
            if match:
                cache[match.group(1)] = match.group(2)
    build_type = cache.get("CMAKE_BUILD_TYPE", "")
    lines = [
        f"- Build tree: {build}",
        f"- CMAKE_CXX_COMPILER: {cache.get('CMAKE_CXX_COMPILER', 'unknown')}",
        f"- CMAKE_BUILD_TYPE: {build_type or 'unset'}",
        f"- CMAKE_CXX_FLAGS: {cache.get('CMAKE_CXX_FLAGS', '')}",
    ]
    if build_type:
        key = "CMAKE_CXX_FLAGS_" + build_type.upper()
        lines.append(f"- {key}: {cache.get(key, '')}")
    lines.append("- Package warnings: -Wall -Wextra -Wpedantic -Wconversion -Wshadow -ffp-contract=off "
                 "(stx_fontpack/CMakeLists.txt)")
    return lines


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--pack", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True, help="the CMake build tree (for the compiler and flags)")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--rounds", type=int, default=25)
    parser.add_argument("--fresh", type=int, default=20)
    arguments = parser.parse_args()
    arguments.output.mkdir(parents=True, exist_ok=True)

    started = datetime.datetime.now().astimezone().isoformat(timespec="seconds")
    machine = machine_lines()
    raw = []
    rounds = {}
    groups = []
    for label, threads in THREAD_COUNTS:
        output = run([str(arguments.binary), "rounds", str(arguments.pack),
                      "--rounds", str(arguments.rounds), "--threads", threads])
        raw.append(f"## rounds, {label}\n{output}")
        for line in output.splitlines():
            match = CASE.match(line)
            if match:
                name = match.group(1)
                if name not in groups:
                    groups.append(name)
                rounds[(name, label)] = (float(match.group(3)), float(match.group(5)), float(match.group(6)))
    compiler = [line for line in raw[0].splitlines() if line.startswith(("compiler", "assertions", "pack "))]

    fresh = {}
    for label, threads in FRESH_COUNTS:
        times = []
        for _ in range(arguments.fresh):
            output = run([str(arguments.binary), "fresh", str(arguments.pack), "--threads", threads])
            raw.append(f"## fresh, {label}\n{output}")
            for line in output.splitlines():
                match = FRESH.match(line)
                if match:
                    times.append(float(match.group(2)))
        times.sort()
        fresh[label] = (times[len(times) // 2], times[-1], statistics.mean(times))

    report = [f"# stx_fontpack loader timings ({started})", "", "## Where", ""]
    report += machine
    report += build_lines(arguments.build)
    report += [f"- Binary reports: {'; '.join(compiler)}", "",
               f"## Median of {arguments.rounds} rounds in one process (ms)", "",
               "| Decoded | " + " | ".join(label for label, _ in THREAD_COUNTS) + " | Scratch (all threads) |",
               "|---|" + "---:|" * (len(THREAD_COUNTS) + 1)]
    for name in groups:
        cells = []
        for label, _ in THREAD_COUNTS:
            median, _slowest, _scratch = rounds[(name, label)]
            cells.append(f"{median:.2f} ms")
        scratch = rounds[(name, THREAD_COUNTS[-1][0])][2]
        scratch_cell = f"{scratch:.1f}%" if scratch > 0 else "-"
        report.append(f"| {name} | " + " | ".join(cells) + f" | {scratch_cell} |")
    report += ["", f"## Eager groups, {arguments.fresh} fresh processes (ms)", "",
               "| Workers | Median | Slowest | Mean |", "|---|---:|---:|---:|"]
    for label, _ in FRESH_COUNTS:
        median, slowest, mean = fresh[label]
        report.append(f"| {label} | {median:.2f} ms | {slowest:.2f} ms | {mean:.2f} ms |")
    report.append("")

    (arguments.output / "loader-timings.md").write_text("\n".join(report))
    (arguments.output / "raw.txt").write_text("\n".join(raw))
    print("\n".join(report))


if __name__ == "__main__":
    main()
