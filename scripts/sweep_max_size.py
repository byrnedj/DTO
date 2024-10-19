# ==========================================================================
# Copyright (C) 2024 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==========================================================================

import subprocess, os, time

MEMSET = 1
MEMCOPY = 2    
MEMMOVE = 4       
MEMCMP = 8

bg_command = []
dto_command_base = ['../dto-test-settable-size-dev']
run_perf = False
cores = []

bg_name = 'none'
dto_name = 'dtotest-settable'

num_threads=2
num_dsas=1

tx_sizes = [2048, 128]

#tx_sizes = [1024]

min_size = 8*1024 
max_size = 2097152
size_step = 2
num_reps = 3

memops = MEMSET  | MEMCOPY  # memset followed by memcpy

print('memops {}'.format(memops))

perf_command = 'perf stat -e dsa0/event=0x1,event_category=0x0/,dsa2/event=0x1,event_category=0x0/,dsa4/event=0x1,event_category=0x0/,dsa6/event=0x1,event_category=0x0/,dsa0/event=0x1,event_category=0x1/,dsa2/event=0x1,event_category=0x1/,dsa4/event=0x1,event_category=0x1/,dsa6/event=0x1,event_category=0x1/,dsa0/event=0x2,event_category=0x1/,dsa2/event=0x2,event_category=0x1/,dsa4/event=0x2,event_category=0x1/,dsa6/event=0x2,event_category=0x1/'

#if len(cores) > 0:
#    if not type(cores[0]) == str:
#        cores_str = ['{}'.format(c) for c in cores]
#        cores = cores_str
#    dto_command = 'numactl -C ' + ','.join(cores) + ' ' + dto_command


#if run_perf:
#    dto_command = perf_command + ' ' + dto_command

tx_size = '_'.join([str(size) for size in tx_sizes])
name = 'sweepmaxsize_{}_{}_{}_{}_{}_{}_{}'.format(dto_name,num_threads,num_dsas, tx_size, min_size,max_size,size_step)   

base_env = os.environ.copy()
dto_env = os.environ.copy()

dto_env['DTO_USESTDC_CALLS']='0'
dto_env['DTO_COLLECT_STATS']='1'


dto_env['DTO_STATS_OUTPUT_TYPE']='1'   # 0 for text, 1 for python dict
dto_env['DTO_COLLECT_ALG_STATS']='1'
dto_env['DTO_WAIT_METHOD']='yield'
dto_env['DTO_MIN_BYTES']='8192'
dto_env['DTO_CPU_SIZE_FRACTION']='0.33'
dto_env['DTO_AUTO_ADJUST_KNOBS']='1'
dto_env['DTO_MAKE_ADJ'] = '1'
dto_env['DTO_DSA_CC']='1'
dto_env['LD_PRELOAD']='' #'./libdto.so.1.0'

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

results_root = './results'

sync_id = time.strftime('%m%d%y_%H%M%S', time.localtime())
results_dir = os.path.join(results_root,name+'_'+sync_id)
if not os.path.exists(results_dir):
    os.makedirs(results_dir)


bg_command = []
if bg_command != []:
    bg_process = subprocess.Popen(bg_command, env=base_env)

size = min_size

if len(tx_sizes) == 1:
    rat = tx_sizes[0]/128
    num_iter = int(1000000/rat)
    dto_command = dto_command_base + [str(num_threads), str(memops), str(tx_sizes[0]), str(num_iter)]
elif len(tx_sizes) == num_threads:
    dto_command = dto_command_base + [str(num_threads), str(memops)]
    for tx_size in tx_sizes:
        dto_command.append(str(tx_size))
        rat = tx_size/128
        num_iter = int(1000000/rat)
        dto_command.append(str(num_iter))

print(dto_command)



while size <= max_size:
    print("processing max_size {}".format(size))
    
    dto_env['DTO_MAX_BYTES']=str(size)

    fn= 'results_{}.py'.format(size)
    with open(os.path.join(results_dir,fn),'w') as f:

        f.write('results = {{{} : ['.format(size))
        f.flush()

        for i in range(num_reps):
            print('step {} out of {}'.format(i+1,num_reps))
            dto_process = subprocess.Popen(dto_command, env=dto_env, stdout=f, stderr=f) 
            ret = dto_process.wait()
            time.sleep(5)
            f.flush()
            f.write(',')
            f.flush()
        f.write(']}')

    size *= size_step
    #break

if bg_command != []:
    bg_process.kill()

