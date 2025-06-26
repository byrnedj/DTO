import glob
import os
import re
import time
import shutil


directory = './results/sweep_perc_threads_uniform_wq1'

# Some of the files were created with cpu fractions with one digit after the decimal point. 
# To make things easier, we rename them adding an 0 to the cpu fraction. 
# There were also cases with duplicate tests in which one file had cpu fraction of say 0.30 and another with 0.3
files = [file for file in sorted(glob.glob(os.path.join(directory, '*.txt')))] #[:1]
for file in files:
    print(file)
    new_file = file+'.old'

    if os.path.exists(new_file):
        continue

    with open("temp_file.txt", "w") as of:
        with open(file, "r") as in_f:
            for line in in_f:
                line1 = re.findall(r'failed', line)
                if line1:
                    continue
                print(line)
                of.write(line)


    
    os.rename(file, new_file)
    time.sleep(2)
    shutil.copy("temp_file.txt", file)

os.remove("temp_file.txt")




