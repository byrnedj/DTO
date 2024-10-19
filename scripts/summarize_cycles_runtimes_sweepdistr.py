# -*- coding: utf-8 -*-
"""
Created on Tue Oct 15 10:11:10 2024

@author: jjsydir
"""

import glob
import os
import re

directory = './results/distribution_2'

#summary_type = 'runtimes'
#summary_type = 'cycles'

summary_types =  ['runtimes','cycles']

cpu_percentages = [ 'auto','0.0', '0.1', '0.33']
min_sizes = ['8K', '28K']


def write_output(file, of, summary_type, st='', ed=''):
    output = []
    with open(file, "r") as f:
        for line in f:
            if summary_type == 'cycles':
                line1 = re.findall(r'cycles', line)
            else:
                line1 = re.findall(r'task-clock', line)
            if line1:
                fields = line.split()
                if summary_type == 'cycles':
                    output.append(str(int(fields[0].replace(',',''))))
                else:
                    output.append(str(float(fields[0].replace(',',''))))
                
    of.write(st+','.join(output)+ed)


for summary_type in summary_types:


    outfile = os.path.join(directory,'summary_{}.csv'.format(summary_type))
    
    if summary_type == 'cycles':
        name = 'cycles'
    else:
        name = 'time'
    
    columns = ','+','.join(min_sizes)+'\n'
    
    with open(outfile,'w') as of:

        of.write(columns)
        of.write('no dsa,')
        for min_size in min_sizes:
            files = [file for file in sorted(glob.glob(os.path.join(directory, 'results_*_{}_nodsa.txt'.format(min_size))))]
            assert len(files) <= 1,"found multiple {} nodsa files".format(min_size)
            if len(files) == 1:
                write_output(files[0],of,summary_type,ed=',')
        of.write('\n')
        
        for perc in cpu_percentages:
            
            of.write('dsa cpu_fract {},'.format(perc))
            
            for min_size in min_sizes:
        
                files = [file for file in sorted(glob.glob(os.path.join(directory, 'results_*_{}_{}.txt'.format(min_size,perc))))]
                assert len(files) <= 1,"found multiple {} {} files".format(min_size,perc)
                if len(files) == 1:
                    write_output(files[0],of,summary_type,ed=',')
                    
            of.write('\n{} saved with DSA\n% reduction\n\n'.format(name))
            
            
        
            
   
            
"""               
            files = [file for file in sorted(glob.glob(os.path.join(directory, 'results_*{}_yield_cpufrace{}.txt'.format(memop,perc))))]
            assert len(files) <= 1,"found multiple {} yield {} files".format(memop,perc)
            if len(files) == 1:
                write_output(files[0],of,summary_type,'dsa cpu_fract {} yield,'.format(perc),'{} saved with DSA\n% reduction\n\n'.format(name))
                
            files = [file for file in sorted(glob.glob(os.path.join(directory, 'results_*{}_umwait_cpufrace{}.txt'.format(memop,perc))))]
            assert len(files) <= 1,"found multiple {} umwait {} files".format(memop,perc)
            if len(files) == 1:
                write_output(files[0],of,summary_type,'dsa cpu_fract {} umwait,'.format(perc),'{} saved with DSA\n% reduction\n\n'.format(name))
            
        
        
        
        files = [file for file in sorted(glob.glob(os.path.join(directory, 'results_*{}_nodsa.txt'.format(memop))))]
        assert len(files) <= 1,"found multiple {} nodsa files".format(memop)
        if len(files) == 1:
            write_output(files[0],of,summary_type,'no dsa,','\n')
        
        files = [file for file in sorted(glob.glob(os.path.join(directory, 'results_*{}_yield_autocpufract.txt'.format(memop))))]
        assert len(files) <= 1,"found multiple {} yield_autcpufract files".format(memop)
        if len(files) == 1:
            write_output(files[0],of,summary_type,'dsa cpu_fract auto yield,','{} saved with DSA\n% reduction\n\n'.format(name))
        
        files = [file for file in sorted(glob.glob(os.path.join(directory, 'results_*{}_umwait_autocpufract.txt'.format(memop))))]
        assert len(files) <= 1,"found multiple {} umwait_autcpufract files".format(memop)
        if len(files) == 1:
            write_output(files[0],of,summary_type,'dsa cpu_fract auto umwait,','{} saved with DSA\n% reduction\n\n'.format(name))
        
                
    
files = [file for file in sorted(glob.glob(os.path.join(directory, 'results_*{}_sleep_cpufrace0.1.txt'.format(memop))))]
assert len(files) <= 1,"found multiple {} sleep 0.1 files".format(memop)
if len(files) == 1:
    write_output(files[0],of,'dsa cpu_fract 0.1 sleep,','Cycles saved with DSA\n% reduction\n\n'.format(name))
    
files = [file for file in sorted(glob.glob(os.path.join(directory, 'results_*{}_yield_cpufrace0.1.txt'.format(memop))))]
assert len(files) <= 1,"found multiple {} yield 0.1 files".format(memop)
if len(files) == 1:
    write_output(files[0],of,'dsa cpu_fract 0.1 yield,','Cycles saved with DSA\n% reduction\n\n')
    
files = [file for file in sorted(glob.glob(os.path.join(directory, 'results_*{}_umwait_cpufrace0.1.txt'.format(memop))))]
assert len(files) <= 1,"found multiple {} umwait 0.1 files".format(memop)
if len(files) == 1:
    write_output(files[0],of,'dsa cpu_fract 0.1 umwait,','Cycles saved with DSA\n% reduction\n\n')
    
    
files = [file for file in sorted(glob.glob(os.path.join(directory, 'results_*{}_sleep_cpufrace0.33.txt'.format(memop))))]
assert len(files) <= 1,"found multiple {} sleep 0.33 files".format(memop)
if len(files) == 1:
    write_output(files[0],of,'dsa cpu_fract 0.33 sleep,','Cycles saved with DSA\n% reduction\n\n')
    
files = [file for file in sorted(glob.glob(os.path.join(directory, 'results_*{}_yield_cpufrace0.33.txt'.format(memop))))]
assert len(files) <= 1,"found multiple {} yield 0.33 files".format(memop)
if len(files) == 1:
    write_output(files[0],of,'dsa cpu_fract 0.33 yield,','Cycles saved with DSA\n% reduction\n\n')
    
files = [file for file in sorted(glob.glob(os.path.join(directory, 'results_*{}_umwait_cpufrace0.33.txt'.format(memop))))]
assert len(files) <= 1,"found multiple {} umwait 0.33 files".format(memop)
if len(files) == 1:
    write_output(files[0],of,'dsa cpu_fract 0.33 umwait,','Cycles saved with DSA\n% reduction\n\n')
"""
        
        
        
        
        
                    
     