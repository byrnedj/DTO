# ==========================================================================
# Copyright (C) 2024 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==========================================================================

import subprocess, os, time, re, time, json, sys
import numpy as np
import argparse

from set_frequency import SetCoreFrequency,SetUncoreFrequency

# Create an ArgumentParser object
parser = argparse.ArgumentParser()

# Add arguments
parser.add_argument('--output-type', type=str, default='perf', help='output type: perf | DTO-python | emon')
parser.add_argument('--run-name', type=str, default='test', help='name to be used in output directory')
parser.add_argument('--num-threads-override', type=int, default=None, help='override number of threads from config file')
parser.add_argument('--num-iter', type=int, default=10000000, help='number of iterations')
parser.add_argument('--cfg-filepath', type=str, default=None, help='cofiguration file')
parser.add_argument('--distribution-filepath', type=str, default=None, help='mem op distribution file')
parser.add_argument('--results-dirname', type=str, default=None, help='results directory')
parser.add_argument('--dry-run', action='store_true', help='perform a dryrun')
parser.add_argument('--overwrite', action='store_true', help='overwrite existing test results')
parser.add_argument('--alg-stats', action='store_true', help='collect algorithm stats')
parser.add_argument('--no-stats', action='store_true', help='do not collect any stats')
parser.add_argument('--num-dsas', type=int, default=8, help='number of DSA to use')
parser.add_argument('--max-transaction-size', type=int, default=None, help='maximum transaction size (in KB) to use')
parser.add_argument('--stats-warmup-steps', type=int, default=0, help='number of transactions to ignore')
parser.add_argument('--exclude-pfs', action='store_true', help='exclude DSA transactions with PFs from autotune algorithm')
parser.add_argument('--same-buffer', action='store_true', help='perform multiple memops in series on same data buffers')
parser.add_argument('--per-op-instances', action='store_true', help='perform multiple memops in series on same data buffers')
parser.add_argument('--separate-processes', action='store_true', help='spawn separate processes in place of threads')
parser.add_argument('--perc-buffer-overlap', type=int, default=0, help='percentage overlap in buffers for mov transactions')
parser.add_argument('--overlap-probability', type=int, default=0, help='probability of overlap as a percentage between 0 and 100')
parser.add_argument('--overlapping-move-action', type=str, default='cpu', help='Action to be taken for overlapping memmove')
parser.add_argument('--control-frequencies', action='store_true', help='control core and uncore frequencies')

# Parse the arguments
args = parser.parse_args()


MEMSET = 1
MEMCOPY = 2   
MEMSET_MEMCPY = 3 
MEMMOVE = 4       
MEMCMP = 8


def get_mem_ops(mem_op):
    ops = []
    for mask in [MEMSET,MEMCOPY,MEMMOVE,MEMCMP]:
        if mem_op & mask:
            ops.append(mask)
    return ops

def create_uniform_dist_file(size,mem_ops,file):
    percentage = round(1/len(mem_ops),3)
    remainder = 1-(percentage*(len(mem_ops)))
    num = 0
    with open(file, "w") as f:
        f.write('{},'.format(size*1024))
        if MEMSET in mem_ops:
            num += 1
            f.write('{},'.format(percentage))
        else:
            f.write('{},'.format(0.0))
        if MEMCOPY in mem_ops:
            num += 1
            value = percentage
            if num == len(mem_ops):
                value += remainder
                value = round(value,3)
            f.write('{},'.format(value))
        else:
            f.write('{},'.format(0.0))
        if MEMMOVE in mem_ops:
            num += 1
            value = percentage
            if num == len(mem_ops):
                value += remainder
                value = round(value,3)
            f.write('{},'.format(value))
        else:
            f.write('{},'.format(0.0))

        f.write('\n')


numa_alloc_policy = { 'unaware': '0',
                      'same': '1',
                      'diff': '2'
}

overlapping_memmove_actions = {'cpu':'0', 'dsa':'1'}


