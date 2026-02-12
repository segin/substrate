#!/bin/bash
# Generate swapper mock for benchmarking
cp ../../sys/kern/swapper.c swapper_mock.c
sed -i 's/#include <kern\/sched.h>//g' swapper_mock.c
sed -i 's/#include <sys\/proc.h>//g' swapper_mock.c
sed -i 's/__asm__ volatile("cli");//g' swapper_mock.c
sed -i 's/__asm__ volatile("sti");//g' swapper_mock.c
sed -i 's/__asm__ volatile("sti\\n" "hlt\\n");//g' swapper_mock.c
# Modify infinite loop to run once
sed -i 's/for (;;)/for (int _i = 0; _i < 1; _i++)/g' swapper_mock.c

# Compile benchmark
gcc -o bench_idle bench_idle.c

# Run benchmark
./bench_idle

# Cleanup
rm swapper_mock.c bench_idle
