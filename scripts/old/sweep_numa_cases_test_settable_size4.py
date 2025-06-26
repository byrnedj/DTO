# ==========================================================================
# Copyright (C) 2024 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==========================================================================

import subprocess, os, time, re, time, json, sys

import argparse

# Create an ArgumentParser object
parser = argparse.ArgumentParser()

# Add arguments
parser.add_argument('--output-type', type=str, default='perf', help='output type: perf | DTO_python | emon')
parser.add_argument('--run-name', type=str, default='test', help='name to be used in output directory')
parser.add_argument('--num-threads-override', type=int, default=None, help='override number of threads from config file')
parser.add_argument('--num-iter', type=int, default=10000000, help='number of iterations')
parser.add_argument('--cfg-filepath', type=str, default=None, help='cofiguration file')
# The distribution must be all set or all cpy operations.
parser.add_argument('--distribution-filepath', type=str, default=None, help='mem op distribution file')
parser.add_argument('--results-dirname', type=str, default=None, help='results directory')
parser.add_argument('--overwrite', action='store_true', help='overwrite existing test results')
parser.add_argument('--dry-run', action='store_true', help='perform a dryrun')
parser.add_argument('--alg-stats', action='store_true', help='collect algorithm stats')
parser.add_argument('--num-dsas', type=int, default=8, help='number of DSA to use')
parser.add_argument('--exclude-nodsa', action='store_true', help='exclude the nodsa run')
parser.add_argument('--max-transaction-size', type=int, default=None, help='maximum transaction size to use')
parser.add_argument('--single-case', type=str, default=None, help='run a single case with the given specification')
parser.add_argument('--cpu-perc-override', type=str, default=None, help='override for cpu perc values in a comma seperated list')

# Parse the arguments
args = parser.parse_args()

MEMSET = 1
MEMCOPY = 2    
MEMMOVE = 4       
MEMCMP = 8

dto_command_base_settable = ['../dto-test-settable-size-dev4']
dto_command_base_distribution = ['../dto-test-distribution-multithread-dev4']

run_taskset = True
dry_run = args.dry_run   #False
overwrite = args.overwrite
# Output choices are: 'DTO_python' or  'emon'  or 'perf'
output_type = args.output_type  #'perf'  #'emon' # 'DTO_python'  #  'perf'
cfg_filepath = args.cfg_filepath
results_dirname = args.results_dirname
run_name = args.run_name  #'test_perf'
#num_threads=args.num_threads
num_threads_override=args.num_threads_override
num_iter = args.num_iter  #10000000 #1000000
distribution_filepath = args.distribution_filepath
alg_stats = args.alg_stats
num_dsas= args.num_dsas  #8
exclude_nodsa = args.exclude_nodsa
max_transaction_size = args.max_transaction_size
single_case = args.single_case
cpu_perc_override = args.cpu_perc_override

if distribution_filepath is not None:
    dto_command_base = dto_command_base_distribution
else:
    dto_command_base = dto_command_base_settable

scale_buf_sizes_with_tx_size = True
total_buf_sizes = [8*1024] #[64, 8*1024]  # in MB

taskset_startcpu = 56

#sizes = [2**x for x in range(3,11)]   #8-1024
#sizes = [2**x for x in range(5,11)]  #32-1024
#sizes = [2**x for x in range(6,11)]  #64-1024
#sizes = [1024,1280,1536,1792,2048]
#sizes = [8,16]
sizes = [1024]


percentages = ['auto', '0.0']  # ['auto'] # [ '0.8', '0.9', '0.99'] #, '0.7' ]  

mem_ops =  [MEMCOPY] #[MEMSET,MEMCOPY] #,MEMMOVE]
mem_op_names = ['cpy'] #['set', 'cpy'] #, 'mov']

#print(total_buf_sizes)
#print(sizes)
#print(mem_buf_sizes)
#print(num_mem_bufs)
#print(crash)

alloc_types = ['individual'] #['single', 'individual']

thread_counts = [1]

rand_index_uses =  [1]  #[0,1]

if scale_buf_sizes_with_tx_size:
    mem_buf_sizes = sizes*len(total_buf_sizes)
    num_mem_bufs = [int(t*1024/m) for t in total_buf_sizes for m in sizes ]