map_numa_config = {'unaware': 'u', 'buffer': 'b', 'cpu':'c', 'antibuffer': 'ab','anticpu':'ac'}

map_wait_method = {'yield': 'y', 'busypoll': 'b', 'umwait': 'uw'}

dto_command_base_settable = ['../dto-test-settable-size-dev5']
dto_command_base_distribution = ['../dto-test-distribution-multithread-dev5']

run_taskset = True
dry_run = args.dry_run   #False
overwrite = args.overwrite
# Output choices are: 'DTO-python' or  'emon'  or 'perf'
output_type = args.output_type  #'perf'  #'emon' # 'DTO-python'  #  'perf'
cfg_filepath = args.cfg_filepath
results_dirname = args.results_dirname
run_name = args.run_name  #'test_perf'
#num_threads=args.num_threads
num_threads_override=args.num_threads_override
num_iter = args.num_iter  #10000000 #1000000
distribution_filepath = args.distribution_filepath
alg_stats = args.alg_stats
no_stats = args.no_stats
num_dsas= args.num_dsas  #8
max_transaction_size = args.max_transaction_size
stats_warmup_steps = args.stats_warmup_steps
exclude_pfs_from_autotune = args.exclude_pfs
same_buffer = args.same_buffer
per_op_instances = args.per_op_instances
separate_processes = args.separate_processes
perc_buffer_overlap = args.perc_buffer_overlap
overlap_probability = args.overlap_probability
overlapping_move_action = args.overlapping_move_action
control_frequencies = args.control_frequencies

#if distribution_filepath is not None:
#    dto_command_base = dto_command_base_distribution
#else:
#    dto_command_base = dto_command_base_settable


scale_buf_sizes_with_tx_size = False
total_buf_sizes = [8*1024] # [64, 8*1024]  # [8*1024] #in MB


taskset_startcpu = 56

#sizes = [2**x for x in range(3,11)]   #8-1024
sizes = [2**x for x in range(4,11)]  #16-1024
#sizes = [2**x for x in range(5,11)]  #32-1024
#sizes = [2**x for x in range(6,11)]  #64-1024
#sizes = [1024,1280,1536,1792,2048]
#sizes = [8,16]
#sizes=[1024]


percentages =  ['auto', 'nodsa','0.0']  #['auto', 'nodsa'] # [ 0.8, 0.9, 0.99] #, 0.7 ]  #[0.7] #[0.0, 0.1, 0.33] #[0.3, 0.35, 0.4, 0.45, 0.5 ] [0.8, 0.9, 0.99] #[0.75, 0.8, 0.85, 0.9, 0.95, 0.99]

mem_ops = [MEMSET,MEMCOPY] #,MEMMOVE]
mem_op_names = {MEMSET:'set', MEMCOPY:'cpy', MEMSET_MEMCPY:'cpy-set', MEMMOVE:'mov'}

numa_configs = ['unaware']  #, 'buffer', 'cpu', 'antibuffer', 'anticpu']

cache_control_configs = [1] #[0,1]

wait_methods = ['yield']  #, 'umwait', 'busypoll']

alloc_types = ['individual'] #['single', 'individual']

numa_alloc_policy_src = 'unaware'
numa_alloc_policy_dst = 'unaware'

# These go in pairs
thread_counts = [1]
bursting_thread_counts = [0]


burst_sizes = [1]
times_between_bursts_ms = [0]

warmup_time_s = 0

rand_index_uses = [1] # [0,1]

if scale_buf_sizes_with_tx_size:
    mem_buf_sizes = sizes*len(total_buf_sizes)
    num_mem_bufs = [int(t*1024/m) for t in total_buf_sizes for m in sizes ]
