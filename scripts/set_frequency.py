
import os
import argparse
import subprocess



class SetCoreFrequency:
    def __init__(self, cores):

        self.current_freq = 1800
        self.cores =  cores
        self.worker_files = []
    
        # Open the files for min and max for the worker cores
        for cpu_id in self.cores:
            file_max = '/sys/devices/system/cpu/cpu{}/cpufreq/scaling_{}_freq'.format(cpu_id,'max')
            self.worker_files.append(os.open(file_max, os.O_RDWR))

        # initialize upf cores to performance governor and a known frequency for the maximum
        #for cpu_id in self.upf1_cores + self.upf2_cores:
        for cpu_id in self.cores:
            command = 'echo performance > /sys/devices/system/cpu/cpu{}/cpufreq/scaling_governor'.format(cpu_id)
            junk = subprocess.run(command, shell=True, capture_output=True)

        freq_set = self.current_freq*1000
        for core_file in self.worker_files:
            os.write(core_file, '{}'.format(int(freq_set)).encode())
    
    def __call__(self, target_freq):

        freq_set = str(int(target_freq*1000)).encode()

        for core_file in self.worker_files:
            os.write(core_file, freq_set)
        
        for core_file in self.worker_files:
            os.lseek(core_file,0, os.SEEK_SET)
                        
    def __del__(self):
        for core_file in self.worker_files:    
            os.close(core_file)    

        for cpu_id in self.cores:
            command = 'echo ondemand > /sys/devices/system/cpu/cpu{}/cpufreq/scaling_governor'.format(cpu_id)
            junk = subprocess.run(command, shell=True, capture_output=True)

        
class SetCoreFrequency_echo:
    def __init__(self, cores):
        
        self.cores = cores
        self.current_freq = 1800
        
        freq_set = self.current_freq*1000

        command = ''

        # initialize upf cores to performance governor and a known frequency
        #for cpu_id in self.upf1_cores + self.upf2_cores:
        for cpu_id in self.cores:
            command += 'echo performance > /sys/devices/system/cpu/cpu{}/cpufreq/scaling_governor;'.format(cpu_id)
            command += 'echo {} > /sys/devices/system/cpu/cpu{}/cpufreq/scaling_{}_freq;'.format(freq_set,cpu_id,'max')
            
        junk = subprocess.run(command, shell=True, capture_output=True)

    
    def __call__(self, target_freq):
        #self.change(target_freq)
        
        freq_set = target_freq*1000
        
        self.current_freq = target_freq

        command = ''
        
        for cpu_id in self.cores:
            command += 'echo {} > /sys/devices/system/cpu/cpu{}/cpufreq/scaling_max_freq;'.format(freq_set,cpu_id)

        junk = subprocess.run(command, shell=True, capture_output=True)


class SetUncoreFrequency:
    def __init__(self, sockets=['01']):

        self.current_freq = 2400
        self.sockets =  sockets
    
        freq_set = self.current_freq*1000
        command = ''
        # Open the files for min and max for the worker cores
        for socket_id in self.sockets:
            
            command += 'echo {} > /sys/devices/system/cpu/intel_uncore_frequency/package_{}_die_00/max_freq_khz;'.format(freq_set,socket_id)
            command += 'sleep 1;'
            command += 'echo {} > /sys/devices/system/cpu/intel_uncore_frequency/package_{}_die_00/min_freq_khz;'.format(freq_set,socket_id)
        
        junk = subprocess.run(command, shell=True, capture_output=True)


    
    def __call__(self, target_freq):

        command = ''
        if target_freq > self.current_freq:
            self.current_freq = target_freq
            freq_set = self.current_freq*1000

            for socket_id in self.sockets:
            
                command += 'echo {} > /sys/devices/system/cpu/intel_uncore_frequency/package_{}_die_00/max_freq_khz;'.format(freq_set,socket_id)
                command += 'sleep 1;'
                command += 'echo {} > /sys/devices/system/cpu/intel_uncore_frequency/package_{}_die_00/min_freq_khz;'.format(freq_set,socket_id)
        
            junk = subprocess.run(command, shell=True, capture_output=True)

        elif target_freq < self.current_freq:
            self.current_freq = target_freq
            freq_set = self.current_freq*1000

            for socket_id in self.sockets:
            
                command += 'echo {} > /sys/devices/system/cpu/intel_uncore_frequency/package_{}_die_00/min_freq_khz;'.format(freq_set,socket_id)
                command += 'sleep 1;'
                command += 'echo {} > /sys/devices/system/cpu/intel_uncore_frequency/package_{}_die_00/max_freq_khz;'.format(freq_set,socket_id)
        
            junk = subprocess.run(command, shell=True, capture_output=True)
                        
    def reset(self):
        
        for socket_id in self.sockets:
            max_freq_set = 2400*1000
            min_freq_set = 800*1000
            command += 'echo {} > /sys/devices/system/cpu/intel_uncore_frequency/package_{}_die_00/max_freq_khz;'.format(max_freq_set,socket_id)
            command += 'sleep 1;'
            command += 'echo {} > /sys/devices/system/cpu/intel_uncore_frequency/package_{}_die_00/min_freq_khz;'.format(min_freq_set,socket_id)
        
        junk = subprocess.run(command, shell=True, capture_output=True)



if __name__ == "__main__":

    import time
 
    parser = argparse.ArgumentParser()
    parser.add_argument("target_freq", help="target core frequency",
                        type=int)
    
    args = parser.parse_args()
    
    set_core_freq = SetCoreFrequency([56,57])
    
    set_core_freq(args.target_freq)


    """
    t = test_os()

    #t.read()

    
    t.change(args.target_freq)
    print('changed')

    #time.sleep(10)

    #t.change(args.target_freq+200)

    t.close()

    
    #time.sleep(20)

    #t.change(900)
    
    #t.close()
    
  """
