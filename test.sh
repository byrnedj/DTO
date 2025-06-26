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
export DTO_COLLECT_STATS=1
export DTO_COLLECT_ALG_STATS=0
export DTO_COLLECT_ALG_STATS_LATEST_UPDATES=1
export DTO_COLLECT_ALG_STATS_LATEST_CPUFRACTS=1
export DTO_STATS_PREFIX=''
export DTO_STATS_OUTPUT_TYPE=1

#export DTO_NUM_AUTOTUNE_INSTANCES=1
export DTO_PER_OP_AUTOTUNE_INSTANCES=0
export DTO_NUM_WQS=1

export DTO_IS_NUMA_AWARE=0
export DTO_OPPOSITE_NUMA=0

#export DTO_WAIT_METHOD=umwait
export DTO_WAIT_METHOD=busypoll  #yield  #         
#export DTO_WAIT_METHOD=sleep
export DTO_MIN_BYTES=8192 # 65536  # 28672 #   16384  #              32768  #  16384  #
export DTO_MAX_BYTES=2097152  #  8192  # 32768  #65536 #16384  #
export DTO_CPU_SIZE_FRACTION=0.0  #0.33 # 0.64 # 0.0  #0.60  # 0.60 #
export DTO_AUTO_ADJUST_KNOBS=0
export DTO_DSA_CC=1

export DTO_LOG_LEVEL=3
export DTO_MAKE_ADJ=1

export DTO_SLEEP_DELAY_NS=100000
export DTO_SLEEP_DELAY_US=1

export DTO_STATS_NUM_WARMUP_OPS=300000
export DTO_AUTOTUNE_EXCLUDE_FAILED=1




#average CPU Fraction
#Yield
#cpy_10: wq1 73 wq128 74
#cpy_1: wq1 44 wq128 45
#cpy_5: wq1 60 wq128 64

#Busypoll
#cpy_10: wq1 73 wq128 74
#cpy_1: wq1 48 wq128 47
#cpy_5: wq1 62 wq128 65

# Run dto-test without DTO library
#/usr/bin/time ./dto-test-wodto

# Run dto-test with DTO library using LD_PRELOAD method
#export LD_PRELOAD=/home/jjsydir/internal_dto/projects.research.dto_dev/libdevdto.so.1.0
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
                                                                      
#/usr/bin/time taskset -c 56-60  ./dto-test-settable-size-dev 5 0 2 128 2000000 #2500000 # 3333333 #5000000 # 10000000  # 500000 # #000000
#perf stat taskset -c 56 ./dto-test-settable-size-dev 1 0 2 1024 1000000 #000000
#perf stat taskset -c 56 ./dto-test-settable-size-dev2 1 0 2 1024 1000000 50331648 1 0 #000000
#taskset -c 56-57 ./dto-test-settable-size-dev2 1 0 2 8 10000 8 1048576 1 0 0
#taskset -c 56-57 ./dto-test-settable-size-dev2 1 0 2 64 1000000000 64 1024 1 0 0
#taskset -c 56-57 ./dto-test-dev
#/usr/bin/time taskset -c 56-57 ./dto-test-wodto
#perf stat taskset -c 56-57 ./dto-test-settable-size-dev2 1 0 2 1024 0 1024 16 1 1 1
#perf stat taskset -c 56 ./dto-test-settable-size-dev2 1 0 2 1024 1000000 1024 49152 0 #000000
#perf stat taskset -c 56 ./dto-test-settable-size-dev2 1 0 2 1024 1000000 1024 49152 1 #000000
#/usr/bin/time taskset -c 56-61 ./dto-test-settable-size-dev3 6 0 2 1024 1000000 524288 1 0 1 1
#/usr/bin/time taskset -c 56-61 ./dto-test-settable-size-dev3 6 0 2 1024 100000000 524288 1 0 1 1
#/usr/bin/time taskset -c 56-59 ./dto-test-settable-size-wodto3 4 0 2 1024 1000000 524288 1 0 1 1

#/usr/bin/time taskset -c 56-65 ./dto-test-settable-size-dev4            10 0 2 1024 1000000 8388608 1 1 0 0    #1000000

#/usr/bin/time taskset -c 56-86 ./dto-test-settable-size-dev5 20 0 2 1024 1000000 8388608 1 1 0 0 15 500 500 1
#/usr/bin/time taskset -c 56-81 ./dto-test-settable-size-dev5 15 0 2 1024 1000000 8388608 1 1 0 0    #1000000
#/usr/bin/time taskset -c 56-76 ./dto-test-settable-size-dev5 10 0 2 1024 1000000 8388608 1 1 0 0    #1000000
#/usr/bin/time taskset -c 56-76 ./dto-test-settable-size-dev5 5 0 2 1024 1000000 8388608 1 1 0 0    #1000000
#/usr/bin/time taskset -c 56 ./dto-test-settable-size-dev5 1 0 2 1024 100000 8388608 1 1 0 0    #1000000

#taskset -c 56 ./dto-test-distribution-multithread-dev5-2 1 0 10000000 16777216 1 1 0 0 0 1 0 0 "./dto-test-configs/bimodal_512_2048_cpy.csv"

#/usr/bin/time taskset -c 56 ./dto-test-distribution-multithread-dev5 1 0 100000 8388608 1 1 0 0 0 1 0 0 ./dto-test-configs/uniform_cpy_256_test.csv
#taskset -c 56 ./dto-test-settable-size-dev5-2 1 0 2 256       100000 8388608 1 1 0 0 0 1 0 0