else: 
    mem_buf_sizes = [1024] # multiples of 1024 - number of 1024 means buffer size of 1MB
    num_mem_bufs = [64,512,32*1014]  #[32, 1024, 2*1024, 4*1024, 8*1024, 16*1024, 32*1014] #[32, 64, 512, 1024, 2*1024, 4*1024, 8*1024, 16*1024, 32*1014] #[1024] #[32, 64, ]  #, 512, 1024, 2*1024, 4*1024, 8*1024, 16*1024, 32*1014]  # 48*1024] # 48  

    if len(mem_buf_sizes) == 1 and len(num_mem_bufs) > 1:
        mem_buf_sizes = mem_buf_sizes * len(num_mem_bufs)


config = {
    "output_type":output_type,
    #"num_threads":num_threads,
    "num_dsas":num_dsas,
    "num_iter":num_iter,
    "run_taskset":run_taskset,
    "taskset_startcpu":taskset_startcpu,
    "sizes":sizes,
    "percentages":percentages,
    "mem_ops":mem_ops,
    "scale_buf_sizes_with_tx_size": scale_buf_sizes_with_tx_size,
    "total_buf_sizes":total_buf_sizes,
    "mem_buf_sizes": mem_buf_sizes,
    "num_mem_bufs": num_mem_bufs,
    "alloc_types": alloc_types,
    "rand_index_uses": rand_index_uses,
    "distribution_filepath": distribution_filepath,
}

if cfg_filepath is not None:
    if not os.path.exists(cfg_filepath):
            print("error input file {} not found".format(cfg_filepath))
            sys.exit(0)

    with open(cfg_filepath, 'r') as f:
        loaded_config = json.load(f)

    #print(loaded_config)

    if 'sizes' in loaded_config:
        sizes = config['sizes'] = loaded_config['sizes']

    if 'percentages' in loaded_config:
        percentages = config['percentages'] = loaded_config['percentages']
    
    if "mem_ops" in loaded_config:
        mem_ops = config["mem_ops"] = loaded_config["mem_ops"]

    if "scale_buf_sizes_with_tx_size" in loaded_config:
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

    #if "wait_methods" in loaded_config:
    #    wait_methods = config["wait_methods"] = loaded_config["wait_methods"]

    #if "cache_control_configs" in loaded_config:
    #    cache_control_configs = config["cache_control_configs"] = loaded_config["cache_control_configs"]


    if "thread_counts" in loaded_config:
        thread_counts = config["thread_counts"] = loaded_config["thread_counts"]

if scale_buf_sizes_with_tx_size:
    mem_buf_sizes = config["mem_buf_sizes"] = sizes*len(total_buf_sizes)
    num_mem_bufs = config["num_mem_bufs"] = [int(t*1024/m) for t in total_buf_sizes for m in sizes ]
else: 
    if len(mem_buf_sizes) == 1 and len(num_mem_bufs) > 1:
        mem_buf_sizes = mem_buf_sizes * len(num_mem_bufs)

if distribution_filepath is not None:
    sizes = config['sizes'] = ['dist']
    #mem_ops = config["mem_ops"] = ['dist']

if num_threads_override is not None:
    thread_counts = config["thread_counts"] = [num_threads_override]


if cpu_perc_override is not None:
    config["thread_counts"] = percentages = cpu_perc_override.split(',')

name = 'sweepnumacases_{}_{}'.format(run_name, output_type)

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


base_env = os.environ.copy()
dto_env = os.environ.copy()

dto_env['DTO_NUM_WQS']='{}'.format(num_dsas)

if max_transaction_size is not None:
    dto_env['DTO_MAX_BYTES'] = max_transaction_size

dto_env['DTO_USESTDC_CALLS']='0'
dto_env['DTO_COLLECT_STATS']='0'
dto_env['DTO_STATS_OUTPUT_TYPE']='0'
dto_env['DTO_WAIT_METHOD']= 'yield' # 'umwait' # 'busypoll' # 'umwait' #  'yield' # 'sleep' #   
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
dto_env['DTO_LOG_LEVEL']='3'
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

