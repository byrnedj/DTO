# ==========================================================================
# Copyright (C) 2024 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==========================================================================

import subprocess, os, time, re, time

import glob, argparse


def postprocess_emon(directory, core_view, socket_view, search_string, parms):
    if search_string is None:
        search_string = '*.emon.txt'
    files = [file for file in sorted(glob.glob(os.path.join(directory, search_string )))]

    for file in files:
        
        _,fn = os.path.split(file)
        fn,_ = os.path.splitext(fn)
        fn = fn[:-5]


        #command = [ '/home/jjsydir/miniforge3/envs/emon/bin/python', '/opt/intel/sep/config/edp/pyedp/mpp.py', '-i', file, '-m', '/opt/intel/sep/config/edp/sapphirerapids_server.xml', '-o', fn]
        #command = [ '/home/jjsydir/miniforge3/envs/emon/bin/python', '/opt/intel/sep/config/edp/pyedp/mpp.py', '-i', file, '-m', '/opt/intel/sep/config/edp/sapphirerapids_server.xml', '-o', fn, '--core-view']
        command = [ '/home/jjsydir/miniforge3/envs/emon/bin/python', '/opt/intel/sep/config/edp/pyedp/mpp.py', '-i', file, '-m', '/opt/intel/sep/config/edp/sapphirerapids_server.xml', '-o', fn]
        
        if socket_view:
            command += ['--socket-view']

        if core_view:
            command += ['--core-view']

        if parms is not None:
            command += parms.split()

        print(command)


        proc = subprocess.Popen(command, cwd=directory)

        ret = proc.wait()


if __name__ == '__main__':

    # Create an ArgumentParser object
    parser = argparse.ArgumentParser()

    # Add arguments
    parser.add_argument('--directory', type=str, help='directory where files are')
    parser.add_argument('--core-view', action='store_true', help='')
    parser.add_argument('--socket-view', action='store_true', help='')
    parser.add_argument('--parms', type= str, default=None)
    parser.add_argument('--search-string', type= str, default=None)

    # Parse the arguments
    args = parser.parse_args()

    directory = args.directory
    #directory = '/home/jjsydir/internal_dto/projects.research.dto_dev/scripts/results/sweepalltest2_alloc-type_emon_032625_004008'
    #directory = '/home/jjsydir//output_llama/socket1_results'

    postprocess_emon(directory, args.core_view, args.socket_view, args.search_string, args.parms )

        