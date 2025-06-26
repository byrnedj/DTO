# ==========================================================================
# Copyright (C) 2024 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==========================================================================

import subprocess, os, time, re, time, json

import argparse

# Create an ArgumentParser object
parser = argparse.ArgumentParser()

# Add arguments
parser.add_argument('--output-type', type=str, default='perf', help='output type: perf | DTO_python | emon')
parser.add_argument('--run-name', type=str, default='test', help='name to be used in output directory')
parser.add_argument('--num-threads', type=int, default=1, help='number of threads')

# Parse the arguments
args = parser.parse_args()


MEMSET = 1
MEMCOPY = 2    
MEMMOVE = 4       
MEMCMP = 8

dto_command_base = ['../dto-test-settable-size-dev']

run_taskset = True

# Output choices are: 'DTO_python' or  'emon'  or 'perf'
output_type = args.output_type  #'perf'  #'emon' # 'DTO_python'  #  'perf'

#results_dir = '/home/jjsydir/internal_dto/projects.research.dto_dev/scripts/results/sweepalltest_test_perf_030425_155904'
results_dir = ''

run_name = args.run_name  #'test_perf'

num_threads=args.num_threads
num_dsas=8
num_iter = 10000000  #1000000
taskset_startcpu = 56

sizes = [2**x for x in range(3,11)]   #8-1024
#sizes = [2**x for x in range(5,11)]  #32-1024
#sizes = [2**x for x in range(6,11)]  #64-1024
#sizes = [1024,1280,1536,1792,2048]

percentages = ['auto', 'nodsa'] # [ 0.8, 0.9, 0.99] #, 0.7 ]  #[0.7] #[0.0, 0.1, 0.33] #[0.3, 0.35, 0.4, 0.45, 0.5 ] [0.8, 0.9, 0.99] #[0.75, 0.8, 0.85, 0.9, 0.95, 0.99]

mem_ops = [MEMSET,MEMCOPY] #,MEMMOVE]
mem_op_names = ['set', 'cpy'] #, 'mov']

numa_configs = ['unaware', 'buffer', 'cpu', 'antibuffer', 'anticpu']

config = {
    "output_type":output_type,
    "num_threads":num_threads,
    "num_dsas":num_dsas,
    "num_iter":num_iter,
    "run_taskset":run_taskset,
    "taskset_startcpu":taskset_startcpu,
    "sizes":sizes,
    "percentages":percentages,
    "mem_ops":mem_ops,
    "numa_configs":numa_configs
}

name = 'sweepalltest_{}'.format(run_name)

results_root = './results'

if results_dir == '':
    sync_id = time.strftime('%m%d%y_%H%M%S', time.localtime())
    results_dir = os.path.join(results_root,name+'_'+sync_id)
    if not os.path.exists(results_dir):
        os.makedirs(results_dir)   

    with open(os.path.join(results_dir,'{}_config.json'.format(name)), 'w') as f:
        json.dump(config, f)

taskset_command = ['taskset', '-c', '{}-{}'.format(taskset_startcpu,taskset_startcpu+num_threads)]

#perf_command = 'perf stat -e dsa0/event=0x1,event_category=0x0/,dsa2/event=0x1,event_category=0x0/,dsa4/event=0x1,event_category=0x0/,dsa6/event=0x1,event_category=0x0/,dsa0/event=0x1,event_category=0x1/,dsa2/event=0x1,event_category=0x1/,dsa4/event=0x1,event_category=0x1/,dsa6/event=0x1,event_category=0x1/,dsa0/event=0x2,event_category=0x1/,dsa2/event=0x2,event_category=0x1/,dsa4/event=0x2,event_category=0x1/,dsa6/event=0x2,event_category=0x1/'
perf_command = ['perf', 'stat']

emon_command = ['sudo', '/opt/intel/sep/bin64/emon', '-collect-edp', 'edp_file=/opt/intel/sep/config/edp/sapphirerapids_server_events.txt']


base_env = os.environ.copy()
dto_env = os.environ.copy()

dto_env['DTO_NUM_WQS']='{}'.format(num_dsas)

