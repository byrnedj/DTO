#!/bin/bash
export DTO_USESTDC_CALLS=0
export DTO_COLLECT_STATS=0
export DTO_COLLECT_ALG_STATS=0
export DTO_COLLECT_ALG_STATS_LATEST_UPDATES=1
export DTO_COLLECT_ALG_STATS_LATEST_CPUFRACTS=1
export DTO_STATS_PREFIX=''
export DTO_STATS_OUTPUT_TYPE=1

#export DTO_NUM_AUTOTUNE_INSTANCES=1
export DTO_PER_OP_AUTOTUNE_INSTANCES=0
export DTO_NUM_WQS=8

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

export DTO_LOG_LEVEL=0
export DTO_MAKE_ADJ=0

export DTO_SLEEP_DELAY_NS=100000
export DTO_SLEEP_DELAY_US=1

export DTO_STATS_NUM_WARMUP_OPS=0  # 300000
export DTO_AUTOTUNE_EXCLUDE_FAILED=1

export LD_LIBRARY_PATH=./
export DTO_OVERLAPPING_MEMMOVE_ACTION=1  # 0-CPU, 1-DSA


#declare -a threads=(1 8 16 24 32 40 48)
#starting_cpu=56

declare -a delays=(1 3 5 7)
declare -a threads=(56 64 72 80 88 96 102 110)
starting_cpu=0

for delay in "${delays[@]}"
do
    echo "delay: $delay"
    for thread in "${threads[@]}"
    do
        ending_cpu=$((starting_cpu + thread))
        echo "threads: $thread"
        for (( i=1; i<=10; i++ )); do
            #echo "taskset -c $starting_cpu-$ending_cpu ./dto-test-settable-size5-localq $thread  0 2 128 1000000 2097152 1 1 0 0 0 1 0 0 0 0"
            taskset -c $starting_cpu-$ending_cpu ./dto-test-settable-size5-localq $thread  $delay 2 128 10000000 2097152 1 1 0 0 0 1 0 0 0 0
        done
    done
done

