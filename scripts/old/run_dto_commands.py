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
parser.add_argument('--input-filepath', type=str, default=None, help='file containing DTO commands')
parser.add_argument('--cfg-filepath', type=str, default=None, help='cofiguration file')
parser.add_argument('--output-type', type=str, default='perf', help='output type: perf | DTO_python | emon')
parser.add_argument('--run-name', type=str, default='test', help='name to be used in output directory')
parser.add_argument('--results-dirname', type=str, default=None, help='results directory')
parser.add_argument('--dry-run', action='store_true', help='perform a dryrun')
parser.add_argument('--overwrite', action='store_true', help='overwrite existing test results')
parser.add_argument('--alg-stats', action='store_true', help='collect algorithm stats')
parser.add_argument('--num-dsas', type=int, default=8, help='number of DSA to use')
parser.add_argument('--max-transaction-size', type=int, default=None, help='maximum transaction size (in KB) to use')

# Parse the arguments
args = parser.parse_args()


MEMSET = 1
MEMCOPY = 2    
MEMMOVE = 4       
MEMCMP = 8

numa_alloc_policy = { 'unaware': '0',
                      'same': '1',
                      'diff': '2'
}


run_taskset = True
dry_run = args.dry_run   #False
overwrite = args.overwrite
# Output choices are: 'DTO_python' or  'emon'  or 'perf'
output_type = args.output_type  #'perf'  #'emon' # 'DTO_python'  #  'perf'
input_filepath = args.input_filepath
cfg_filepath = args.cfg_filepath
results_dirname = args.results_dirname
run_name = args.run_name  #'test_perf'
alg_stats = args.alg_stats
num_dsas= args.num_dsas  #8
max_transaction_size = args.max_transaction_size

taskset_startcpu = 56




percentages =  ['auto', 'nodsa','0.0']  #['auto', 'nodsa'] # [ 0.8, 0.9, 0.99] #, 0.7 ]  #[0.7] #[0.0, 0.1, 0.33] #[0.3, 0.35, 0.4, 0.45, 0.5 ] [0.8, 0.9, 0.99] #[0.75, 0.8, 0.85, 0.9, 0.95, 0.99]

numa_configs = ['unaware']  #, 'buffer', 'cpu', 'antibuffer', 'anticpu']

cache_control_configs = [1] #[0,1]

wait_methods = ['yield']  #, 'umwait', 'busypoll']



config = {
    "output_type":output_type,
    "input_file": input_filepath,
    "num_dsas":num_dsas,
    "run_taskset":run_taskset,
    "taskset_startcpu":taskset_startcpu,
    "percentages":percentages,
    "numa_configs":numa_configs,
    "wait_methods": wait_methods,
    "cache_control_configs":cache_control_configs,
}

if cfg_filepath is not None:
    if not os.path.exists(cfg_filepath):
            print("error input file {} not found".format(cfg_filepath))
            sys.exit(0)

    with open(cfg_filepath, 'r') as f:
        loaded_config = json.load(f)

    if 'percentages' in loaded_config:
        percentages = config['percentages'] = loaded_config['percentages']

    if "wait_methods" in loaded_config:
        wait_methods = config["wait_methods"] = loaded_config["wait_methods"]

    if "cache_control_configs" in loaded_config:
        cache_control_configs = config["cache_control_configs"] = loaded_config["cache_control_configs"]
    
    if "numa_configs" in loaded_config:
        numa_configs = config["numa_configs"] = loaded_config["numa_configs"]

    
name = 'run_dtocommands_{}_{}'.format(run_name, output_type)

results_root = './results'

if not dry_run:
    if results_dirname is None:
        save_config = True
        sync_id = time.strftime('%m%d%y_%H%M%S', time.localtime())
        results_dirname = name+'_'+sync_id
    else:
        save_config = False
    
    results_dir = os.path.join(results_root,results_dirname)
    if not os.path.exists(results_dir):
        os.makedirs(results_dir)   

    if save_config:
        with open(os.path.join(results_dir,'{}_config.json'.format(name)), 'w') as f:
            json.dump(config, f, indent=4)

#perf_command = 'perf stat -e dsa0/event=0x1,event_category=0x0/,dsa2/event=0x1,event_category=0x0/,dsa4/event=0x1,event_category=0x0/,dsa6/event=0x1,event_category=0x0/,dsa0/event=0x1,event_category=0x1/,dsa2/event=0x1,event_category=0x1/,dsa4/event=0x1,event_category=0x1/,dsa6/event=0x1,event_category=0x1/,dsa0/event=0x2,event_category=0x1/,dsa2/event=0x2,event_category=0x1/,dsa4/event=0x2,event_category=0x1/,dsa6/event=0x2,event_category=0x1/'
perf_command = ['perf', 'stat']

