# Set up environment
oneccl="/home/jjsydir/oneCCL/build/_install"
source $oneccl/env/setvars.sh
libfabric="/home/jjsydir/libfabric_install/lib"
 
ranks=4
workers=4

tries=100
 
# DTO settings
 
export DTO_USESTDC_CALLS=0
export DTO_COLLECT_STATS=0
export DTO_WAIT_METHOD=yield
export DTO_MIN_BYTES=8192
export DTO_CPU_SIZE_FRACTION=0.33
export DTO_AUTO_ADJUST_KNOBS=1
export DTO_LOG_LEVEL=1
 
# Use this if using DTO
#export LD_PRELOAD=$libfabric/libfabric.so:/home/jjsydir/internal_dto/projects.research.dto_dev/libdevdto.so.1.0
#export LD_PRELOAD=$libfabric/libfabric.so:/home/jjsydir/internal_dto/projects.research.dto_dev/libdto.so.1.0
#export LD_PRELOAD=$libfabric/libfabric.so:/home/jjsydir/old_dto/DTO/libdto.so.1.0
export LD_PRELOAD=$libfabric/libfabric.so:/home/jjsydir/clean_dto/DTO/libdto.so.1.0
# Otherwise use this to use default stdc mem* operations
#export LD_PRELOAD=$libfabric/libfabric.so
 
# Libfabric and oneCCL settings
 
export LD_LIBRARY_PATH=$libfabric:$LD_LIBRARY_PATH
export FI_SHM_USE_DSA_SAR=0
export FI_PROVIDER=shm
export FI_SHM_DISABLE_CMA=1
export CCL_ATL_TRANSPORT=ofi
export CCL_ATL_SHM=1
export CCL_ALLREDUCE=ring
export CCL_WORKER_COUNT=$workers # Set number of CCL workers, default=1
#export FI_LOG_LEVEL=warn
#export CCL_LOG_LEVEL=info
 
# Example Allreduce command line, 2 ranks on the same node. Use --inplace 1 to enable in-place reductions. Default is out of place
#$oneccl/bin/mpiexec.hydra -n $ranks $oneccl/examples/benchmark/benchmark --iter_policy off --coll allreduce -f $(( 4*1024 )) -t $(( 32*1024*1024 )) -w 50 -i 50 --check last --inplace 1
#$oneccl/bin/mpiexec.hydra -n $ranks $oneccl/examples/benchmark/benchmark --iter_policy off --coll allreduce -f $(( 4*1024 )) -t $(( 32*1024*1024 )) -w 50 -i 5000 --check all --inplace 1

while [ "$tries" -gt 0 ]; do
    #$oneccl/bin/mpiexec.hydra -n $ranks $oneccl/examples/benchmark/benchmark --iter_policy off --coll allreduce -f $(( 4*1024 )) -t $(( 32*1024*1024 )) -w 50 -i 50 --check last --inplace 1
    $oneccl/bin/mpiexec.hydra -n $ranks $oneccl/examples/benchmark/benchmark --iter_policy off --coll allreduce -f $(( 8*1024*1024 )) -t $(( 32*1024*1024 )) -w 50 -i 50 --check all --inplace 1
    tries=$(( tries - 1 ))
    sleep 1
done