else: 
    mem_buf_sizes = [1024] # multiples of 1024 - number of 1024 means buffer size of 1MB
    num_mem_bufs = [32, 64, 512, 1024] # [2,4,8,16]  #[32, 1024, 2*1024, 4*1024, 8*1024, 16*1024, 32*1014] #[32, 64, 512, 1024, 2*1024, 4*1024, 8*1024, 16*1024, 32*1014] #[1024] #[32, 64, ]  #, 512, 1024, 2*1024, 4*1024, 8*1024, 16*1024, 32*1014]  # 48*1024] # 48  

    if len(mem_buf_sizes) == 1 and len(num_mem_bufs) > 1:
        mem_buf_sizes = mem_buf_sizes * len(num_mem_bufs)


config = {
    "output_type":output_type,
    #"num_threads":num_threads,
    "thread_counts": thread_counts,
    "num_dsas":num_dsas,
    "num_iter":num_iter,
    "run_taskset":run_taskset,
    "taskset_startcpu":taskset_startcpu,
    "sizes":sizes,
    "percentages":percentages,
    "mem_ops":mem_ops,
    "numa_configs":numa_configs,
    "scale_buf_sizes_with_tx_size": scale_buf_sizes_with_tx_size,
    "total_buf_sizes":total_buf_sizes,
    "mem_buf_sizes": mem_buf_sizes,
    "num_mem_bufs": num_mem_bufs,
    "alloc_types": alloc_types,
    "rand_index_uses": rand_index_uses,
    "wait_methods": wait_methods,
    "cache_control_configs":cache_control_configs,
    "distribution_filepath": distribution_filepath,
    "numa_alloc_policy_src": numa_alloc_policy_src,
    "numa_alloc_policy_dst": numa_alloc_policy_dst,
    "bursting_thread_counts": bursting_thread_counts,
    "burst_sizes": burst_sizes,
    "time_between_bursts_ms": times_between_bursts_ms,
    "warmup_time_s": warmup_time_s,
    "stats_warmup_steps": stats_warmup_steps,
    "exlude_pfs_from_autotune":  exclude_pfs_from_autotune,
    "same_buffer": same_buffer,
    "per_op_instances": per_op_instances,
    "perc_buffer_overlap" : perc_buffer_overlap,
    "overlap_probability": overlap_probability,
    "overlapping_move_action": overlapping_move_action,
}