emon_command = ['sudo', '/opt/intel/sep/bin64/emon', '-collect-edp', 'edp_file=/opt/intel/sep/config/edp/sapphirerapids_server_events.txt']


dto_commands = []
thread_counts = []
output_names = []

with open(input_filepath, "r") as f:
    for line in f:
        line=line.strip()
        if len(line) != 0:
            print('line: {}'.format(line))
            pieces = line.split(' ')
            if len(pieces) > 0 and pieces[0][0] != '#':
                dto_commands.append(pieces[:-1])
                thread_counts.append(int(pieces[1]))
                output_names.append(pieces[-1])


print(dto_commands)
print(thread_counts)
print(output_names)




base_env = os.environ.copy()
dto_env = os.environ.copy()

dto_env['DTO_NUM_WQS']='{}'.format(num_dsas)

if max_transaction_size is not None:
    dto_env['DTO_MAX_BYTES'] = max_transaction_size

dto_env['DTO_USESTDC_CALLS']='0'
dto_env['DTO_COLLECT_STATS']='0'
dto_env['DTO_STATS_OUTPUT_TYPE']='0'
dto_env['DTO_COLLECT_ALG_STATS']='0'
#dto_env['DTO_WAIT_METHOD']= 'busypoll' # 'yield' #  'umwait' #  'yield' # 'sleep' #   
dto_env['DTO_MIN_BYTES']='8192'  #'65536'#    
#dto_env['DTO_CPU_SIZE_FRACTION']= '0.1' # '0.33' #
dto_env['DTO_AUTO_ADJUST_KNOBS']='0'
dto_env['DTO_MAKE_ADJ'] = '1'
#dto_env['DTO_DSA_CC']='1'
dto_env['LD_PRELOAD']='' #'./libdto.so.1.0'
dto_env['DTO_MAX_BYTES']='2097152'

dto_env['DTO_IS_NUMA_AWARE']='2'
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
    print("setting up for DTO_python")
    dto_env['DTO_STATS_PREFIX']="'''"
    #dto_env['DTO_STATS_PREFIX']=""
    dto_env['DTO_STATS_OUTPUT_TYPE']='1'
    dto_env['DTO_COLLECT_STATS']='1'
    if alg_stats:
        dto_env['DTO_COLLECT_ALG_STATS']='1'
        dto_env['DTO_COLLECT_ALG_STATS_LATEST_UPDATES']='1'
        dto_env['DTO_COLLECT_ALG_STATS_LATEST_CPUFRACTS']='1'
elif output_type == 'perf':
    dto_env['DTO_STATS_OUTPUT_TYPE']='0'
    dto_env['DTO_COLLECT_STATS']='1'
    if alg_stats:
        dto_env['DTO_COLLECT_ALG_STATS']='1'
        dto_env['DTO_COLLECT_ALG_STATS_LATEST_UPDATES']='1'
        dto_env['DTO_COLLECT_ALG_STATS_LATEST_CPUFRACTS']='1'
        dto_env['DTO_STATS_OUTPUT_TYPE']='1'

for dto_command,num_threads,output_name_base in zip(dto_commands,thread_counts,output_names):
    taskset_command = ['taskset', '-c', '{}-{}'.format(taskset_startcpu,taskset_startcpu+num_threads)]
    
    for numa_config in numa_configs:
        for wait in wait_methods:
            dto_env['DTO_WAIT_METHOD'] = wait
            for cache_control in cache_control_configs:
                dto_env['DTO_DSA_CC']=str(cache_control)

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
                    if perc == 'nodsa' and (numa_config != 'unaware'  or wait != 'yield'):
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
                        dto_env['DTO_CPU_SIZE_FRACTION']=perc

                    output_name = '{}_{}_{}_{}_{}'.format(output_name_base,numa_config,perc.replace(".", "-"),wait,cache_control)

                    print('processing {}'.format(output_name))

                    if not dry_run:
                        if not overwrite and os.path.exists(os.path.join(results_dir,'{}_{}_DTO_config.json'.format(name,output_name))):
                            continue
                    
                        with open(os.path.join(results_dir,'{}_{}_DTO_config.json'.format(name, output_name)), 'w') as f:
                            json.dump(dto_env, f, indent=4)
                    
                    if run_taskset:
                        dto_command = taskset_command + dto_command
                    
                    if dry_run:
                        print(' '.join(dto_command))
                        continue

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
                        
                        if output_type == 'perf':
                                dto_command = perf_command + dto_command

                        with open(outfile,'w') as f:
                            if output_type == 'DTO_python':
                                f.write("'''\n")
                                f.flush()
                            
                            dto_process = subprocess.Popen(dto_command, env=dto_env, stdout=f, stderr=f) # stdout=1, stderr=1) #
                            ret = dto_process.wait()
                            time.sleep(5)
                            f.flush()
                    
                    time.sleep(1)
                
