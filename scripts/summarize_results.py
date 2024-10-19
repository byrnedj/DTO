# ==========================================================================
# Copyright (C) 2024 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==========================================================================

import glob
import os
import re

directory = './results/sweeptxsize_perc_minsize8K_1M_iterations_101124_115605'

files = [file for file in sorted(glob.glob(os.path.join(directory, 'results_*.py')))]

for file in files:
    _,fn = os.path.split(file)
    fn,_ = os.path.splitext(fn)
    out_file = os.path.join(directory,fn+'_summary.txt')
    
    
    with open(file, "r") as origin_file:
        with open(out_file, "w") as out:
            for line in origin_file:
                line1 = re.findall(r'processing', line)
                if line1:
                    out.write(line)
                    print(line1)
                else:
                    line1 = re.findall(r'cycles', line)
                    if line1:
                        out.write(line)
                        print(line1)