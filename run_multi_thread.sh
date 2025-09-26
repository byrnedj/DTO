#!/bin/bash
# ==========================================================================
# Copyright (C) 2023 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==========================================================================


# You can set the DTO envrironment variables as you need them for 
export DTO_USESTDC_CALLS=0
export DTO_COLLECT_STATS=0
export DTO_COLLECT_ALG_STATS=0
export DTO_PER_OP_AUTOTUNE_INSTANCES=0
export DTO_IS_NUMA_AWARE=0
export DTO_OPPOSITE_NUMA=0
export DTO_WAIT_METHOD=busypoll  #yield  #          
export DTO_MIN_BYTES=8192 # 65536  # 28672 #   16384  #              32768  #  16384  #
export DTO_CPU_SIZE_FRACTION=0.33 # 0.64 # 0.0  #0.60  # 0.60 #
export DTO_AUTO_ADJUST_KNOBS=1
export DTO_DSA_CC=1
export DTO_LOG_LEVEL=0
export DTO_AUTOTUNE_EXCLUDE_FAILED=1
export DTO_OVERLAPPING_MEMMOVE_ACTION=1  # 0-CPU, 1-DSA
export LD_LIBRARY_PATH=./   # dto is linked into the micro, but need to set ld_library_path to find libdto.so

num_threads=2  # Don't forget to adjust the taskset cpu list below
size=800 # size in KB
num_iter=10000000
total_buff_size=4194304  # total buffer size in KB (this gets divided into buffers of 'size' KB each)
perc_in_cache=0

taskset -c 56-57 ./dto-test-settable-size5 $num_threads 0 2 $size $num_iter  $total_buff_size 1 1 0 0 0 1 0 0 0 0 $perc_in_cache 0
