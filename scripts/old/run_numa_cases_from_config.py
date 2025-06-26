# ==========================================================================
# Copyright (C) 2024 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==========================================================================

import subprocess, os, time, re, time, json, glob

import argparse

dry_run = False

# Create an ArgumentParser object
parser = argparse.ArgumentParser()

# Add arguments
parser.add_argument('--input-filepath', type=str, default=None, help='config file path used as input')
parser.add_argument('--input-dirpath', type=str, default=None, help='config dirctory path used as input')
parser.add_argument('--input-searchstr', type=str, default=None, help='search string for input files')
parser.add_argument('--output-type', type=str, default='perf', help='output type: perf | DTO_python | emon | stdout_perf | stdout_time | stdout_none')
parser.add_argument('--dto-stats', action='store_true', help='run perf stat - used when ouput_type=other')
parser.add_argument('--dto-algstats', action='store_true', help='run perf stat - used when ouput_type=other')
parser.add_argument('--run-name', type=str, default='test', help='name to be used in output directory')
parser.add_argument('--num-threads', type=int, default=1, help='number of threads')
parser.add_argument('--num-iter', type=int, default=10000000, help='number of iterations')
parser.add_argument('--exclude-nodsa', action='store_true', help='skip nodsa cases')

# Parse the arguments
args = parser.parse_args()

input_dirpath = args.input_dirpath

if input_dirpath is not None:
    if args.input_filepath is not None:
        print("error: need to specify either input filepath or input dirpath but not both")

    if args.input_searchstr is None:
        input_searchstr = '*DTO_config.json'
    else:
        print (args.input_searchstr)
        input_searchstr = args.input_searchstr

    input_files = [file for file in sorted(glob.glob(os.path.join(input_dirpath, input_searchstr)))]

else:
    if args.input_filepath is None:
        print("error: need to specify at least one of input filepath or input dirpath")
    else:
        input_files = [args.input_filepath]


MEMSET = 1
MEMCOPY = 2    
MEMMOVE = 4       
MEMCMP = 8

op_map = {'set':MEMSET, 'cpy':MEMCOPY, 'mov':MEMMOVE, 'cmp':MEMCMP}

dto_command_base = ['../dto-test-settable-size-dev2']

run_taskset = True


# Output choices are: 'DTO_python' or  'emon'  or 'perf'
output_type = args.output_type  #'perf'  #'emon' # 'DTO_python'  #  'perf'

dto_stats = args.dto_stats
dto_algstats = args.dto_algstats
exclude_nodsa = args.exclude_nodsa

run_name = args.run_name  #'test_perf'

num_threads=args.num_threads
num_dsas=8
num_iter = args.num_iter  #10000000 
taskset_startcpu = 56

taskset_command = ['taskset', '-c', '{}-{}'.format(taskset_startcpu,taskset_startcpu+num_threads)]

perf_command = ['perf', 'stat']

emon_command = ['sudo', '/opt/intel/sep/bin64/emon', '-collect-edp', 'edp_file=/opt/intel/sep/config/edp/sapphirerapids_server_events.txt']

time_command = ['/usr/bin/time']

if output_type not in ['stdout_perf', 'stdout_time', 'stdout_none']:

    name = 'runnumacases_{}_{}'.format(run_name, output_type)

    results_root = './results'

    if not dry_run:
        sync_id = time.strftime('%m%d%y_%H%M%S', time.localtime())
        results_dir = os.path.join(results_root,name+'_'+sync_id)
        if not os.path.exists(results_dir):
            os.makedirs(results_dir)   

