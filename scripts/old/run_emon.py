# ==========================================================================
# Copyright (C) 2024 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==========================================================================

import subprocess, os, time, re, time


bg_command = []
dto_command_base = ['/home/jjsydir/internal_dto/projects.research.dto_dev/dto-test-settable-size']
run_taskset = True
python_output = False

bg_name = 'none'

nums_threads =  [50]  #[10,30,40,50]  #[40,50] #] [20] #[1, 10]  #, 16, 32]   #[1, 2, 4, 8, 16, 32]  #[32]  #[2, 4, 8, 16] #  

num_iter = 100000000000

percentages =   ['nodsa',  0.0]  # ['nodsa', 'auto', 0.0, 0.1, 0.33]  # ['auto']  #
percentage_names = ['nodsa',  'cpu0']

operations = [1] # [1,2,4]
op_sizes = [256]  #[16,32,64,128,256] # [512, 1024, 2048]  #   [8,16,32,64,128,256] #,[8,16,32,64,128,256]

emon_command = ['sudo', '/opt/intel/sep/bin64/emon', '-collect-edp', 'edp_file=/opt/intel/sep/config/edp/sapphirerapids_server_events.txt']

name = 'emon_test_settable'

base_env = os.environ.copy()
dto_env = os.environ.copy()

dto_env['DTO_USESTDC_CALLS']='0'
dto_env['DTO_COLLECT_STATS']='0'
dto_env['DTO_WAIT_METHOD']='yield' # 'sleep' #   
dto_env['DTO_MIN_BYTES']='8192'  # '32768'   # '65536'#    
#dto_env['DTO_CPU_SIZE_FRACTION']= '0.1' # '0.33' #
#dto_env['DTO_AUTO_ADJUST_KNOBS']='0'
dto_env['DTO_MAKE_ADJ'] = '1'
dto_env['DTO_DSA_CC']='0'
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
#dto_env['LD_LIBRARY_PATH']='../'
dto_env['LD_LIBRARY_PATH']='/home/jjsydir/internal_dto/projects.research.dto_dev'

if python_output:
    dto_env['DTO_STATS_PREFIX']="'''"
    dto_env['DTO_STATS_OUTPUT_TYPE']='1'
else:
    dto_env['DTO_STATS_OUTPUT_TYPE']='0'

results_root = './results'

sync_id = time.strftime('%m%d%y_%H%M%S', time.localtime())
results_dir = os.path.join(results_root,name+'_'+sync_id)
if not os.path.exists(results_dir):
    os.makedirs(results_dir)

#results_dir = '/home/jjsydir/internal_dto/projects.research.dto_dev/scripts/results/emon_test_settable_013125_111458'

for num_threads in nums_threads:
    for op_size in op_sizes:
        for mem_op in operations:
            dto_command = dto_command_base + [str(num_threads),'0', str(mem_op), str(op_size), str(num_iter)  ]

            if num_threads == 1:
                cores = '56'
            else:
                cores='56-{}'.format(56+num_threads-1)

            taskset_command = ['taskset', '-c', cores]

            if run_taskset:
                dto_command = taskset_command + dto_command

            print(dto_command)

            dto_env['DTO_USESTDC_CALLS']='0'
            for perc, perc_name in zip(percentages, percentage_names):
                print("num threads {} op size {} mem op {} percentage {}".format(num_threads, op_size, mem_op, perc))
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
                
                outfile = os.path.join(results_dir,'test_{}_{}_{}_{}.emon.txt'.format(num_threads, op_size, mem_op, perc_name))
                errfile = os.path.join(results_dir,'test_{}_{}_{}_{}.stderr.txt'.format(num_threads, op_size, mem_op, perc_name))
                #appfile = os.path.join(results_dir,'test_{}_{}_{}_{}.output.txt'.format(num_threads, op_size, mem_op, perc_name))
                of = open(outfile,'w')
                ef = open(errfile, 'w')
                #af = open(appfile, 'w')

                dto_process = subprocess.Popen(dto_command, env=dto_env, stdout=1, stderr=1)
                #time.sleep(10)

                emon_process = subprocess.Popen(emon_command, stdout=of, stderr=ef)
                time.sleep(30)

                kill_process = subprocess.Popen(['sudo', 'pkill', 'emon'])
                kill_dto = subprocess.Popen(['pkill', dto_command_base[0].split('/')[-1]])
                ret = kill_process.wait()
                ret = kill_dto.wait()

                emon_process.kill()
                dto_process.kill()
                of.close()
                ef.close()
                #af.flush()
                #af.close()
            
                time.sleep(10)



