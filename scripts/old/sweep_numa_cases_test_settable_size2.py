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
parser.add_argument('--num-iter', type=int, default=10000000, help='number of iterations')
parser.add_argument('--cfg-filepath', type=str, default=None, help='cofiguration file')
parser.add_argument('--dry-run', action='store_true', help='perform a dryrun')
parser.add_argument('--non-split-only', action='store_true', help='for memcpy run only tests where src/dst are allocated on same numa')

# Parse the arguments
args = parser.parse_args()

MEMSET = 1
MEMCOPY = 2    
MEMMOVE = 4       
MEMCMP = 8

dto_command_base = ['../dto-test-settable-size-dev2']

run_taskset = True
dry_run = args.dry_run   #False

# Output choices are: 'DTO_python' or  'emon'  or 'perf'
output_type = args.output_type  #'perf'  #'emon' # 'DTO_python'  #  'perf'
cfg_filepath = args.cfg_filepath
non_split_only = args.non_split_only

scale_buf_sizes_with_tx_size = True
total_buf_sizes = [8*1024] #[64, 8*1024]  # in MB

results_dir = '/home/jjsydir/internal_dto/projects.research.dto_dev/scripts/results/sweepnumacases_no-cache_perf_041425_172013'
#results_dir = ''

run_name = args.run_name  #'test_perf'

num_threads=args.num_threads
num_dsas=8
num_iter = args.num_iter  #10000000 #1000000
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

cache_control = 1 #0

alloc_types = ['individual'] #['single', 'individual']

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
    "num_threads":num_threads,
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
    "cache_control":cache_control,
}

if cfg_filepath is not None:
    if not os.path.exists(cfg_filepath):
            print("error input file {} not found".format(cfg_filepath))
            sys.exit(0)

    with open(cfg_filepath, 'r') as f:
        loaded_config = json.load(f)

    print(loaded_config)

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

    if "cache_control" in loaded_config:
        cache_control = config["cache_control"] = loaded_config["cache_control"]


if scale_buf_sizes_with_tx_size:
    mem_buf_sizes = config["mem_buf_sizes"] = sizes*len(total_buf_sizes)
    num_mem_bufs = config["num_mem_bufs"] = [int(t*1024/m) for t in total_buf_sizes for m in sizes ]
else: 
    if len(mem_buf_sizes) == 1 and len(num_mem_bufs) > 1:
        mem_buf_sizes = mem_buf_sizes * len(num_mem_bufs)


name = 'sweepnumacases_{}_{}'.format(run_name, output_type)

results_root = './results'

if not dry_run:
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
dto_env['DTO_WAIT_METHOD']= 'yield' # 'umwait' # 'busypoll' # 'umwait' #  'yield' # 'sleep' #   
dto_env['DTO_MIN_BYTES']='8192'  #'65536'#    
#dto_env['DTO_CPU_SIZE_FRACTION']= '0.1' # '0.33' #
dto_env['DTO_AUTO_ADJUST_KNOBS']='0'
dto_env['DTO_MAKE_ADJ'] = '1'
dto_env['DTO_DSA_CC']=str(cache_control)
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
    #dto_env['DTO_STATS_PREFIX']="'''"
    dto_env['DTO_STATS_PREFIX']=""
    dto_env['DTO_STATS_OUTPUT_TYPE']='1'
    dto_env['DTO_COLLECT_STATS']='1'
elif output_type == 'perf':
    dto_env['DTO_STATS_OUTPUT_TYPE']='0'
    dto_env['DTO_COLLECT_STATS']='1'

# memset operations
if MEMSET in mem_ops:
    mem_op = MEMSET  #MEMCOPY
    op_name = 'set' # 'cpy'

    for cpu_numa in ['same', 'diff']:
        for num_b, mem_buf_size in zip(num_mem_bufs, mem_buf_sizes):
            for alloc in alloc_types:
                for r in rand_index_uses:
                    dto_env['DTO_USESTDC_CALLS']='1'
                    perc = 'nodsa'
                    dsa_numa = 'na'

                    for size in sizes:
                        if scale_buf_sizes_with_tx_size and size != mem_buf_size:
                            continue
                        
                        output_name = '{}_{}_{}_{}_{}_{}_{}_{}_{}K'.format(op_name,cpu_numa,dsa_numa,perc.replace(".", "-"),alloc,r,num_b,mem_buf_size,size)

                        print('processing {}'.format(output_name))

                        if not dry_run:
                            if os.path.exists(os.path.join(results_dir,'{}_DTO_config.json'.format(output_name))):
                                continue

                            with open(os.path.join(results_dir,'{}_DTO_config.json'.format(output_name)), 'w') as f:
                                json.dump(dto_env, f)


                        if alloc == 'single':
                            alloc_size = mem_buf_size*num_b
                            dto_command = dto_command_base + [str(num_threads), str(0), str(mem_op), str(size), str(num_iter), str(alloc_size), '1', '0']
                        else:
                            dto_command = dto_command_base + [str(num_threads), str(0), str(mem_op), str(size), str(num_iter), str(mem_buf_size), str(num_b), str(r)]
                        

                        if cpu_numa == 'same':
                            dto_command += ['1', '1']
                        else:
                            dto_command += ['2', '2']

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

                                dto_process = subprocess.Popen(dto_command, env=dto_env, stdout=f, stderr=f) # stdout=1, stderr=1) #
                                ret = dto_process.wait()
                                time.sleep(5)
                                f.flush()
                        
                        time.sleep(1)



                    for dsa_numa in ['same', 'diff']:
                        dto_env['DTO_IS_NUMA_AWARE']='1'
                        if dsa_numa == 'same':
                            dto_env['DTO_OPPOSITE_NUMA']='0'
                        else:
                            dto_env['DTO_OPPOSITE_NUMA']='1'

                        for perc in percentages:

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
                                output_name = '{}_{}_{}_{}_{}_{}_{}_{}_{}K'.format(op_name,cpu_numa,dsa_numa,perc.replace(".", "-"),alloc,r,num_b,mem_buf_size,size)

                                print('processing {}'.format(output_name))

                                if not dry_run:
                                    if os.path.exists(os.path.join(results_dir,'{}_DTO_config.json'.format(output_name))):
                                        continue

                                    with open(os.path.join(results_dir,'{}_DTO_config.json'.format(output_name)), 'w') as f:
                                        json.dump(dto_env, f)


                                if alloc == 'single':
                                    alloc_size = mem_buf_size*num_b
                                    dto_command = dto_command_base + [str(num_threads), str(0), str(mem_op), str(size), str(num_iter), str(alloc_size), '1', '0']
                                else:
                                    dto_command = dto_command_base + [str(num_threads), str(0), str(mem_op), str(size), str(num_iter), str(mem_buf_size), str(num_b), str(r)]
                                
                                if cpu_numa == 'same':
                                    dto_command += ['1', '1']
                                else:
                                    dto_command += ['2', '2']

                                if run_taskset:
                                    dto_command = taskset_command + dto_command

                                if dry_run:
                                    #print(dto_command)
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

                                        dto_process = subprocess.Popen(dto_command, env=dto_env, stdout=f, stderr=f) # stdout=1, stderr=1) #
                                        ret = dto_process.wait()
                                        time.sleep(5)
                                        f.flush()
                                
                                time.sleep(1)
                            