for input_filepath in input_files:
    #print(input_filepath)

    if not os.path.exists(input_filepath):
        print("error input file {} not found".format(input_filepath))
        sys.exit(0)

    with open(input_filepath, 'r') as f:
        dto_env = json.load(f)

    _,fn = os.path.split(input_filepath)
    fn,_ = os.path.splitext(fn)
    #print(fn)
    pieces = fn.split('_')

    op_name = pieces[0]
    mem_op = op_map[op_name] 

    if op_name == 'set':
        #output_name = '{}_{}_{}_{}_{}_{}_{}_{}_{}K'.format(op_name,cpu_numa,dsa_numa,perc.replace(".", "-"),alloc,r,num_b,mem_buf_size,size)
        cpu_numa = pieces[1]
        dsa_numa = pieces[2]
        perc = pieces[3].replace( "-",".")
        alloc = pieces[4]
        r = pieces[5]
        num_b = pieces[6]
        mem_buf_size = pieces[7]
        size = pieces[8][:-1]

        output_name = '{}_{}_{}_{}_{}_{}_{}_{}_{}K'.format(op_name,cpu_numa,dsa_numa,perc.replace(".", "-"),alloc,r,num_b,mem_buf_size,size)


        if alloc == 'single':
            alloc_size = mem_buf_size*num_b
            dto_command = dto_command_base + [str(num_threads), str(0), str(mem_op), size, str(num_iter), str(alloc_size), '1', '0']
        else:
            dto_command = dto_command_base + [str(num_threads), str(0), str(mem_op), size, str(num_iter), mem_buf_size, num_b, r]

        if cpu_numa == 'same':
            dto_command += ['1', '1']
        else:
            dto_command += ['2', '2']


    elif op_name == 'cpy':
        #output_name = '{}_{}_{}_{}_{}_{}_{}_{}_{}_{}K'.format(op_name,src_cpu_numa,dst_cpu_numa,dsa_numa,perc.replace(".", "-"),alloc,r,num_b,mem_buf_size,size)
        src_cpu_numa = pieces[1]
        dst_cpu_numa = pieces[2]
        dsa_numa = pieces[3]
        perc = pieces[4].replace( "-",".")
        alloc = pieces[5]
        r = pieces[6]
        num_b = pieces[7]

        if len(pieces) == 9:
            mem_buf_size = size = pieces[8][:-1]
        else:
            mem_buf_size = pieces[8]
            size = pieces[9][:-1]

        output_name = '{}_{}_{}_{}_{}_{}_{}_{}_{}_{}K'.format(op_name,src_cpu_numa,dst_cpu_numa,dsa_numa,perc.replace(".", "-"),alloc,r,num_b,mem_buf_size,size)

        if alloc == 'single':
            alloc_size = mem_buf_size*num_b
            dto_command = dto_command_base + [str(num_threads), str(0), str(mem_op), size, str(num_iter), str(alloc_size), '1', '0']
        else:
            dto_command = dto_command_base + [str(num_threads), str(0), str(mem_op), size, str(num_iter), mem_buf_size, num_b, r]
        
        if src_cpu_numa == 'same':
            dto_command += ['1']
        else:
            dto_command += ['2']

        if dst_cpu_numa == 'same':
            dto_command += ['1']
        else:
            dto_command += ['2']

    if exclude_nodsa and perc == 'nodsa':
        continue

    if run_taskset:
        dto_command = taskset_command + dto_command

    if dry_run:
        print(output_name)
        #print(dto_command)
        continue

    if output_type not in ['stdout_perf', 'stdout_time', 'stdout_none']:
        if not dry_run:
            with open(os.path.join(results_dir,'{}_DTO_config.json'.format(output_name)), 'w') as f:
                json.dump(dto_env, f)

    if output_type == 'emon':
        outfile = os.path.join(results_dir,'{}_{}.emon.txt'.format(name,output_name))
        errfile = os.path.join(results_dir,'{}_{}.stderr.txt'.format(name,output_name))

        of = open(outfile,'w')
        ef = open(errfile, 'w')

        dto_process = subprocess.Popen(dto_command, env=dto_env, stdout=1, stderr=1)
        time.sleep(5)

        emon_process = subprocess.Popen(emon_command, stdout=of, stderr=ef)
        time.sleep(120)

        kill_process = subprocess.Popen(['sudo', 'pkill', 'emon'])
        kill_dto = subprocess.Popen(['pkill', dto_command_base[0].split('/')[-1]])
        ret = kill_process.wait()
        ret = kill_dto.wait()

        emon_process.kill()
        dto_process.kill()
        of.close()
        ef.close()

    elif output_type in ['perf', 'DTO_python']:
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

    else:
        if dto_stats:
            dto_env['DTO_COLLECT_STATS']='1'
            if dto_env['DTO_LOG_LEVEL'] != '3':
                dto_env['DTO_LOG_LEVEL']='2'
        if dto_algstats:
            dto_env['DTO_COLLECT_STATS']='1'
            dto_env['DTO_STATS_OUTPUT_TYPE']='1'
            if dto_env['DTO_LOG_LEVEL'] != '3':
                dto_env['DTO_LOG_LEVEL']='2'

        if output_type == 'stdout_perf':
            dto_command = perf_command + dto_command
        elif output_type == 'stdout_time':
            dto_command = time_command + dto_command

        dto_process = subprocess.Popen(dto_command, env=dto_env, stdout=1, stderr=1)

        
