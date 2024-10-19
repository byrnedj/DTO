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
dto_command_base = ['../dto-test-settable-size']
run_perf = True
cores = []

bg_name = 'none'
dto_name = 'dtotest-settable'

num_threads=1
num_dsas=1

min_size = 8
max_size = 2048
size_step = 2
#sizes = [2**x for x in range(3,12)]
sizes = [8]
sleep_times = [1,1,1,2,4,16,16,40,128]

wait_methods = ['umwait']  #,'sleep', 'yield']

num_reps = 1

min_perc=0.1
max_perc=0.1
perc_step = 0.2

percentages = [0.0]  #, 0.1, 0.33]

mem_ops = [MEMSET,MEMCOPY,MEMMOVE]
mem_op_names = ['set', 'cpy', 'mov']

num_iter = 100  #1000000

#perf_command = 'perf stat -e dsa0/event=0x1,event_category=0x0/,dsa2/event=0x1,event_category=0x0/,dsa4/event=0x1,event_category=0x0/,dsa6/event=0x1,event_category=0x0/,dsa0/event=0x1,event_category=0x1/,dsa2/event=0x1,event_category=0x1/,dsa4/event=0x1,event_category=0x1/,dsa6/event=0x1,event_category=0x1/,dsa0/event=0x2,event_category=0x1/,dsa2/event=0x2,event_category=0x1/,dsa4/event=0x2,event_category=0x1/,dsa6/event=0x2,event_category=0x1/'
perf_command = ['perf', 'stat']

#if len(cores) > 0:
#    if not type(cores[0]) == str:
#        cores_str = ['{}'.format(c) for c in cores]
#        cores = cores_str
#    dto_command = 'numactl -C ' + ','.join(cores) + ' ' + dto_command


#if run_perf:
#    dto_command = perf_command + ' ' + dto_command


name = 'sweeptxsize_perc_minsize8K_1M_iterations_separated_ops'
#name = 'sweeptxsize_nostatscomp_waitsleep_cpufract0.1_minsize8K'
#name = 'sweeptxsize_nostatscomp_nodsa_minsize8K'
#name = 'test'

base_env = os.environ.copy()
dto_env = os.environ.copy()

#dto_env['DTO_USESTDC_CALLS']='0'
dto_env['DTO_COLLECT_STATS']='0'
#dto_env['DTO_WAIT_METHOD']='yield' # 'sleep' #   
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


results_root = './results'

sync_id = time.strftime('%m%d%y_%H%M%S', time.localtime())
results_dir = os.path.join(results_root,name+'_'+sync_id)
if not os.path.exists(results_dir):
    os.makedirs(results_dir)


bg_command = []
if bg_command != []:
    bg_process = subprocess.Popen(bg_command, env=base_env)


for mem_op, op_name in zip(mem_ops,mem_op_names):

    dto_env['DTO_USESTDC_CALLS']='1'
    fn= 'results_{}_{}_nodsa.txt'.format(name,op_name)
    with open(os.path.join(results_dir,fn),'w') as f:

        for size,sleep_time in zip(sizes, sleep_times):
            dto_command = dto_command_base + [str(num_threads), str(mem_op), str(size), str(num_iter)]
            if run_perf:
                dto_command = perf_command + dto_command

            print("processing size {}K {} iterations".format(size,num_iter))
            f.write("\n\nprocessing size {}K\n".format(size))
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

    time.sleep(1)

    out_fn= 'results_{}_{}_nodsa_summary.txt'.format(name,op_name)
    with open(os.path.join(results_dir,fn), "r") as origin_file:
        with open(os.path.join(results_dir,out_fn), "w") as out:
            for line in origin_file:
                line1 = re.findall(r'processing', line)
                if line1:
                    out.write(line)
                else:
                    line1 = re.findall(r'cycles', line)
                    if line1:
                        out.write(line)


    dto_env['DTO_USESTDC_CALLS']='0'
    for wait_method in wait_methods:
        dto_env['DTO_WAIT_METHOD'] = wait_method
        for perc in percentages:

            dto_env['DTO_CPU_SIZE_FRACTION']=str(perc)

            print("processing {} {}".format(wait_method,perc))

            fn= 'results_{}_{}_{}_cpufrace{}.txt'.format(name,op_name, wait_method,perc)
            with open(os.path.join(results_dir,fn),'w') as f:

                for size,sleep_time in zip(sizes, sleep_times):
                    dto_command = dto_command_base + [str(num_threads), str(mem_op), str(size), str(num_iter)]
                    if run_perf:
                        dto_command = perf_command + dto_command

                    print("processing size {}K {} iterations".format(size,num_iter))
                    f.write("\n\nprocessing size {}K\n".format(size))
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

            out_fn = 'results_{}_{}_{}_cpufrace{}_summary.txt'.format(name,op_name,wait_method,perc)

            time.sleep(1)

            with open(os.path.join(results_dir,fn), "r") as origin_file:
                with open(os.path.join(results_dir,out_fn), "w") as out:
                    for line in origin_file:
                        line1 = re.findall(r'processing', line)
                        if line1:
                            out.write(line)
                        else:
                            line1 = re.findall(r'cycles', line)
                            if line1:
                                out.write(line)


    dto_env['DTO_USESTDC_CALLS']='0'
    dto_env['DTO_CPU_SIZE_FRACTION']= '0.33' 
    dto_env['DTO_AUTO_ADJUST_KNOBS']='1'
    dto_env['DTO_WAIT_METHOD'] = 'yield'

    fn= 'results_{}_{}_autocpufract.txt'.format(name,op_name)
    with open(os.path.join(results_dir,fn),'w') as f:

        for size,sleep_time in zip(sizes, sleep_times):
            dto_command = dto_command_base + [str(num_threads), str(mem_op), str(size), str(num_iter)]
            if run_perf:
                dto_command = perf_command + dto_command

            print("processing size {}K {} iterations".format(size,num_iter))
            f.write("\n\nprocessing size {}K\n".format(size))
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

    time.sleep(1)

    out_fn= 'results_{}_{}_autocpufract_summary.txt'.format(name,op_name)
    with open(os.path.join(results_dir,fn), "r") as origin_file:
        with open(os.path.join(results_dir,out_fn), "w") as out:
            for line in origin_file:
                line1 = re.findall(r'processing', line)
                if line1:
                    out.write(line)
                else:
                    line1 = re.findall(r'cycles', line)
                    if line1:
                        out.write(line)

if bg_command != []:
    bg_process.kill()
