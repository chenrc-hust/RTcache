# Simulator Environment and Trace Analysis Tool Integration

This project integrates the GEM5 simulator environment with a trace analysis toolchain for computer architecture research and cache performance analysis.

## Requirements

- Ubuntu 20.04/22.04 LTS (recommended)
- Python 3.6+
- gcc/g++ 9.0+
- SCons build tool
- make utility

## Installation Guide

### 1. Install Dependencies

```bash
sudo apt update
sudo apt install -y build-essential git m4 scons zlib1g zlib1g-dev \
    libprotobuf-dev protobuf-compiler libprotoc-dev libgoogle-perftools-dev \
    python3-dev python-is-python3 libboost-all-dev pkg-config
```

### 2. Clone and Build GEM5 Simulator

```bash
git clone https://github.com/chenrc-hust/RTcache.git  
cd RTcache
scons build/X86/gem5.opt -j$(nproc)
```

### 3. Install Trace Generator Tool

```bash
git clone https://github.com/dgist-datalab/trace_generator.git
cd trace_generator
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### 4. Build RTCache Analysis Tool

```bash
git clone https://github.com/chenrc-hust/RTcache.git  
cd RTcache/rtcache-cpp
make
```

## Usage Instructions

### Running GEM5 Simulator

Use the provided DIY configuration script to run simulations:

```bash
# From the gem5 directory
./build/X86/gem5.opt configs/deprecated/example/diy.py workload [additional parameters]
```

### Generating Trace Data

```bash
cd trace_generator/build
./trace_generator [options] <target_program>
```

### Running RTCache Analysis

```bash
cd rtcache-cpp
./start.sh [trace_file]
```

## Project Structure

```
.
├── ./                 # GEM5 simulator
├── rtcache-cpp/          # Trace analysis tool
│   ├── Makefile          # Build configuration
│   └── start.sh          # Startup script
└── README.md             # This file
```

## Example Workflow

1. Run benchmark programs using GEM5
2. Capture program execution traces using trace_generator
3. Analyze trace data using rtcache-cpp

## Claims/results to be reproduced



