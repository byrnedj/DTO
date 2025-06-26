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

bg_command = []
dto_command_base = ['../dto-test-settable-size-dev']
run_perf = False
run_taskset = True
python_output = True

dto_name = 'dto-test-settable-size-dev'

num_threads=1
num_dsas=1

min_size = 8
max_size = 2048
size_step = 2
#sizes = [2**x for x in range(3,12)]
#sizes = [2**x for x in range(5,11)]
#sizes = [2**x for x in range(3,11)]
sizes = [8]


#percentages = [0.0, 0.1, 0.33]
percentages = ['auto', 'nodsa', 0.0] #[0.1, 0.3, 0.5, 0.7 ]

mem_ops = [MEMSET,MEMCOPY] #,MEMMOVE]
mem_op_names = ['set', 'cpy'] #, 'mov']

num_iter = 10000000  #1000000

#perf_command = 'perf stat -e dsa0/event=0x1,event_category=0x0/,dsa2/event=0x1,event_category=0x0/,dsa4/event=0x1,event_category=0x0/,dsa6/event=0x1,event_category=0x0/,dsa0/event=0x1,event_category=0x1/,dsa2/event=0x1,event_category=0x1/,dsa4/event=0x1,event_category=0x1/,dsa6/event=0x1,event_category=0x1/,dsa0/event=0x2,event_category=0x1/,dsa2/event=0x2,event_category=0x1/,dsa4/event=0x2,event_category=0x1/,dsa6/event=0x2,event_category=0x1/'
perf_command = ['perf', 'stat']

taskset_command = ['taskset', '-c', '56-57']

#if len(cores) > 0:
#    if not type(cores[0]) == str:
#        cores_str = ['{}'.format(c) for c in cores]
#        cores = cores_str
#    dto_command = 'numactl -C ' + ','.join(cores) + ' ' + dto_command


#if run_perf:
#    dto_command = perf_command + ' ' + dto_command


name = 'sweeptxsize_python_stats'
#name = 'sweeptxsize_perf_stats-enabled'
#name = 'sweeptxsize_perf_stats-disabled'

#name = 'sweeptxsize_nostatscomp_waitsleep_cpufract0.1_minsize8K'
#name = 'sweeptxsize_nostatscomp_nodsa_minsize8K'
#name = 'test'

base_env = os.environ.copy()
dto_env = os.environ.copy()

dto_env['DTO_USESTDC_CALLS']='0'
dto_env['DTO_COLLECT_STATS']='1'
dto_env['DTO_WAIT_METHOD']='yield' # 'sleep' #   
dto_env['DTO_MIN_BYTES']='8192'  #'65536'#    
#dto_env['DTO_CPU_SIZE_FRACTION']= '0.1' # '0.33' #
#dto_env['DTO_AUTO_ADJUST_KNOBS']='0'
dto_env['DTO_MAKE_ADJ'] = '1'
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


results_root = './results'

sync_id = time.strftime('%m%d%y_%H%M%S', time.localtime())
results_dir = os.path.join(results_root,name+'_'+sync_id)
if not os.path.exists(results_dir):
    os.makedirs(results_dir)


for mem_op, op_name in zip(mem_ops,mem_op_names):

    for perc in percentages:

        if perc == 'nodsa':
            dto_env['DTO_USESTDC_CALLS']='1'
        elif perc == 'auto':
            dto_env['DTO_USESTDC_CALLS']='0'
            dto_env['DTO_AUTO_ADJUST_KNOBS']='1'
            dto_env['DTO_CPU_SIZE_FRACTION']='0.33'
        else:
            dto_env['DTO_USESTDC_CALLS']='0'
            dto_env['DTO_AUTO_ADJUST_KNOBS']='0'
            dto_env['DTO_CPU_SIZE_FRACTION']=str(perc)

        for size in sizes:
            if python_output:
                fn= 'results_{}_{}_{}_{}.py'.format(name,op_name, str(perc).replace('.','-'), size)
            else:
                fn= 'results_{}_{}_{}_{}.txt'.format(name,op_name, str(perc).replace('.','-'), size)
            print("processing {} cpu_perc {} size {}".format(op_name, perc, size))
            with open(os.path.join(results_dir,fn),'w') as f:
                dto_command = dto_command_base + [str(num_threads), str(0), str(mem_op), str(size), str(num_iter)]
                if run_taskset:
                    dto_command = taskset_command + dto_command
                if run_perf:
                    dto_command = perf_command + dto_command

                dto_process = subprocess.Popen(dto_command, env=dto_env, stdout=f, stderr=f) 
                ret = dto_process.wait()
                time.sleep(5)
                f.flush()
                time.sleep(1)
           