# memset operations
if MEMSET in mem_ops:
    mem_op = MEMSET  #MEMCOPY
    op_name = 'set' # 'cpy'

    for num_threads in thread_counts:
        taskset_command = ['taskset', '-c', '{}-{}'.format(taskset_startcpu,taskset_startcpu+num_threads)]
 
        for cpu_numa in ['same', 'diff']:

            if single_case is not None:
                sc_cpu_numa = single_case.split(',')[0]
                if sc_cpu_numa != cpu_numa:
                    continue

            for num_b, mem_buf_size in zip(num_mem_bufs, mem_buf_sizes):
                for alloc in alloc_types:
                    for r in rand_index_uses:
                        if not exclude_nodsa:

                            dto_env['DTO_USESTDC_CALLS']='1'
                            perc = 'nodsa'
                            dsa_numa = 'na'

                            for size in sizes:
                                if scale_buf_sizes_with_tx_size and size != mem_buf_size:
                                    continue
                                
                                output_name = '{}_{}_{}_{}_{}_{}_{}_{}_{}_{}K'.format(op_name,num_threads,cpu_numa,dsa_numa,perc.replace(".", "-"),alloc,r,num_b,mem_buf_size,size)

                                print('processing {}'.format(output_name))

                                if not dry_run:
                                    if not overwrite and os.path.exists(os.path.join(results_dir,'{}_DTO_config.json'.format(output_name))):
                                        continue

                                    with open(os.path.join(results_dir,'{}_DTO_config.json'.format(output_name)), 'w') as f:
                                        json.dump(dto_env, f)

                                if alloc == 'single':
                                    alloc_size = mem_buf_size*num_b
                                    #dto_command = dto_command_base + [str(num_threads), str(0), str(mem_op), str(size), str(num_iter), str(alloc_size), '1', str(r)]
                                    dto_command = dto_command_base + [str(num_threads), str(0), str(num_iter), str(alloc_size), '1', str(r)]
                                else:
                                    dto_command = dto_command_base + [str(num_threads), str(0), str(num_iter), str(mem_buf_size), str(num_b), str(r)]
                                
                                if cpu_numa == 'same':
                                    dto_command += ['1', '1']
                                else:
                                    dto_command += ['2', '2']

                                if distribution_filepath is not None:
                                    dto_command = dto_command + [ distribution_filepath]

                                if run_taskset:
                                    dto_command = taskset_command + dto_command

                                if dry_run:
                                    print(dto_command)
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
                                    
                                    with open(outfile,'w') as f:
                                        
                                        if output_type == 'perf':
                                            dto_command = perf_command + dto_command
                                        
                                        if output_type == 'DTO_python':
                                                    f.write("'''\n")
                                                    f.flush()
                                        
                                        dto_process = subprocess.Popen(dto_command, env=dto_env, stdout=f, stderr=f) # stdout=1, stderr=1) #
                                        ret = dto_process.wait()
                                        time.sleep(5)
                                        f.flush()
                                
                                time.sleep(1)



                        for dsa_numa in ['same', 'diff']:
                            if single_case is not None:
                                sc_dsa_numa = single_case.split(',')[1]
                                if sc_dsa_numa != dsa_numa:
                                    continue

                            dto_env['DTO_IS_NUMA_AWARE']='1'
                            if dsa_numa == 'same':
                                dto_env['DTO_OPPOSITE_NUMA']='0'
                            else:
                                dto_env['DTO_OPPOSITE_NUMA']='1'

                            for perc in percentages:

                                if perc == 'nodsa':
                                    continue

                                if perc == 'auto':
                                    dto_env['DTO_USESTDC_CALLS']='0'
                                    dto_env['DTO_AUTO_ADJUST_KNOBS']='1'
                                    dto_env['DTO_CPU_SIZE_FRACTION']='0.33'
                                else:
                                    dto_env['DTO_USESTDC_CALLS']='0'
                                    dto_env['DTO_AUTO_ADJUST_KNOBS']='0'
                                    dto_env['DTO_CPU_SIZE_FRACTION']=perc

                                for size in sizes:
                                    if scale_buf_sizes_with_tx_size and size != mem_buf_size:
                                        continue
                                    output_name = '{}_{}_{}_{}_{}_{}_{}_{}_{}_{}K'.format(op_name,num_threads,cpu_numa,dsa_numa,perc.replace(".", "-"),alloc,r,num_b,mem_buf_size,size)

                                    print('processing {}'.format(output_name))

                                    if not dry_run:
                                        if not overwrite and os.path.exists(os.path.join(results_dir,'{}_DTO_config.json'.format(output_name))):
                                            continue

                                        with open(os.path.join(results_dir,'{}_DTO_config.json'.format(output_name)), 'w') as f:
                                            json.dump(dto_env, f)


                                    if alloc == 'single':
                                        alloc_size = mem_buf_size*num_b
                                        dto_command = dto_command_base + [str(num_threads), str(0), str(num_iter), str(alloc_size), '1', str(r)]
                                    else:
                                        dto_command = dto_command_base + [str(num_threads), str(0), str(num_iter), str(mem_buf_size), str(num_b), str(r)]
                                    
                                    if cpu_numa == 'same':
                                        dto_command += ['1', '1']
                                    else:
                                        dto_command += ['2', '2']
                                    
                                    if distribution_filepath is not None:
                                        dto_command = dto_command + [ distribution_filepath]

                                    if run_taskset:
                                        dto_command = taskset_command + dto_command

                                    if dry_run:
                                        print(dto_command)
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
                                        
                                        with open(outfile,'w') as f:
                                            if output_type == 'perf':
                                                dto_command = perf_command + dto_command
                                            
                                            if output_type == 'DTO_python':
                                                    f.write("'''\n")
                                                    f.flush()
                                            
                                            dto_process = subprocess.Popen(dto_command, env=dto_env, stdout=f, stderr=f) # stdout=1, stderr=1) #
                                            ret = dto_process.wait()
                                            time.sleep(5)
                                            f.flush()
                                    
                                    time.sleep(1)
                            