if cfg_filepath is not None:
    if not os.path.exists(cfg_filepath):
            print("error input file {} not found".format(cfg_filepath))
            sys.exit(0)

    with open(cfg_filepath, 'r') as f:
        loaded_config = json.load(f)

    if 'sizes' in loaded_config:
        sizes = config['sizes'] = loaded_config['sizes']

    if 'percentages' in loaded_config:
        percentages = config['percentages'] = loaded_config['percentages']
    
    if "mem_ops" in loaded_config:
        mem_ops = config["mem_ops"] = loaded_config["mem_ops"]

    if distribution_filepath is None and "scale_buf_sizes_with_tx_size" in loaded_config:
        scale_buf_sizes_with_tx_size = config["scale_buf_sizes_with_tx_size"] = loaded_config["scale_buf_sizes_with_tx_size"]

    if "total_buf_sizes" in loaded_config:
        total_buf_sizes = config["total_buf_sizes"] = loaded_config["total_buf_sizes"]

    if "alloc_types" in loaded_config:
        alloc_types = config["alloc_types"] = loaded_config["alloc_types"]

    if "rand_index_uses" in loaded_config:
        rand_index_uses = config["rand_index_uses"] = loaded_config["rand_index_uses"]

    if "mem_buf_sizes" in loaded_config:
        mem_buf_sizes = config["mem_buf_sizes"] = loaded_config["mem_buf_sizes"]

    if "num_mem_bufs" in loaded_config:
        num_mem_bufs = config["num_mem_bufs"] = loaded_config["num_mem_bufs"]

    if "wait_methods" in loaded_config:
        wait_methods = config["wait_methods"] = loaded_config["wait_methods"]

    if "cache_control_configs" in loaded_config:
        cache_control_configs = config["cache_control_configs"] = loaded_config["cache_control_configs"]

    if "numa_alloc_policy_src" in loaded_config:
        numa_alloc_policy_src = config["numa_alloc_policy_src"] = loaded_config["numa_alloc_policy_src"]

    if "numa_alloc_policy_dst" in loaded_config:
        numa_alloc_policy_dst = config["numa_alloc_policy_dst"] = loaded_config["numa_alloc_policy_dst"]
    
    if "thread_counts" in loaded_config:
        thread_counts = config["thread_counts"] = loaded_config["thread_counts"]

        if "bursting_thread_counts" not in loaded_config:
            bursting_thread_counts = config["bursting_thread_counts"] = [0]*len(thread_counts)

    if "bursting_thread_counts" in loaded_config:
        bursting_thread_counts = config["bursting_thread_counts"] = loaded_config["bursting_thread_counts"]

    if "burst_sizes" in loaded_config:
        burst_sizes = config["burst_sizes"] = loaded_config["burst_sizes"]

    if "time_between_bursts_ms" in loaded_config:
        times_between_bursts_ms = config["time_between_bursts_ms"] = loaded_config["time_between_bursts_ms"]

    if "warmup_time_s" in loaded_config:
        warmup_time_s = config["warmup_time_s"] = loaded_config["warmup_time_s"]

    if "numa_configs" in loaded_config:
        numa_configs = config["numa_configs"] = loaded_config["numa_configs"]

    #if "stats_warmups" in loaded_config:
    #    stats_warmups = config["stats_warmups"] = loaded_config["stats_warmups"]

    #if "exlude_pfs_choices" in loaded_config:
    #    exlude_pfs_choices = config["exlude_pfs_choices"] = loaded_config["exlude_pfs_choices"]

    if control_frequencies:
        assert("core_frequencies" in loaded_config and "uncore_frequencies" in loaded_config), "Error: frequency control is selected, but core and uncore frequencies not found in config file"
        core_freqs = config["core_frequencies"] = loaded_config["core_frequencies"]
        uncore_freqs = config["uncore_frequencies"] = loaded_config["uncore_frequencies"]
  
if scale_buf_sizes_with_tx_size:
    mem_buf_sizes = config["mem_buf_sizes"] = sizes*len(total_buf_sizes)
    num_mem_bufs = config["num_mem_bufs"] = [int(t*1024/m) for t in total_buf_sizes for m in sizes ]
else: 
    if len(mem_buf_sizes) == 1 and len(num_mem_bufs) > 1:
        mem_buf_sizes = mem_buf_sizes * len(num_mem_bufs)

if distribution_filepath is not None:
    sizes = config['sizes'] = ['dist']
    mem_ops = config["mem_ops"] = ['dist']

if num_threads_override is not None:
    thread_counts = config["thread_counts"] = [num_threads_override]

if per_op_instances and 'auto' in percentages:
    percentages.append('autop') 
    config["percentages"] = percentages

assert len(thread_counts) == len(bursting_thread_counts), "Error: thread_counts length is {} and bursting_thread_counts lenght is {}. They must be equal".format(len(thread_counts), len(bursting_thread_counts))

#assert len(burst_sizes) == len(time_between_bursts_ms), "Error: burst_sizes length is {} and time_between_bursts_ms lenght is {}. They must be equal".format(len(burst_sizes), len(time_between_bursts_ms))

if output_type == 'emon' and stats_warmup_steps != 0:
    assert False, "Error: emon output type not supported with stats-warmup-steps > 0"

if output_type == 'emon' and separate_processes:
    assert False, "Error: emon output type not currently supported separate-processes" 

base_name = 'all5_{}_{}'.format(run_name, output_type)

results_root = './results'

