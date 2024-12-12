# ==========================================================================
# Copyright (C) 2024 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==========================================================================

import subprocess, os, time, re, time


bg_command = []
dto_command_base = ['../dto-test-distribution']
run_perf = True
cores = []

bg_name = 'none'
dto_name = 'dto-test-distribution'

distribution_input_files = ['../salesforce_all_ops.csv']
#distribution_input_files = ['../bimodal-8K-512K_bysize.csv']
#distribution_input_files = ['../bimodal-8K-32K.csv']
#distribution_input_files = ['../bimodal-8K-64K.csv', '../bimodal-8K-128K.csv']

num_threads=1
num_dsas=1

num_reps = 1

include_no_dsa_case = True

#percentages = np.arange(min_perc,max_perc+perc_step,perc_step)
percentages = ['auto']  #, [ 0.0, 0.1, 0.33]  #

min_sizes = [32, 36, 40, 64, 128] # [8, 16]  # [32, 36, 40, 64, 128] # [12,16,20,24]  #[8,28]

num_iter = 500000000  # 5B
#num_iter = 50000000   # 500M
#num_iter = 10000000   # 100M
#num_iter = 1000000   # 10M

iter_name = '5B'  # '100M' #  

#perf_command = 'perf stat -e dsa0/event=0x1,event_category=0x0/,dsa2/event=0x1,event_category=0x0/,dsa4/event=0x1,event_category=0x0/,dsa6/event=0x1,event_category=0x0/,dsa0/event=0x1,event_category=0x1/,dsa2/event=0x1,event_category=0x1/,dsa4/event=0x1,event_category=0x1/,dsa6/event=0x1,event_category=0x1/,dsa0/event=0x2,event_category=0x1/,dsa2/event=0x2,event_category=0x1/,dsa4/event=0x2,event_category=0x1/,dsa6/event=0x2,event_category=0x1/'
perf_command = ['perf', 'stat']

#if len(cores) > 0:
#    if not type(cores[0]) == str:
#        cores_str = ['{}'.format(c) for c in cores]
#        cores = cores_str
#    dto_command = 'numactl -C ' + ','.join(cores) + ' ' + dto_command


#if run_perf:
#    dto_command = perf_command + ' ' + dto_command

name = 'test_dist2_{}'.format(iter_name)
#name = 'test_distribution_100M_iterations_bimodal_2'
#name = 'test_distribution_5B_iterations_3'
#name = 'sweeptxsize_nostatscomp_waitsleep_cpufract0.1_minsize8K'
#name = 'sweeptxsize_nostatscomp_nodsa_minsize8K'
#name = 'test'

base_env = os.environ.copy()
dto_env = os.environ.copy()

dto_env['DTO_USESTDC_CALLS']='0'
#dto_env['DTO_COLLECT_STATS']='1'
dto_env['DTO_WAIT_METHOD']='yield' # 'sleep' #   
#dto_env['DTO_MIN_BYTES']='8192'  #'65536'#    
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


results_root = './results'

sync_id = time.strftime('%m%d%y_%H%M%S', time.localtime())
results_dir = os.path.join(results_root,name+'_'+sync_id)
if not os.path.exists(results_dir):
    os.makedirs(results_dir)


bg_command = []
if bg_command != []:
    bg_process = subprocess.Popen(bg_command, env=base_env)

for distribution_input_file in distribution_input_files:

    dist_name = os.path.splitext(os.path.basename(distribution_input_file))[0]

    dto_command = dto_command_base + [str(num_iter), distribution_input_file]
    if run_perf:
        dto_command = perf_command + dto_command

    for min_size in min_sizes:
        dto_env['DTO_MIN_BYTES']='{}'.format(min_size*1024)

        if include_no_dsa_case:
            dto_env['DTO_USESTDC_CALLS']='1'
            fn= 'results_{}_{}_{}K_nodsa.txt'.format(name, dist_name,min_size)
            print("processing {} distribution {}K no DSA".format(dist_name, min_size))
            with open(os.path.join(results_dir,fn),'w') as f:        
                for i in range(num_reps):
                    print('step {} out of {}'.format(i+1,num_reps))
                    f.write('\nstep {} out of {}\n'.format(i+1,num_reps))
                    f.flush()
                    dto_process = subprocess.Popen(dto_command, env=dto_env, stdout=f, stderr=f) 
                    ret = dto_process.wait()
                    time.sleep(5)
                    f.flush()

            time.sleep(1)

            """
            out_fn= 'results_{}_{}K_nodsa_summary.txt'.format(name, min_size)
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
            """

        dto_env['DTO_USESTDC_CALLS']='0'
        for perc in percentages:
            if perc == 'auto':
                dto_env['DTO_AUTO_ADJUST_KNOBS']='1'
                dto_env['DTO_CPU_SIZE_FRACTION']='0.33'
                print("processing {} distribution {}K auto cpu fraction".format(dist_name, min_size))
                
            else:
                dto_env['DTO_AUTO_ADJUST_KNOBS']='0'
                dto_env['DTO_CPU_SIZE_FRACTION']=str(perc)
                print("processing {} distribution {}K {} cpu fraction".format(dist_name, min_size, perc))
            

            fn= 'results_{}_{}_{}K_{}.txt'.format(name, dist_name, min_size,perc)
            #out_fn= 'results_{}_{}K_{}_summary.txt'.format(name, min_size,perc)

            with open(os.path.join(results_dir,fn),'w') as f:            
                for i in range(num_reps):
                    print('step {} out of {}'.format(i+1,num_reps))
                    f.write('\nstep {} out of {}\n'.format(i+1,num_reps))
                    f.flush()
                    dto_process = subprocess.Popen(dto_command, env=dto_env, stdout=f, stderr=f) 
                    ret = dto_process.wait()
                    time.sleep(5)
                    f.flush()
                        
            time.sleep(1)

            """
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
            """



if bg_command != []:
    bg_process.kill()

