#!/bin/bash
# ==========================================================================
# Copyright (C) 2023 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==========================================================================

export DTO_USESTDC_CALLS=0
export DTO_COLLECT_STATS=0
#export DTO_WAIT_METHOD=umwait
export DTO_WAIT_METHOD=yield
#export DTO_WAIT_METHOD=sleep
export DTO_MIN_BYTES=8192 # 28672 #   16384  #              32768  #  16384  #
export DTO_MAX_BYTES=2097152  #  8192  # 32768  #65536 #16384  #
export DTO_CPU_SIZE_FRACTION=0.33
export DTO_AUTO_ADJUST_KNOBS=0
export DTO_MAKE_ADJ=1
export DTO_DSA_CC=1

export DTO_LOG_LEVEL=0
export DTO_MAKE_ADJ=0

export DTO_SLEEP_DELAY_NS=100000
export DTO_SLEEP_DELAY_US=1

export LD_PRELOAD=/home/jjsydir/internal_dto/projects.research.dto_dev/libdto.so.1.0