if not dry_run:
    if results_dirname is None:
        save_config = True
        sync_id = time.strftime('%m%d%y_%H%M%S', time.localtime())
        results_dirname = base_name+'_'+sync_id
    else:
        save_config = False
    
    results_dir = os.path.join(results_root,results_dirname)
    if not os.path.exists(results_dir):
        os.makedirs(results_dir)   

    if save_config:
        with open(os.path.join(results_dir,'{}_config.json'.format(base_name)), 'w') as f:
            json.dump(config, f, indent=4)

#perf_command = 'perf stat -e dsa0/event=0x1,event_category=0x0/,dsa2/event=0x1,event_category=0x0/,dsa4/event=0x1,event_category=0x0/,dsa6/event=0x1,event_category=0x0/,dsa0/event=0x1,event_category=0x1/,dsa2/event=0x1,event_category=0x1/,dsa4/event=0x1,event_category=0x1/,dsa6/event=0x1,event_category=0x1/,dsa0/event=0x2,event_category=0x1/,dsa2/event=0x2,event_category=0x1/,dsa4/event=0x2,event_category=0x1/,dsa6/event=0x2,event_category=0x1/'
perf_command = ['perf', 'stat']

emon_command = ['sudo', '/opt/intel/sep/bin64/emon', '-collect-edp', 'edp_file=/opt/intel/sep/config/edp/sapphirerapids_server_events.txt']


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

dto_env['DTO_STATS_NUM_WARMUP_OPS']=str(stats_warmup_steps )
dto_env['DTO_AUTOTUNE_EXCLUDE_FAILED']='1' if exclude_pfs_from_autotune else '0'

dto_env['DTO_OVERLAPPING_MEMMOVE_ACTION'] = overlapping_memmove_actions[overlapping_move_action]

if output_type == 'DTO-python':
    print("setting up for DTO-python")
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
    if not no_stats:
        dto_env['DTO_COLLECT_STATS']='1'
        if alg_stats:
            dto_env['DTO_COLLECT_ALG_STATS']='1'
            dto_env['DTO_COLLECT_ALG_STATS_LATEST_UPDATES']='1'
            dto_env['DTO_COLLECT_ALG_STATS_LATEST_CPUFRACTS']='1'
            dto_env['DTO_STATS_OUTPUT_TYPE']='1'


if control_frequencies and not dry_run:
    cores = list(range(taskset_startcpu,taskset_startcpu+np.max(thread_counts)+1))
    set_uncore_freq = SetUncoreFrequency()
    set_core_freq = SetCoreFrequency(cores)

