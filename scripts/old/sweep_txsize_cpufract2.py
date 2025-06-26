# ==========================================================================
# Copyright (C) 2024 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==========================================================================

import subprocess, os, time, re, time

MEMSET = 1
MEMCOPY = 2    
MEMMOVE = 4       
MEMCMP = 8

dto_command_base = ['../dto-test-settable-size-dev']
run_perf = False
run_taskset = True
python_output = True

#results_dir = '/home/jjsydir/internal_dto/projects.research.dto_dev/scripts/results/sweeptxsize_perc_python_stats_2threads_022125_143621'
results_dir = '/home/jjsydir/internal_dto/projects.research.dto_dev/scripts/results/sweeptxsize_perc_python_stats_4threads_022425_095836'
#results_dir = ''

num_threads=4
num_dsas=1  # 8

#sizes = [2**x for x in range(3,12)]
sizes = [2**x for x in range(5,11)]  #32-1024
#sizes = [2**x for x in range(6,11)]  #64-1024
#sizes = [1024,1280,1536,1792,2048]

percentages = [ 0.8, 0.9, 0.99] #, 0.7 ]  #[0.7]  #[0.0, 0.1, 0.33] #[0.3, 0.35, 0.4, 0.45, 0.5 ] [0.8, 0.9, 0.99] #[0.75, 0.8, 0.85, 0.9, 0.95, 0.99]

mem_ops = [MEMSET,MEMCOPY] #,MEMMOVE]
mem_op_names = ['set', 'cpy'] #, 'mov']

num_iter = 10000000  #1000000

name = 'sweeptxsize_perc_python_stats_{}threads'.format(num_threads)

results_root = './results'

if results_dir == '':
    sync_id = time.strftime('%m%d%y_%H%M%S', time.localtime())
    results_dir = os.path.join(results_root,name+'_'+sync_id)
    if not os.path.exists(results_dir):
        os.makedirs(results_dir)    

taskset_command = ['taskset', '-c', '56-{}'.format(56+num_threads)]

#perf_command = 'perf stat -e dsa0/event=0x1,event_category=0x0/,dsa2/event=0x1,event_category=0x0/,dsa4/event=0x1,event_category=0x0/,dsa6/event=0x1,event_category=0x0/,dsa0/event=0x1,event_category=0x1/,dsa2/event=0x1,event_category=0x1/,dsa4/event=0x1,event_category=0x1/,dsa6/event=0x1,event_category=0x1/,dsa0/event=0x2,event_category=0x1/,dsa2/event=0x2,event_category=0x1/,dsa4/event=0x2,event_category=0x1/,dsa6/event=0x2,event_category=0x1/'
perf_command = ['perf', 'stat']

base_env = os.environ.copy()
dto_env = os.environ.copy()

dto_env['DTO_NUM_WQS']='{}'.format(num_dsas)

dto_env['DTO_USESTDC_CALLS']='0'
dto_env['DTO_COLLECT_STATS']='1'
dto_env['DTO_WAIT_METHOD']='yield' # 'sleep' #   
dto_env['DTO_MIN_BYTES']='8192'  #'65536'#    
#dto_env['DTO_CPU_SIZE_FRACTION']= '0.1' # '0.33' #
dto_env['DTO_AUTO_ADJUST_KNOBS']='0'
dto_env['DTO_MAKE_ADJ'] = '0'
dto_env['DTO_DSA_CC']='1'
dto_env['LD_PRELOAD']='' #'./libdto.so.1.0'
dto_env['DTO_MAX_BYTES']='2097152'

dto_env['DTO_IS_NUMA_AWARE']='0'
#dto_env['DTO_WQ_LIST']="". 
dto_env['DTO_DSA_MEMCPY']='1'
dto_env['DTO_DSA_MEMMOVE']='1'
dto_env['DTO_DSA_MEMSET']='1'
dto_env['DTO_DSA_MEMCMP']='1'
dto_env['DTO_UMWAIT_DELAY']= '100000'
#dto_env['DTO_LOG_FILE']=''
dto_env['DTO_LOG_LEVEL']='2'
dto_env['LD_LIBRARY_PATH']='../'

if python_output:
    #dto_env['DTO_STATS_PREFIX']="'''"
    dto_env['DTO_STATS_PREFIX']=""
    dto_env['DTO_STATS_OUTPUT_TYPE']='1'
else:
    dto_env['DTO_STATS_OUTPUT_TYPE']='0'


for mem_op, op_name in zip(mem_ops,mem_op_names):

    for perc in percentages:

        dto_env['DTO_CPU_SIZE_FRACTION']=str(perc)

        for size in sizes:

            fn= 'results_{}_{}_{}_cpufrace{}.py'.format(name,op_name, perc, size)
            print("processing {} cpu_perc {} size {}".format(op_name, perc, size))
            with open(os.path.join(results_dir,fn),'w') as f:
                dto_command = dto_command_base + [str(num_threads), str(0), str(mem_op), str(size), str(num_iter)]
                #print(dto_command)
                if run_taskset:
                    dto_command = taskset_command + dto_command
                if run_perf:
                    dto_command = perf_command + dto_command

                dto_process = subprocess.Popen(dto_command, env=dto_env, stdout=f, stderr=f) 
                ret = dto_process.wait()
                time.sleep(5)
                f.flush()
                time.sleep(1)
           
"""
percentages = [ 0.8, 0.9, 0.99] #, 0.7 ]  #[0.7]  #[0.0, 0.1, 0.33] #[0.3, 0.35, 0.4, 0.45, 0.5 ] [0.8, 0.9, 0.99] #[0.75, 0.8, 0.85, 0.9, 0.95, 0.99]

mem_ops = [MEMSET] #,MEMMOVE]
mem_op_names = ['set'] #, 'mov']

for mem_op, op_name in zip(mem_ops,mem_op_names):

    for perc in percentages:

        dto_env['DTO_CPU_SIZE_FRACTION']=str(perc)

        for size in sizes:

            fn= 'results_{}_{}_{}_cpufrace{}.py'.format(name,op_name, perc, size)
            print("processing {} cpu_perc {} size {}".format(op_name, perc, size))
            with open(os.path.join(results_dir,fn),'w') as f:
                dto_command = dto_command_base + [str(num_threads), str(0), str(mem_op), str(size), str(num_iter)]
                #print(dto_command)
                if run_taskset:
                    dto_command = taskset_command + dto_command
                if run_perf:
                    dto_command = perf_command + dto_command

                dto_process = subprocess.Popen(dto_command, env=dto_env, stdout=f, stderr=f) 
                ret = dto_process.wait()
                time.sleep(5)
                f.flush()
                time.sleep(1)

"""