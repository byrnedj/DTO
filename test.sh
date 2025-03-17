#!/bin/bash
# ==========================================================================
# Copyright (C) 2023 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==========================================================================

#accel-config disable-device dsa0
#accel-config disable-device dsa2
#accel-config disable-device dsa4
#accel-config disable-device dsa6

#accel-config load-config -c ./dto-4-dsa.conf

#accel-config enable-device dsa0
#accel-config enable-device dsa2
#accel-config enable-device dsa4
#accel-config enable-device dsa6

#accel-config enable-wq dsa0/wq0.0
#accel-config enable-wq dsa2/wq2.0
#accel-config enable-wq dsa4/wq4.0
#accel-config enable-wq dsa6/wq6.0

export DTO_USESTDC_CALLS=0
export DTO_COLLECT_STATS=0
export DTO_COLLECT_ALG_STATS=0
export DTO_COLLECT_ALG_STATS_LATEST_UPDATES=1
export DTO_COLLECT_ALG_STATS_LATEST_CPUFRACTS=1
export DTO_STATS_PREFIX=''
export DTO_STATS_OUTPUT_TYPE=1

export DTO_NUM_AUTOTUNE_INSTANCES=1
export DTO_PER_OP_AUTOTUNE_INSTANCES=0
export DDTO_NUM_AUTOTUNE_INSTANCES=1
export DTO_PER_OP_AUTOTUNE_INSTANCES=0=8

export DTO_IS_NUMA_AWARE=0
export DTO_OPPOSITE_NUMA=1

#export DTO_WAIT_METHOD=umwait
export DTO_WAIT_METHOD=yield
#export DTO_WAIT_METHOD=sleep
export DTO_MIN_BYTES=8192 # 28672 #   16384  #              32768  #  16384  #
export DTO_MAX_BYTES=2097152  #  8192  # 32768  #65536 #16384  #
export DTO_CPU_SIZE_FRACTION=0.33
export DTO_AUTO_ADJUST_KNOBS=1
export DTO_DSA_CC=1

export DTO_LOG_LEVEL=0
export DTO_MAKE_ADJ=1

export DTO_SLEEP_DELAY_NS=100000
export DTO_SLEEP_DELAY_US=1

# Run dto-test without DTO library
#/usr/bin/time ./dto-test-wodto

# Run dto-test with DTO library using LD_PRELOAD method
#export LD_PRELOAD=/home/jjsydir/internal_dto/projects.research.dto_dev/libdto.so.1.0
#/usr/bin/time ./dto-test-wodto
#/usr/bin/time /home/jjsydir/ssb-poc-0.10.0/ssb-poc/output/bin/benchmark.sh -p -d ssb

#./check_stdlib_usage

# Run dto-test with DTO library using "re-compile with DTO" method
# (i.e., without LD_PRELOAD)
export LD_LIBRARY_PATH=./
#./dto-test-distribution 1 10000000 ./all_ops.csv
#/usr/bin/time ./dto-test-distribution 1 50000 250 ./ops_over_8K.csv

#/usr/bin/time  ./dto-test-dev 
#/usr/bin/time ./dto-test-size-steps 10 0 3  16 2048 64 1000000
#exec ./dto-test 
#exec ./dto-test-wodto 
#exec ./test2.sh
#emon  -collect-edp    edp_file=/opt/intel/sep/config/edp/sapphirerapids_server_events.txt -t0 ./dto-test-dev
#taskset -c 49-59 chrt -rr 1 perf stat ./dto-test
#taskset -c 49-59 perf stat ./dto-test

#perf stat ./dto-test-distribution-nopause 10 1000000 ./all_ops.csv
#perf stat ./dto-test-distribution-nopause 10 1000000 ./ops_over_8K.csv
#perf stat ./dto-test-settable-size 1 1 16 10000000
#taskset -c 49 perf stat ./dto-test-settable-size 1 0 1 16 10000000
#taskset -c 49-50 chrt -rr 2 perf stat ./dto-test-settable-size 1 0 1 512 1000000

#taskset -c 49 perf stat ./dto-test-settable-size-dev 1 0 1 16 100   #000000
#perf stat taskset -c 56 ./dto-test-settable-size-dev 1 0 2 1024 1000000 #000000
#perf stat taskset -c 56 ./dto-test-settable-size-dev2 1 0 2 1024 1000000 50331648 1 0 #000000
#taskset -c 56-57 ./dto-test-settable-size-dev2 1 0 2 8 10000 8 1048576 1 0 0
taskset -c 56-57 ./dto-test-settable-size-dev2 1 0 2 64 100000 64 1024 1 0 0
#perf stat taskset -c 56 ./dto-test-settable-size-dev2 1 0 2 1024 1000000 1024 49152 0 #000000
#perf stat taskset -c 56 ./dto-test-settable-size-dev2 1 0 2 1024 1000000 1024 49152 1 #000000
#/usr/bin/time ./dto-test-settable-size 1 8 100000

#perf stat ./dto-test-distribution 500000000 ./salesforce_all_ops.csv
#perf stat ./dto-test-distribution 5000000000 ./all_ops.csv
#./dto-test-distribution-nopause 100000000 ./all_ops.csv
#perf stat ./dto-test-distribution-nopause 1000000 ./test_ops.csv

#/usr/bin/time ./dto-test-distribution-multithread-dev2 10000000 10 ./uniform_dist_cpy_8_2048_64.csv 
#/usr/bin/time ./dto-test-distribution-multithread-dev2 10000000 10 ./bimodal-8K-512K_cpy.v2.csv
#taskset -c 56-72 /usr/bin/time ./dto-test-distribution-multithread-dev2 20000000 16 ./bimodal-34K-514K_cpy.v2.csv
#taskset -c 56-72 /usr/bin/time ./dto-test-distribution-multithread-dev2 20000000 16 ./bimodal-34K-514K_setcpy.v2.csv
#/usr/bin/time ./dto-test-distribution-multithread-dev2 10000000 16 ./bimodal-66K-1026K_set.v2.csv
#/usr/bin/time ./dto-test-distribution-multithread-dev2 10000000 16 ./bimodal-34K-514K_setcpy.v2.csv

#./dto-test-distribution-nopause 100 ./all_ops.csv

#./dto-test-distribution-multithread-dev2 10000 1 ./bimodal-66K-1030K_setcpy.v2.csv

#100000
# Run dto-test with DTO and get DSA perfmon counters
#perf stat -e dsa0/event=0x1,event_category=0x0/,dsa2/event=0x1,event_category=0x0/,dsa4/event=0x1,event_category=0x0/,dsa6/event=0x1,event_category=0x0/,dsa0/event=0x1,event_category=0x1/,dsa2/event=0x1,event_category=0x1/,dsa4/event=0x1,event_category=0x1/,dsa6/event=0x1,event_category=0x1/,dsa0/event=0x2,event_category=0x1/,dsa2/event=0x2,event_category=0x1/,dsa4/event=0x2,event_category=0x1/,dsa6/event=0x2,event_category=0x1/ /usr/bin/time ./dto-test
