#!/bin/bash
# ==========================================================================
# Copyright (C) 2023 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==========================================================================


export DTO_USESTDC_CALLS=0
export DTO_COLLECT_STATS=1

export DTO_COLLECT_ALG_STATS=1
export DTO_COLLECT_ALG_STATS_RAW_VALUES=1
export DTO_COLLECT_ALG_STATS_RAW_TIMES=1
export DTO_COLLECT_ALG_STATS_ACCESS_TIMES=0
export DTO_COLLECT_ALG_STATS_DSA_TIMESTAMPS=1
export DTO_COLLECT_ALG_STATS_LATEST_VALUES=1

export DTO_AUTO_ADJUST_USE_SPLIT_ALGORITHM=1

export DTO_STATS_PREFIX=''
export DTO_STATS_OUTPUT_TYPE=1

export DTO_PER_OP_AUTOTUNE_INSTANCES=0
export DTO_NUM_WQS=1

export DTO_IS_NUMA_AWARE=0
export DTO_OPPOSITE_NUMA=0

#export DTO_WAIT_METHOD=umwait
export DTO_WAIT_METHOD=busypoll  #yield  #          
#export DTO_WAIT_METHOD=sleep
export DTO_MIN_BYTES=4096  #8192 # 65536  # 28672 #   16384  #              32768  #  16384  #
export DTO_CPU_SIZE_FRACTION=0.33 # 0.64 # 0.0  #0.60  # 0.60 #
export DTO_AUTO_ADJUST_KNOBS=1
export DTO_DSA_CC=1

export DTO_LOG_LEVEL=0
export DTO_MAKE_ADJ=1

export DTO_SLEEP_DELAY_NS=100000
export DTO_SLEEP_DELAY_US=1

export DTO_STATS_NUM_WARMUP_OPS=0  # 300000
export DTO_AUTOTUNE_EXCLUDE_FAILED=1


# (i.e., without LD_PRELOAD)
export LD_LIBRARY_PATH=./


export DTO_OVERLAPPING_MEMMOVE_ACTION=1  # 0-CPU, 1-DSA


#taskset -c 56-56 ./dto-test-settable-size-dev5 1 0 2 800 1000 4194304 1 1 0 0 0 1 0 0 0 0 50 0

#taskset -c 56-57 ./dto-test-settable-size-dev5-with-dsa-time 1 1 2 800 1000 4194304 1 1 0 0 0 1 0 0 0 0 50 0 1

taskset -c 56-56 ./dto-test-settable-size-dev5 1 0 2 800 10000 8388000 1 1 0 0 0 1 0 0 0 0 0 0

