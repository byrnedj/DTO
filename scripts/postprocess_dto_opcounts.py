# ==========================================================================
# Copyright (C) 2024 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==========================================================================

import subprocess, os, time, re, time, sys


bg_command = []
dto_command_base = ['/home/jjsydir/internal_dto/projects.research.dto_dev/dto-test-settable-size']
run_taskset = True
python_output = False

bg_name = 'none'

nums_threads = [10,30,40,50]  # [20] #[1, 10]  #, 16, 32]   #[1, 2, 4, 8, 16, 32]  #[32]  #[2, 4, 8, 16] #  

num_iter = 100000000000

percentages =   ['nodsa',  0.0]  # ['nodsa', 'auto', 0.0, 0.1, 0.33]  # ['auto']  #
percentage_names = ['nodsa',  'cpu0']

operations = [1] # [1,2,4]
op_sizes = [16,32,64,128,256]  # [512, 1024, 2048]  # [8,16,32,64,128,256]




results_dir = '/home/jjsydir/internal_dto/projects.research.dto_dev/scripts/results/emon_test_settable_013125_141749'
outfile = os.path.join(results_dir,'dto_opcounts_dict3.py')
infile = os.path.join(results_dir,'dto_opcounts.txt')
of = open(outfile,'w')
with open(infile,'r') as inf:
    input_lines = inf.readlines()
current = 0


of.write('op_counts = {\n')

for num_threads in nums_threads:
    for op_size in op_sizes:
        for mem_op in operations:
            for perc, perc_name in zip(percentages, percentage_names):

                if current == len(input_lines):
                    print("error not enough lines in input file")
                    sys.exit(0)

                line = input_lines[current].strip()
                current += 1

                pieces = line.split()
                count = int(pieces[1])
                
                case = "{}_{}_{}_{}".format(num_threads,op_size,mem_op,perc_name)
                of.write("'{}':{},\n".format(case,count))
                print('{}:{}'.format(case,count))

of.write('}\n')
of.close()