# memcpy operations
if MEMCOPY in mem_ops:
    mem_op = MEMCOPY
    op_name = 'cpy'

    for src_cpu_numa in ['same', 'diff']:
        for dst_cpu_numa in ['same', 'diff']:
            if non_split_only and src_cpu_numa != dst_cpu_numa:
                continue
            for num_b, mem_buf_size in zip(num_mem_bufs, mem_buf_sizes):
                for alloc in alloc_types:
                    for r in rand_index_uses:
                        dto_env['DTO_USESTDC_CALLS']='1'
                        perc = 'nodsa'
                        dsa_numa = 'na'

                        for size in sizes:
                            if scale_buf_sizes_with_tx_size and size != mem_buf_size:
                                continue
                            output_name = '{}_{}_{}_{}_{}_{}_{}_{}_{}_{}K'.format(op_name,src_cpu_numa,dst_cpu_numa,dsa_numa,perc.replace(".", "-"),alloc,r,num_b,mem_buf_size,size)

                            print('processing {}'.format(output_name))

                            if not dry_run:
                                if os.path.exists(os.path.join(results_dir,'{}_DTO_config.json'.format(output_name))):
                                    continue

                                with open(os.path.join(results_dir,'{}_DTO_config.json'.format(output_name)), 'w') as f:
                                    json.dump(dto_env, f)


                            if alloc == 'single':
                                alloc_size = mem_buf_size*num_b
                                dto_command = dto_command_base + [str(num_threads), str(0), str(mem_op), str(size), str(num_iter), str(alloc_size), '1', '0']
                            else:
                                dto_command = dto_command_base + [str(num_threads), str(0), str(mem_op), str(size), str(num_iter), str(mem_buf_size), str(num_b), str(r)]
                            
                            if src_cpu_numa == 'same':
                                dto_command += ['1']
                            else:
                                dto_command += ['2']

                            if dst_cpu_numa == 'same':
                                dto_command += ['1']
                            else:
                                dto_command += ['2']
                            
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
                                    output_name = '{}_{}_{}_{}_{}_{}_{}_{}_{}_{}K'.format(op_name,src_cpu_numa,dst_cpu_numa,dsa_numa,perc.replace(".", "-"),alloc,r,num_b,mem_buf_size,size)

                                    print('processing {}'.format(output_name))

                                    if not dry_run:
                                        if os.path.exists(os.path.join(results_dir,'{}_DTO_config.json'.format(output_name))):
                                            continue

                                        with open(os.path.join(results_dir,'{}_DTO_config.json'.format(output_name)), 'w') as f:
                                            json.dump(dto_env, f)


                                    if alloc == 'single':
                                        alloc_size = mem_buf_size*num_b
                                        dto_command = dto_command_base + [str(num_threads), str(0), str(mem_op), str(size), str(num_iter), str(alloc_size), '1', '0']
                                    else:
                                        dto_command = dto_command_base + [str(num_threads), str(0), str(mem_op), str(size), str(num_iter), str(mem_buf_size), str(num_b), str(r)]
                                    
                                    if src_cpu_numa == 'same':
                                        dto_command += ['1']
                                    else:
                                        dto_command += ['2']

                                    if dst_cpu_numa == 'same':
                                        dto_command += ['1']
                                    else:
                                        dto_command += ['2']

                                    if run_taskset:
                                        dto_command = taskset_command + dto_command

                                    if dry_run:
                                        #print(dto_command)
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

                                            dto_process = subprocess.Popen(dto_command, env=dto_env, stdout=f, stderr=f) # stdout=1, stderr=1) #
                                            ret = dto_process.wait()
                                            time.sleep(5)
                                            f.flush()
                                    
                                    time.sleep(1)
