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
#declare -a starting_cpus=(55 52 48 44 40 36 32)
#declare -a ending_cpus=(56 59 63 67 71 75 79)


declare -a threads=(56 64 72 80 88 96 102 110)
declare -a starting_cpus=(28 24 20 16 12 8 5 1)
declare -a ending_cpus=(83 87 91 95 99 103 106 110)

echo "Local Q selector"
for (( i=7; i<8; i++ )); do
    thread=${threads[$i]}
    starting_cpu=${starting_cpus[$i]}
    ending_cpu=${ending_cpus[$i]}

    echo "threads: $thread"
    for (( j=1; j<=10; j++ )); do
        #echo "taskset -c $starting_cpu-$ending_cpu ./dto-test-settable-size5-localq $thread  0 2 128 1000000 2097152 1 1 0 0 0 1 0 0 0 0"
        taskset -c $starting_cpu-$ending_cpu ./dto-test-settable-size5-localq-timing $thread  0 2 128 10000000 2097152 1 1 0 0 0 1 0 0 0 0
    done
done

echo "Global Q selector"
for (( i=7; i<8; i++ )); do
    thread=${threads[$i]}
    starting_cpu=${starting_cpus[$i]}
    ending_cpu=${ending_cpus[$i]}

    echo "threads: $thread"
    for (( j=1; j<=10; j++ )); do
        #echo "taskset -c $starting_cpu-$ending_cpu ./dto-test-settable-size5-localq $thread  0 2 128 1000000 2097152 1 1 0 0 0 1 0 0 0 0"
        taskset -c $starting_cpu-$ending_cpu ./dto-test-settable-size5-globalq-timing $thread  0 2 128 10000000 2097152 1 1 0 0 0 1 0 0 0 0
    done
done

