#!/bin/bash


#export DTO_USESTDC_CALLS=0
#export DTO_COLLECT_STATS=1
#export DTO_COLLECT_ALG_STATS=0
#export DTO_COLLECT_ALG_STATS_LATEST_UPDATES=1
#export DTO_COLLECT_ALG_STATS_LATEST_CPUFRACTS=1
#export DTO_STATS_PREFIX=''
#export DTO_STATS_OUTPUT_TYPE=2
#export DTO_PER_OP_AUTOTUNE_INSTANCES=0
#export DTO_NUM_WQS=8
#export DTO_IS_NUMA_AWARE=0
#export DTO_OPPOSITE_NUMA=0
#export DTO_WAIT_METHOD=busypoll  # yield  #       
#export DTO_MIN_BYTES=8192 # 65536  # 28672 #   16384  #              32768  #  16384  #
#export DTO_MAX_BYTES=2097152  #  8192  # 32768  #65536 #16384  #
#export DTO_CPU_SIZE_FRACTION=0.15  #0.33 # 0.64 # 0.0  #0.60  # 0.60 #
#export DTO_AUTO_ADJUST_KNOBS=0
#export DTO_DSA_CC=1
#export DTO_LOG_LEVEL=0
#export DTO_MAKE_ADJ=1
#export DTO_SLEEP_DELAY_NS=100000
#export DTO_SLEEP_DELAY_US=1

export LD_LIBRARY_PATH=./

export DTO_USESTDC_CALLS=0
export DTO_COLLECT_STATS=1
export DTO_IS_NUMA_AWARE=0
export DTO_DSA_CC=1
export DTO_WAIT_METHOD=yield
export DTO_MIN_BYTES=32768
export DTO_CPU_SIZE_FRACTION=0.15
export DTO_AUTO_ADJUST_KNOBS=0



#old
#/usr/bin/time taskset -c 56 ./dto-test-settable-size5 1 0 2 781   1000000    8388608 1 1 0 0 0 1 0 0  #

#new
/usr/bin/time taskset -c 56 ./dto-test-settable-size-dev5-2 1 0 2 781   1000000    8388608 1 1 0 0 0 1 0 0  #