dto_env['DTO_USESTDC_CALLS']='0'
dto_env['DTO_COLLECT_STATS']='0'
dto_env['DTO_STATS_OUTPUT_TYPE']='0'
dto_env['DTO_WAIT_METHOD']='yield' # 'sleep' #   
dto_env['DTO_MIN_BYTES']='8192'  #'65536'#    
#dto_env['DTO_CPU_SIZE_FRACTION']= '0.1' # '0.33' #
dto_env['DTO_AUTO_ADJUST_KNOBS']='0'
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

if output_type == 'DTO_python':
    #dto_env['DTO_STATS_PREFIX']="'''"
    dto_env['DTO_STATS_PREFIX']=""
    dto_env['DTO_STATS_OUTPUT_TYPE']='1'
    dto_env['DTO_COLLECT_STATS']='1'
elif output_type == 'perf':
    dto_env['DTO_STATS_OUTPUT_TYPE']='0'
    dto_env['DTO_COLLECT_STATS']='1'

for mem_op, op_name in zip(mem_ops,mem_op_names):
    for numa_config in numa_configs:
        dto_env['DTO_OPPOSITE_NUMA']='0'
        if numa_config == 'unaware':
                dto_env['DTO_IS_NUMA_AWARE']='0'
        elif numa_config == 'buffer':
                dto_env['DTO_IS_NUMA_AWARE']='1'
        elif numa_config == 'cpu':
                dto_env['DTO_IS_NUMA_AWARE']='2'
        elif numa_config == 'antibuffer':
                dto_env['DTO_IS_NUMA_AWARE']='1'
                dto_env['DTO_OPPOSITE_NUMA']='1'
        elif numa_config == 'anticpu':
                dto_env['DTO_IS_NUMA_AWARE']='2'
                dto_env['DTO_OPPOSITE_NUMA']='1'

        for perc in percentages:

            if perc == 'nodsa' and numa_config != 'unaware':
                continue

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

                print('processing {} {} {} {}K'.format(op_name,numa_config,perc,size))

                output_name = '{}_{}_{}_{}K'.format(op_name,numa_config,perc.replace(".", "-"),size)

                if os.path.exists(os.path.join(results_dir,'{}_DTO_config.json'.format(output_name))):
                    continue

                with open(os.path.join(results_dir,'{}_DTO_config.json'.format(output_name)), 'w') as f:
                    json.dump(dto_env, f)

                dto_command = dto_command_base + [str(num_threads), str(0), str(mem_op), str(size), str(num_iter)]
                #print(dto_command)

                if output_type == 'emon':
                    outfile = os.path.join(results_dir,'{}_{}.emon.txt'.format(name,output_name))
                    errfile = os.path.join(results_dir,'{}_{}.stderr.txt'.format(name,output_name))

                    of = open(outfile,'w')
                    ef = open(errfile, 'w')

                    dto_process = subprocess.Popen(dto_command, env=dto_env, stdout=1, stderr=1)
                    time.sleep(5)

                    emon_process = subprocess.Popen(emon_command, stdout=of, stderr=ef)
                    time.sleep(60)

                    kill_process = subprocess.Popen(['sudo', 'pkill', 'emon'])
                    kill_dto = subprocess.Popen(['pkill', dto_command_base[0].split('/')[-1]])
                    ret = kill_process.wait()
                    ret = kill_dto.wait()

                    emon_process.kill()
                    dto_process.kill()
                    of.close()
                    ef.close()

                else:
                    if output_type == 'DTO_python':
                        outfile = os.path.join(results_dir,'{}_{}.py'.format(name,output_name))
                    else:
                        outfile = os.path.join(results_dir,'{}_{}.txt'.format(name,output_name))
                    
                    with open(outfile,'w') as f:
                        
                        if run_taskset:
                            dto_command = taskset_command + dto_command
                        if output_type == 'perf':
                            dto_command = perf_command + dto_command

                        dto_process = subprocess.Popen(dto_command, env=dto_env, stdout=f, stderr=f) 
                        ret = dto_process.wait()
                        time.sleep(5)
                        f.flush()
                
                time.sleep(1)
            
