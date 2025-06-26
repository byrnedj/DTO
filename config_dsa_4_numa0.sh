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
accel-config disable-device dsa8
accel-config disable-device dsa10
accel-config disable-device dsa12
accel-config disable-device dsa14

accel-config load-config -c /home/jjsydir/internal_dto/projects.research.dto_dev/dto-4-dsa-numa0.conf

accel-config enable-device dsa0
accel-config enable-device dsa2
accel-config enable-device dsa4
accel-config enable-device dsa6

accel-config enable-wq dsa0/wq0.0
accel-config enable-wq dsa2/wq2.0
accel-config enable-wq dsa4/wq4.0
accel-config enable-wq dsa6/wq6.0

chmod o+rw /dev/dsa/wq0.0
chmod o+rw /dev/dsa/wq2.0
chmod o+rw /dev/dsa/wq4.0
chmod o+rw /dev/dsa/wq6.0


