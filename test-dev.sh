#!/bin/bash
# ==========================================================================
# Copyright (C) 2023 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==========================================================================

accel-config disable-device dsa0
accel-config disable-device dsa2
accel-config disable-device dsa4
accel-config disable-device dsa6

accel-config load-config -c ./dto-4-dsa.conf

accel-config enable-device dsa0
accel-config enable-device dsa2
accel-config enable-device dsa4
accel-config enable-device dsa6

accel-config enable-wq dsa0/wq0.0
accel-config enable-wq dsa2/wq2.0
accel-config enable-wq dsa4/wq4.0
accel-config enable-wq dsa6/wq6.0

export DTO_USESTDC_CALLS=0

export DTO_COLLECT_STATS=1
export DTO_STATS_OUTPUT_TYPE=1   # 0 for text, 1 for python dict
export DTO_COLLECT_ALG_STATS=1


#export DTO_WAIT_METHOD=umwait
export DTO_WAIT_METHOD=yield
#export DTO_WAIT_METHOD=sleep
export DTO_MIN_BYTES=28672 # 8192 #  16384  #              32768  #  16384  #
export DTO_MAX_BYTES=2097152  #  8192  # 32768  #65536 #16384  #
export DTO_CPU_SIZE_FRACTION=0.0
export DTO_AUTO_ADJUST_KNOBS=1
export DTO_MAKE_ADJ=1
export DTO_DSA_CC=1

export DTO_LOG_LEVEL=3   #2->stats only 3->stats and trace 


export DTO_SLEEP_DELAY_US=1




# Run dto-test without DTO library
#/usr/bin/time ./dto-test-wodto

# Run dto-test with DTO library using LD_PRELOAD method
#export LD_PRELOAD=./libdto.so.1.0
#/usr/bin/time ./dto-test-wodto

# Run dto-test with DTO library using "re-compile with DTO" method
# (i.e., without LD_PRELOAD)
export LD_LIBRARY_PATH=./
#./dto-test-distribution 1 10000000 ./all_ops.csv
#/usr/bin/time ./dto-test-distribution 1 50000 250 ./ops_over_8K.csv

#/usr/bin/time  ./dto-test
#perf stat ./dto-test

/usr/bin/time  ./dto-test-dev

#perf stat ./dto-test-distribution-nopause 10 1000000 ./all_ops.csv
#perf stat ./dto-test-distribution-nopause 10 1000000 ./ops_over_8K.csv
#perf stat ./dto-test-settable-size 1 0 16 1000000

#/usr/bin/time ./dto-test-settable-size 1 8 100000

#perf stat ./dto-test-distribution-nopause 5000000000 ./all_ops.csv
#./dto-test-distribution-nopause 100000000 ./all_ops.csv
#perf stat ./dto-test-distribution-nopause 1000000 ./test_ops.csv

#./dto-test-distribution-nopause 100 ./all_ops.csv


# Run dto-test with DTO and get DSA perfmon counters
#perf stat -e dsa0/event=0x1,event_category=0x0/,dsa2/event=0x1,event_category=0x0/,dsa4/event=0x1,event_category=0x0/,dsa6/event=0x1,event_category=0x0/,dsa0/event=0x1,event_category=0x1/,dsa2/event=0x1,event_category=0x1/,dsa4/event=0x1,event_category=0x1/,dsa6/event=0x1,event_category=0x1/,dsa0/event=0x2,event_category=0x1/,dsa2/event=0x2,event_category=0x1/,dsa4/event=0x2,event_category=0x1/,dsa6/event=0x2,event_category=0x1/ /usr/bin/time ./dto-test