#taskset -c 56 ./dto-test-settable-size5 1 0 2 64       300000 8388608 1 1 0 0 0 1 0 0

#export DTO_STATS_NUM_WARMUP_OPS=0  #150000  #300000
#export DTO_AUTOTUNE_EXCLUDE_FAILED=0

#./dto-test-settable-size-dev5-2

#old
#/usr/bin/time taskset -c 56 ./dto-test-settable-size5 1 0 2 781   10000000    8388608 1 1 0 0 0 1 0 0  #

#new
#/usr/bin/time taskset -c 56 ./dto-test-settable-size-dev5-2 1 0 2 781   1000000    8388608 1 1 0 0 0 1 0 0  #


#./dto-test-settable-size-dev5 1 0 7 781   100000    8388608 1 0 0 0 0 1 0 0 50
#taskset -c 56-58 ./dto-test-settable-size-dev5 2 0 1 64 10000000 8388608 1 1 0 0 0 1 0 0 0

#/usr/bin/time taskset -c 56-60 ./dto-test-settable-size-dev5 4 0 4 1024 1000000 8388608 1 1 0 0 0 1 0 0 75

export DTO_OVERLAPPING_MEMMOVE_ACTION=1  # 0-CPU, 1-DSA
#taskset -c 56 ./dto-test-settable-size-dev5-2 1 0 4 256 1000000 8388608 1 1 0 0 0 1 0 0 55 50

#taskset -c 56 ./dto-test-settable-size-dev5-2 1 0 4 256 100000 8388608 1 1 0 0 0 1 0 0 55 100
#taskset -c 56 ./dto-test-distribution-multithread-dev5-2 1 0 20 8388608 1 0 0 0 0 1 0 0 55 100 ./scripts/temp_memop_dist.csv
#./dto-test-settable-size-dev5-2 1 0 4 1024 1000 10240 1 0 0 0 0 1 0 0 55 50

taskset -c 56-57 ./dto-test-settable-size-dev5 1 0 4 512 100000000 8388608 1 0 0 0 0 1 0 0 50 100

#./junk 1 0 4 1024 1000 10240 1 0 0 0 0 1 0 0 50 100

#taskset -c 56-59    ./dto-test-settable-size-dev5-2 4 0 2 1024   10000000 240000000 1 1 0 0 0 1 0 0
#taskset -c 56-59  ./dto-test-settable-size-dev3-2   4 0 2 1024   1000000   67108864    1 0 0 0


#taskset -c 56-65 ./dto-test-dev

#ramp_types
RAMP_TYPE_NONE=0
RAMP_TYPE_UP_LINEAR=1
RAMP_TYPE_DOWN_LINEAR=2

#ramp_modes 
RAMP_MODE_ALL=0
RAMP_MODE_CONSTANT=1
RAMP_MODE_BURSTING=2


#perf stat taskset -c 56-57 ./dto-test-settable-size-dev5 1 0 1 64 1000000 8388608 1 1 0 0 0 1 0 0
#perf stat taskset -c 56-57 ./dto-test-settable-size-dev5 1 0 2 2048 100000 8388608 1 1 0 0 0 1 0 0
#perf stat taskset -c 56-61 ./dto-test-distribution-multithread-dev6 5 10 $RAMP_TYPE_DOWN_LINEAR $RAMP_MODE_ALL 10000000 8388608 1 1 0 0 0 500 500 1 "./dto-test-configs/uniform_cpy_64_1024.csv"

#perf stat taskset -c 56 ./dto-test-distribution-multithread-dev4 1 0 10000000 8388608 1 1 0 0 ./dto-test-configs/uniform_cpy_64_100.csv
#perf stat taskset -c 56-61 ./dto-test-distribution-multithread-dev4 5 0 10000000 8388608 1 1 0 0 ./dto-test-configs/uniform_cpy_64_100.csv
#perf stat taskset -c 56-66 ./dto-test-distribution-multithread-dev4 10 0 10000000 8388608 1 1 0 0 ./dto-test-configs/uniform_cpy_64_100.csv

#/usr/bin/time taskset -c 56 ./dto-test-distribution-multithread-dev4 1 0 100000 8388608 1 1 0 0 "./dto-test-configs/uniform_cpy_64_1024.csv"


#taskset -c 56 ./dto-test-settable-size-dev3 6 0 2 1024 10000 524288 1 0 1 1                                                              

#/usr/bin/time taskset -c 56-59 ./dto-test
#/usr/bin/time ./dto-test
#taskset -c 56 ./dto-test-settable-size-dev2 1 0 2 64 100000 524288 1 0 1 1   # single, sequential
#taskset -c 56 ./dto-test-settable-size-dev2 1 0 1 64 100000 1024 512 1 1 1  #individual, random
#taskset -c 56 ./dto-test-distribution-multithread-dev3 1 100000 524288 1 0 1 1 "./dto-test-configs/uniform_normalized_cpy_64_1024.csv" # single sequential
#taskset -c 56 ./dto-test-distribution-multithread-dev3 1 100000 1024 512 1 1 1 "./dto-test-configs/uniform_normalized_cpy_64_1024.csv"   # individual random
#taskset -c 56 ./dto-test-distribution-multithread-dev3 1 100000 524288 1 0 1 1 "./dto-test-configs/uniform_normalized_set_64_1024.csv"
#taskset -c 56 ./dto-test-settable-size-dev2 1 0 1 1024 50000 524288 1 0 1 1
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