for core_freq in core_freqs:
    for uncore_freq in uncore_freqs:
        if control_frequencies:
            name = '{}_{}_{}'.format(base_name,core_freq,uncore_freq)
            if not dry_run:
                set_uncore_freq(uncore_freq)
                set_core_freq(core_freq)
        else:
            name = base_name

        for num_threads, num_bursting_threads in zip(thread_counts,bursting_thread_counts):
            if separate_processes:
                nt=1
                nbt=0
                taskset_commands = []
                for p in range(num_threads):
                    taskset_commands.append(['taskset', '-c', '{}'.format(taskset_startcpu+p)])
                ni = int(round(num_iter / num_threads,0))
            else:
                nt = num_threads
                nbt = num_bursting_threads
                taskset_commands = [['taskset', '-c', '{}-{}'.format(taskset_startcpu,taskset_startcpu+num_threads)]]
                ni = num_iter

            for burst_size in burst_sizes:
                for time_between_bursts in times_between_bursts_ms:
                    for mem_op in mem_ops:
                        generate_distfile = False
                        if mem_op == 'dist':
                            op_name = 'dist'
                        else:
                            if mem_op in [MEMCMP, MEMCOPY, MEMMOVE, MEMSET]:
                                op_name = mem_op_names[mem_op]
                            else:
                                individual_ops = get_mem_ops(mem_op)
                                op_name = '-'.join([mem_op_names[m] for m in individual_ops])
                                if not same_buffer:
                                    generate_distfile = True
                        for numa_config in numa_configs:
                            for num_b, mem_buf_size in zip(num_mem_bufs, mem_buf_sizes):
                                for alloc in alloc_types:
                                    for r in rand_index_uses:
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
                                                    #if perc == 'nodsa' and (numa_config != 'unaware'  or wait != 'yield'):
                                                    #    continue

                                                    if perc == 'nodsa':
                                                        dto_env['DTO_USESTDC_CALLS']='1'
                                                    elif perc in ['auto', 'autop']:
                                                        dto_env['DTO_USESTDC_CALLS']='0'
                                                        dto_env['DTO_AUTO_ADJUST_KNOBS']='1'
                                                        dto_env['DTO_MAKE_ADJ'] = '1'
                                                        dto_env['DTO_CPU_SIZE_FRACTION']='0.33'
                                                        if perc == 'autop':
                                                            dto_env['DTO_PER_OP_AUTOTUNE_INSTANCES']='1'
                                                        else:
                                                            dto_env['DTO_PER_OP_AUTOTUNE_INSTANCES']='0'
                                                    else:
                                                        dto_env['DTO_USESTDC_CALLS']='0'
                                                        dto_env['DTO_CPU_SIZE_FRACTION']=perc
                                                        if alg_stats:
                                                            dto_env['DTO_AUTO_ADJUST_KNOBS']='1'
                                                            dto_env['DTO_MAKE_ADJ'] = '0'
                                                        else:
                                                            dto_env['DTO_AUTO_ADJUST_KNOBS']='0'

                                                    for size in sizes:
                                                        if size != 'dist' and scale_buf_sizes_with_tx_size and size != mem_buf_size:
                                                            continue
                                                        
                                                        # In the case of a distribution, we use the distribution filename in place of the size.
                                                        if size == 'dist':
                                                                _,fn = os.path.split(distribution_filepath)
                                                                fn,_ = os.path.splitext(fn)
                                                                size=fn.replace('_','-')

                                                        if generate_distfile:
                                                            #print('creating temp disfile {}'.format(individual_ops))
                                                            create_uniform_dist_file(size, individual_ops,"temp_memop_dist.csv")

                                                        output_name = '{}_{}_{}_{}_{}_{}_{}_{}_{}_{}_{}_{}_{}_{}_{}K'.format(op_name,num_threads,num_bursting_threads,burst_size,time_between_bursts,warmup_time_s,map_numa_config[numa_config],perc.replace(".", "-"),map_wait_method[wait],alloc,cache_control,r,num_b,mem_buf_size,size)

                                                        print('processing {}_{}'.format(name,output_name))

                                                        if not dry_run:
                                                            if not overwrite and os.path.exists(os.path.join(results_dir,'{}_{}_DTO_config.json'.format(name,output_name))):
                                                                continue
                                                        
                                                            with open(os.path.join(results_dir,'{}_{}_DTO_config.json'.format(name, output_name)), 'w') as f:
                                                                json.dump(dto_env, f, indent=4)

                                                        if alloc == 'single':
                                                            alloc_size = mem_buf_size*num_b
                                                            if distribution_filepath is not None:
                                                                dto_command_base = dto_command_base_distribution
                                                                dto_command = dto_command_base + [str(nt), str(0), str(ni), str(alloc_size), '1', str(r),numa_alloc_policy[numa_alloc_policy_src], numa_alloc_policy[numa_alloc_policy_dst], str(nbt), str(burst_size), str(time_between_bursts), str(warmup_time_s), str(perc_buffer_overlap), str(overlap_probability), distribution_filepath]
                                                            elif generate_distfile:
                                                                dto_command_base = dto_command_base_distribution
                                                                dto_command = dto_command_base + [str(nt), str(0), str(ni), str(alloc_size), '1', str(r),numa_alloc_policy[numa_alloc_policy_src], numa_alloc_policy[numa_alloc_policy_dst], str(nbt), str(burst_size), str(time_between_bursts), str(warmup_time_s), str(perc_buffer_overlap), str(overlap_probability), "temp_memop_dist.csv"]
                                                            else:
                                                                dto_command_base = dto_command_base_settable
                                                                dto_command = dto_command_base + [str(nt), str(0), str(mem_op), str(size), str(ni), str(alloc_size), '1', str(r), numa_alloc_policy[numa_alloc_policy_src], numa_alloc_policy[numa_alloc_policy_dst], str(nbt), str(burst_size), str(time_between_bursts), str(warmup_time_s), str(perc_buffer_overlap), str(overlap_probability)]
                                                        else:
                                                            if distribution_filepath is not None:
                                                                dto_command_base = dto_command_base_distribution
                                                                dto_command = dto_command_base + [str(nt), str(0), str(ni), str(mem_buf_size), str(num_b), str(r),numa_alloc_policy[numa_alloc_policy_src], numa_alloc_policy[numa_alloc_policy_dst], str(nbt), str(burst_size), str(time_between_bursts), str(warmup_time_s), str(perc_buffer_overlap), str(overlap_probability), distribution_filepath]
                                                            elif generate_distfile:
                                                                dto_command_base = dto_command_base_distribution
                                                                dto_command = dto_command_base + [str(nt), str(0), str(ni), str(mem_buf_size), str(num_b), str(r),numa_alloc_policy[numa_alloc_policy_src], numa_alloc_policy[numa_alloc_policy_dst], str(nbt), str(burst_size), str(time_between_bursts), str(warmup_time_s), str(perc_buffer_overlap), str(overlap_probability), "temp_memop_dist.csv"]
                                                            else:
                                                                dto_command_base = dto_command_base_settable
                                                                dto_command = dto_command_base + [str(nt), str(0), str(mem_op), str(size), str(ni), str(mem_buf_size), str(num_b), str(r),numa_alloc_policy[numa_alloc_policy_src], numa_alloc_policy[numa_alloc_policy_dst], str(nbt), str(burst_size), str(time_between_bursts), str(warmup_time_s), str(perc_buffer_overlap), str(overlap_probability)]

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
                                                            outfiles = []
                                                            dto_procs = []

                                                            if separate_processes:
                                                                num_proc = num_threads
                                                            else:
                                                                num_proc = 1

                                                            for p in range(num_proc):

                                                                if run_taskset:
                                                                    cmd = taskset_commands[p] + dto_command

                                                                if dry_run:
                                                                    print(' '.join(cmd))
                                                                    continue

                                                                if output_type == 'DTO-python':
                                                                    ext = 'py'
                                                                else:
                                                                    ext = 'txt'
                                                                if separate_processes:
                                                                    outfile = os.path.join(results_dir,'{}_{}_{}.{}'.format(name,output_name,p,ext))
                                                                else:
                                                                    outfile = os.path.join(results_dir,'{}_{}.{}'.format(name,output_name,ext))
                                                                
                                                                outfiles.append(open(outfile,'w'))
                                                                    
                                                                if output_type == 'perf':
                                                                    cmd = perf_command + cmd

                                                                if output_type == 'DTO-python':
                                                                    f.write("'''\n")
                                                                    f.flush()
                                                                dto_procs.append(subprocess.Popen(cmd, env=dto_env, stdout=outfiles[p], stderr=outfiles[p])) # stdout=1, stderr=1) #

                                                            if not dry_run:
                                                                for p in range(num_proc):
                                                                    ret = dto_procs[p].wait()
                                                                    time.sleep(1)
                                                                    outfiles[p].flush()
                                                                    outfiles[p].close()
                                                        
                                                        if not dry_run:
                                                            time.sleep(1)

                                                        if generate_distfile and not dry_run:
                                                            if os.path.exists("temp_memop_dist.csv"):
                                                                os.remove("temp_memop_dist.csv")
                                                            else:
                                                                print("warning: generate_distfile is true but file {} does not exist".format("temp_memop_dist.csv"))
                                                
                                                    