# memcpy operations
if MEMCOPY in mem_ops:
    mem_op = MEMCOPY
    op_name = 'cpy'

    for num_threads in thread_counts:
        taskset_command = ['taskset', '-c', '{}-{}'.format(taskset_startcpu,taskset_startcpu+num_threads)]
 
        for src_cpu_numa in ['same', 'diff']:
            for dst_cpu_numa in ['same', 'diff']:
                if single_case is not None:
                    sc_src_numa = single_case.split(',')[0]
                    sc_dst_numa = single_case.split(',')[1]
                    if sc_src_numa != src_cpu_numa or sc_dst_numa != dst_cpu_numa:
                        continue

                for num_b, mem_buf_size in zip(num_mem_bufs, mem_buf_sizes):
                    for alloc in alloc_types:
                        for r in rand_index_uses:
                            if not exclude_nodsa:
                                dto_env['DTO_USESTDC_CALLS']='1'
                                perc = 'nodsa'
                                dsa_numa = 'na'

                                for size in sizes:
                                    if scale_buf_sizes_with_tx_size and size != mem_buf_size:
                                        continue
                                    output_name = '{}_{}_{}_{}_{}_{}_{}_{}_{}_{}_{}K'.format(op_name,num_threads,src_cpu_numa,dst_cpu_numa,dsa_numa,perc.replace(".", "-"),alloc,r,num_b,mem_buf_size,size)

                                    print('processing {}'.format(output_name))

                                    if not dry_run:
                                        if not overwrite and os.path.exists(os.path.join(results_dir,'{}_DTO_config.json'.format(output_name))):
                                            continue

                                        with open(os.path.join(results_dir,'{}_DTO_config.json'.format(output_name)), 'w') as f:
                                            json.dump(dto_env, f)

                                    if alloc == 'single':
                                        alloc_size = mem_buf_size*num_b
                                        dto_command = dto_command_base + [str(num_threads), str(0), str(num_iter), str(alloc_size), '1', str(r)]
                                    else:
                                        dto_command = dto_command_base + [str(num_threads), str(0), str(num_iter), str(mem_buf_size), str(num_b), str(r)]
                                    
                                    if src_cpu_numa == 'same':
                                        dto_command += ['1']
                                    else:
                                        dto_command += ['2']

                                    if dst_cpu_numa == 'same':
                                        dto_command += ['1']
                                    else:
                                        dto_command += ['2']

                                    if distribution_filepath is not None:
                                            dto_command = dto_command + [ distribution_filepath]
                                    
                                    if run_taskset:
                                        dto_command = taskset_command + dto_command

                                    if dry_run:
                                        print(dto_command)
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
                                        
                                        with open(outfile,'w') as f:
                                            if output_type == 'perf':
                                                dto_command = perf_command + dto_command

                                            if output_type == 'DTO_python':
                                                    f.write("'''\n")
                                                    f.flush()

                                            dto_process = subprocess.Popen(dto_command, env=dto_env, stdout=f, stderr=f) # stdout=1, stderr=1) #
                                            ret = dto_process.wait()
                                            time.sleep(5)
                                            f.flush()
                                    
                                    time.sleep(1)

                            if src_cpu_numa == dst_cpu_numa:
                                dsa_numa_options = ['same', 'diff']
                            else:
                                dsa_numa_options = ['src', 'dst']

                            for dsa_numa in dsa_numa_options:
                                if single_case is not None:
                                    sc_dsa_numa = single_case.split(',')[2]
                                    if sc_dsa_numa != dsa_numa:
                                        continue

                                if dsa_numa == 'same':
                                    dto_env['DTO_OPPOSITE_NUMA']='0'
                                    dto_env['DTO_IS_NUMA_AWARE']='1'
                                elif dsa_numa == 'diff':
                                    dto_env['DTO_OPPOSITE_NUMA']='1'
                                    dto_env['DTO_IS_NUMA_AWARE']='1'
                                elif dsa_numa == 'src':
                                    dto_env['DTO_IS_NUMA_AWARE']='2'
                                    if src_cpu_numa == 'same':
                                        dto_env['DTO_OPPOSITE_NUMA']='0'
                                    else:
                                        dto_env['DTO_OPPOSITE_NUMA']='1'
                                else:
                                    dto_env['DTO_IS_NUMA_AWARE']='2'
                                    if dst_cpu_numa == 'same':
                                        dto_env['DTO_OPPOSITE_NUMA']='0'
                                    else:
                                        dto_env['DTO_OPPOSITE_NUMA']='1'                                

                                for perc in percentages:

                                    if perc == 'nodsa':
                                        continue

                                    if perc == 'auto':
                                        dto_env['DTO_USESTDC_CALLS']='0'
                                        dto_env['DTO_AUTO_ADJUST_KNOBS']='1'
                                        dto_env['DTO_CPU_SIZE_FRACTION']='0.33'
                                    else:
                                        dto_env['DTO_USESTDC_CALLS']='0'
                                        dto_env['DTO_AUTO_ADJUST_KNOBS']='0'
                                        dto_env['DTO_CPU_SIZE_FRACTION']=str(perc)

                                    for size in sizes:
                                        if scale_buf_sizes_with_tx_size and size != mem_buf_size:
                                            continue
                                        output_name = '{}_{}_{}_{}_{}_{}_{}_{}_{}_{}_{}K'.format(op_name,num_threads,src_cpu_numa,dst_cpu_numa,dsa_numa,perc.replace(".", "-"),alloc,r,num_b,mem_buf_size,size)

                                        print('processing {}'.format(output_name))

                                        if not dry_run:
                                            if not overwrite and os.path.exists(os.path.join(results_dir,'{}_DTO_config.json'.format(output_name))):
                                                continue

                                            with open(os.path.join(results_dir,'{}_DTO_config.json'.format(output_name)), 'w') as f:
                                                json.dump(dto_env, f)


                                        if alloc == 'single':
                                            alloc_size = mem_buf_size*num_b
                                            dto_command = dto_command_base + [str(num_threads), str(0), str(num_iter), str(alloc_size), '1', str(r)]
                                        else:
                                            dto_command = dto_command_base + [str(num_threads), str(0), str(num_iter), str(mem_buf_size), str(num_b), str(r)]
                                        
                                        if src_cpu_numa == 'same':
                                            dto_command += ['1']
                                        else:
                                            dto_command += ['2']

                                        if dst_cpu_numa == 'same':
                                            dto_command += ['1']
                                        else:
                                            dto_command += ['2']

                                        if distribution_filepath is not None:
                                            dto_command = dto_command + [ distribution_filepath]

                                        if run_taskset:
                                            dto_command = taskset_command + dto_command

                                        if dry_run:
                                            print(dto_command)
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
                                            
                                            with open(outfile,'w') as f:
                                                if output_type == 'perf':
                                                    dto_command = perf_command + dto_command
                                                
                                                if output_type == 'DTO_python':
                                                    f.write("'''\n")
                                                    f.flush()

                                                dto_process = subprocess.Popen(dto_command, env=dto_env, stdout=f, stderr=f) # stdout=1, stderr=1) #
                                                ret = dto_process.wait()
                                                time.sleep(5)
                                                f.flush()
                                        
                                        time.sleep(1)
