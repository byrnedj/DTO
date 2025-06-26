# ==========================================================================
# Copyright (C) 2024 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==========================================================================

import subprocess, os, time


bg_command = []
dto_command = ['../dto-test']
run_perf = False
cores = []

bg_name = 'none'
dto_name = 'dtotest'

min_perc = 0 
max_perc = 20   #100
perc_step = 10

perf_command = 'perf stat -e dsa0/event=0x1,event_category=0x0/,dsa2/event=0x1,event_category=0x0/,dsa4/event=0x1,event_category=0x0/,dsa6/event=0x1,event_category=0x0/,dsa0/event=0x1,event_category=0x1/,dsa2/event=0x1,event_category=0x1/,dsa4/event=0x1,event_category=0x1/,dsa6/event=0x1,event_category=0x1/,dsa0/event=0x2,event_category=0x1/,dsa2/event=0x2,event_category=0x1/,dsa4/event=0x2,event_category=0x1/,dsa6/event=0x2,event_category=0x1/'

#if len(cores) > 0:
#    if not type(cores[0]) == str:
#        cores_str = ['{}'.format(c) for c in cores]
#        cores = cores_str
#    dto_command = 'numactl -C ' + ','.join(cores) + ' ' + dto_command


#if run_perf:
#    dto_command = perf_command + ' ' + dto_command


name = 'sweepcpuperc_{}_{}_{}_{}_{}'.format(dto_name,bg_name,min_perc,max_perc,perc_step)   #.format(bitrate, start_rate, end_rate, rate_step, core_freq, uncore_freq)

base_env = os.environ.copy()
dto_env = os.environ.copy()

dto_env['DTO_USESTDC_CALLS']=0
dto_env['DTO_COLLECT_STATS']=1
dto_env['DTO_WAIT_METHOD']='yield'
dto_env['DTO_MIN_BYTES']=8192
dto_env['DTO_CPU_SIZE_FRACTION']=min_perc
dto_env['DTO_AUTO_ADJUST_KNOBS']=0
dto_env['DTO_DSA_CC']=1
dto_env['LD_PRELOAD']='./libdto.so.1.0'

dto_env['DTO_IS_NUMA_AWARE']=0
#dto_env['DTO_WQ_LIST']="". 
dto_env['DTO_DSA_MEMCPY']=1
dto_env['DTO_DSA_MEMMOVE']=1
dto_env['DTO_DSA_MEMSET']=1
dto_env['DTO_DSA_MEMCMP']=1
dto_env['DTO_UMWAIT_DELAY']= 100000
dto_env['DTO_LOG_FILE']=''
dto_env['DTO_LOG_LEVEL']=0


results_root = './results'

sync_id = time.strftime('%m%d%y_%H%M%S', time.localtime())
results_dir = os.path.join(results_root,name+'_'+sync_id)
if not os.path.exists(results_dir):
    os.makedirs(results_dir)


bg_command = []
if bg_command != []:
    bg_process = subprocess.Popen(bg_command, env=base_env)

for perc in range(min_perc, max_perc+perc_step, perc_step):
    dto_env['DTO_CPU_SIZE_FRACTION']=perc/100

    dto_process = subprocess.Popen(dto_command, env=dto_env)
    #st = time.time()

    ret = dto_process.wait()
    #ed = time.time()

    dto_output = dto_process.stdout

    fn= 'results_{}.txt'.format(perc)

    with open(os.path.join(results_dir,fn),'w') as f:
        f.write(dto_output) 

if bg_command != []:
    bg_process.kill()

