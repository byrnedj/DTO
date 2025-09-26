#python sweep_all_test_settable_size2.py --output-type emon --run-name alloc-type --cfg-filepath ./configs/alloc-type_config.json
#python sweep_numa_cases_test_settable_size2.py --output-type emon --run-name redo --cfg-filepath ./configs/numacases_cpy_config.json
#python sweep_numa_cases_test_settable_size2.py --output-type perf --run-name set --cfg-filepath ./configs/numacases_set_config.json
#python sweep_all_test_settable_size2.py --output-type emon --run-name alloc-type-tiny --cfg-filepath ./configs/alloc-type_tiny_config.json
#python sweep_all_test_settable_size2.py --output-type perf --run-name tx-size --cfg-filepath ./configs/tx_size_sweep_config.json

#sudo sysctl -w kernel.numa_balancing=1

#python sweep_all_test_settable_size2.py --output-type perf --run-name tx-size-singlealloc-autonuma --cfg-filepath ./configs/tx_size_sweep_singlealloc_config.json
#python sweep_all_test_settable_size2.py --output-type perf --run-name tx-size-autonuma --cfg-filepath ./configs/tx_size_sweep_config.json
#python sweep_all_test_settable_size2.py --output-type perf --run-name alloc-type-autonuma --cfg-filepath ./configs/alloc-type_config.json
#python sweep_all_test_settable_size2.py --output-type perf --run-name alloc-type-tiny-autonuma --cfg-filepath ./configs/alloc-type_tiny_config.json  --results-dirname sweepalltest2_alloc-type-tiny-autonuma_perf_032825_091839

#python sweep_all_test_settable_size2.py --output-type emon --run-name alloc-type-tiny-autonuma-cpy --cfg-filepath ./configs/alloc-type_tiny-cpy_config.json
#python sweep_all_test_settable_size2.py --output-type emon --run-name alloc-type-autonuma --cfg-filepath ./configs/alloc-type_config.json


#python sweep_all_test_settable_size2.py --output-type perf --run-name tx-size-indiv-seq-autonuma --cfg-filepath ./configs/tx_size_sweep_indiv_seq_config.json

#python sweep_all_test_settable_size4.py --output-type DTO_python --run-name single-size-alg-stats-cpy-1thread --cfg-filepath ./configs/single_size_auto_cpy.json  --alg-stats --num-iter 1000000 --results-dirname dto_alg_stats_multithread2 --alg-stats --num-dsas 1
#python sweep_all_test_settable_size4.py --output-type DTO_python --run-name single-size-alg-stats-set-1thread --cfg-filepath ./configs/single_size_auto_set.json  --alg-stats --num-iter 1000000 --results-dirname dto_alg_stats_multithread2 --alg-stats --num-dsas 1
#python sweep_all_test_settable_size4.py --output-type DTO_python --run-name single-size-alg-stats-cpy-2thread --cfg-filepath ./configs/single_size_auto_cpy.json  --alg-stats --num-iter 1000000 --num-threads 2 --results-dirname dto_alg_stats_multithread2 --alg-stats --num-dsas 1
#python sweep_all_test_settable_size4.py --output-type DTO_python --run-name single-size-alg-stats-set-2thread --cfg-filepath ./configs/single_size_auto_set.json  --alg-stats --num-iter 1000000 --num-threads 2 --results-dirname dto_alg_stats_multithread2 --alg-stats --num-dsas 1
#python sweep_all_test_settable_size4.py --output-type DTO_python --run-name single-size-alg-stats-cpy-3thread --cfg-filepath ./configs/single_size_auto_cpy.json  --alg-stats --num-iter 1000000 --num-threads 3 --results-dirname dto_alg_stats_multithread2 --alg-stats --num-dsas 1
#python sweep_all_test_settable_size4.py --output-type DTO_python --run-name single-size-alg-stats-set-3thread --cfg-filepath ./configs/single_size_auto_set.json  --alg-stats --num-iter 1000000 --num-threads 3 --results-dirname dto_alg_stats_multithread2 --alg-stats --num-dsas 1
#python sweep_all_test_settable_size4.py --output-type DTO_python --run-name dist-cpy-1thread --cfg-filepath ./configs/single_size_auto_cpy.json  --alg-stats --num-iter 1000000 --distribution-filepath ../dto-test-configs/uniform_cpy_64_1024.csv --results-dirname dto_alg_stats_multithread2 --alg-stats --num-dsas 1
#python sweep_all_test_settable_size4.py --output-type DTO_python --run-name dist-set-1thread --cfg-filepath ./configs/single_size_auto_set.json  --alg-stats --num-iter 1000000 --distribution-filepath ../dto-test-configs/uniform_set_64_1024.csv --results-dirname dto_alg_stats_multithread2 --alg-stats --num-dsas 1
#python sweep_all_test_settable_size4.py --output-type DTO_python --run-name dist-cpy-2thread --cfg-filepath ./configs/single_size_auto_cpy.json  --alg-stats --num-iter 1000000 --num-threads 2 --distribution-filepath ../dto-test-configs/uniform_cpy_64_1024.csv --results-dirname dto_alg_stats_multithread2 --alg-stats --num-dsas 1
#python sweep_all_test_settable_size4.py --output-type DTO_python --run-name dist-set-2thread --cfg-filepath ./configs/single_size_auto_set.json  --alg-stats --num-iter 1000000 --num-threads 2 --distribution-filepath ../dto-test-configs/uniform_set_64_1024.csv --results-dirname dto_alg_stats_multithread2 --alg-stats --num-dsas 1
#python sweep_all_test_settable_size4.py --output-type DTO_python --run-name dist-cpy-3thread --cfg-filepath ./configs/single_size_auto_cpy.json  --alg-stats --num-iter 1000000 --num-threads 3 --distribution-filepath ../dto-test-configs/uniform_cpy_64_1024.csv --results-dirname dto_alg_stats_multithread2 --alg-stats --num-dsas 1
#python sweep_all_test_settable_size4.py --output-type DTO_python --run-name dist-set-3thread --cfg-filepath ./configs/single_size_auto_set.json  --alg-stats --num-iter 1000000 --num-threads 3 --distribution-filepath ../dto-test-configs/uniform_set_64_1024.csv --results-dirname dto_alg_stats_multithread2 --alg-stats --num-dsas 1
#python sweep_all_test_settable_size4.py --output-type DTO_python --run-name dist-cpy-1thread --cfg-filepath ./configs/single_size_auto_cpy.json  --alg-stats --num-iter 1000000 --distribution-filepath ../dto-test-configs/uniform_normalized_cpy_64_1024.csv --results-dirname dto_alg_stats_multithread2 --alg-stats --num-dsas 1
#python sweep_all_test_settable_size4.py --output-type DTO_python --run-name dist-set-1thread --cfg-filepath ./configs/single_size_auto_set.json  --alg-stats --num-iter 1000000 --distribution-filepath ../dto-test-configs/uniform_normalized_set_64_1024.csv --results-dirname dto_alg_stats_multithread2 --alg-stats --num-dsas 1
#python sweep_all_test_settable_size4.py --output-type DTO_python --run-name dist-cpy-2thread --cfg-filepath ./configs/single_size_auto_cpy.json  --alg-stats --num-iter 1000000 --num-threads 2 --distribution-filepath ../dto-test-configs/uniform_normalized_cpy_64_1024.csv --results-dirname dto_alg_stats_multithread2 --alg-stats --num-dsas 1
#python sweep_all_test_settable_size4.py --output-type DTO_python --run-name dist-set-2thread --cfg-filepath ./configs/single_size_auto_set.json  --alg-stats --num-iter 1000000 --num-threads 2 --distribution-filepath ../dto-test-configs/uniform_normalized_set_64_1024.csv --results-dirname dto_alg_stats_multithread2 --alg-stats --num-dsas 1
#python sweep_all_test_settable_size4.py --output-type DTO_python --run-name dist-cpy-3thread --cfg-filepath ./configs/single_size_auto_cpy.json  --alg-stats --num-iter 1000000 --num-threads 3 --distribution-filepath ../dto-test-configs/uniform_normalized_cpy_64_1024.csv --results-dirname dto_alg_stats_multithread2 --alg-stats --num-dsas 1
#python sweep_all_test_settable_size4.py --output-type DTO_python --run-name dist-set-3thread --cfg-filepath ./configs/single_size_auto_set.json  --alg-stats --num-iter 1000000 --num-threads 3 --distribution-filepath ../dto-test-configs/uniform_normalized_set_64_1024.csv --results-dirname dto_alg_stats_multithread2 --alg-stats --num-dsas 1
#python sweep_all_test_settable_size4.py --output-type DTO_python --run-name convergence --cfg-filepath ./configs/single_size_auto_set.json  --alg-stats --num-iter 1000000 --distribution-filepath ../dto-test-configs/uniform_set_64_1024.csv --results-dirname convergence --num-dsas 1
#python sweep_all_test_settable_size4.py --output-type DTO_python --run-name convergence --cfg-filepath ./configs/single_size_auto_cpy.json  --alg-stats --num-iter 1000000 --distribution-filepath ../dto-test-configs/uniform_cpy_64_1024.csv --results-dirname convergence --num-dsas 1
#python sweep_all_test_settable_size4.py --output-type DTO_python --run-name convergence --cfg-filepath ./configs/single_size_auto_set.json  --alg-stats --num-iter 1000000 --distribution-filepath ../dto-test-configs/uniform_normalized_set_64_1024.csv --results-dirname convergence --num-dsas 1
#python sweep_all_test_settable_size4.py --output-type DTO_python --run-name convergence --cfg-filepath ./configs/single_size_auto_cpy.json  --alg-stats --num-iter 1000000 --distribution-filepath ../dto-test-configs/uniform_normalized_cpy_64_1024.csv --results-dirname convergence --num-dsas 1


# cpy
#python sweep_all_test_settable_size4.py --output-type perf --run-name perc-sweep --cfg-filepath ./configs/sweep_perc_30_50.json  --alg-stats --num-iter 10000000 --num-threads-override 1 --distribution-filepath ../dto-test-configs/uniform_cpy_64_1024.csv --results-dirname perc_sweep --num-dsas 1
#python sweep_all_test_settable_size4.py --output-type perf --run-name perc-sweep --cfg-filepath ./configs/sweep_perc_65_85.json  --alg-stats --num-iter 10000000 --num-threads-override 5 --distribution-filepath ../dto-test-configs/uniform_cpy_64_1024.csv --results-dirname perc_sweep --num-dsas 1
#python sweep_all_test_settable_size4.py --output-type perf --run-name perc-sweep --cfg-filepath ./configs/sweep_perc_70_90.json  --alg-stats --num-iter 10000000 --num-threads-override 10 --distribution-filepath ../dto-test-configs/uniform_cpy_64_1024.csv --results-dirname perc_sweep --num-dsas 1
#python sweep_all_test_settable_size4.py --output-type perf --run-name perc-sweep --cfg-filepath ./configs/sweep_perc_70_90.json  --alg-stats --num-iter 10000000 --num-threads-override 15 --distribution-filepath ../dto-test-configs/uniform_cpy_64_1024.csv --results-dirname perc_sweep --num-dsas 1
#python sweep_all_test_settable_size4.py --output-type perf --run-name perc-sweep --cfg-filepath ./configs/sweep_perc_75_95.json  --alg-stats --num-iter 10000000 --num-threads-override 20 --distribution-filepath ../dto-test-configs/uniform_cpy_64_1024.csv --results-dirname perc_sweep --num-dsas 1

#cpy outside
#python sweep_all_test_settable_size4.py --output-type perf --run-name perc-sweep --cfg-filepath ./configs/sweep_perc_outside_30_50.json  --alg-stats --num-iter 10000000 --num-threads-override 1 --distribution-filepath ../dto-test-configs/uniform_cpy_64_1024.csv --results-dirname perc_sweep --num-dsas 1
#python sweep_all_test_settable_size4.py --output-type perf --run-name perc-sweep --cfg-filepath ./configs/sweep_perc_outside_65_85.json  --alg-stats --num-iter 10000000 --num-threads-override 5 --distribution-filepath ../dto-test-configs/uniform_cpy_64_1024.csv --results-dirname perc_sweep --num-dsas 1
#python sweep_all_test_settable_size4.py --output-type perf --run-name perc-sweep --cfg-filepath ./configs/sweep_perc_outside_70_90.json  --alg-stats --num-iter 10000000 --num-threads-override 10 --distribution-filepath ../dto-test-configs/uniform_cpy_64_1024.csv --results-dirname perc_sweep --num-dsas 1
#python sweep_all_test_settable_size4.py --output-type perf --run-name perc-sweep --cfg-filepath ./configs/sweep_perc_outside_70_90.json  --alg-stats --num-iter 10000000 --num-threads-override 15 --distribution-filepath ../dto-test-configs/uniform_cpy_64_1024.csv --results-dirname perc_sweep --num-dsas 1
#python sweep_all_test_settable_size4.py --output-type perf --run-name perc-sweep --cfg-filepath ./configs/sweep_perc_outside_75_95.json  --alg-stats --num-iter 10000000 --num-threads-override 20 --distribution-filepath ../dto-test-configs/uniform_cpy_64_1024.csv --results-dirname perc_sweep --num-dsas 1

#set
#python sweep_all_test_settable_size4.py --output-type perf --run-name perc-sweep --cfg-filepath ./configs/sweep_perc_20_40.json  --alg-stats --num-iter 10000000 --num-threads-override 1 --distribution-filepath ../dto-test-configs/uniform_set_64_1024.csv --results-dirname perc_sweep --num-dsas 1
#python sweep_all_test_settable_size4.py --output-type perf --run-name perc-sweep --cfg-filepath ./configs/sweep_perc_50_70.json  --alg-stats --num-iter 10000000 --num-threads-override 5 --distribution-filepath ../dto-test-configs/uniform_set_64_1024.csv --results-dirname perc_sweep --num-dsas 1
#python sweep_all_test_settable_size4.py --output-type perf --run-name perc-sweep --cfg-filepath ./configs/sweep_perc_60_80.json  --alg-stats --num-iter 10000000 --num-threads-override 10 --distribution-filepath ../dto-test-configs/uniform_set_64_1024.csv --results-dirname perc_sweep --num-dsas 1
#python sweep_all_test_settable_size4.py --output-type perf --run-name perc-sweep --cfg-filepath ./configs/sweep_perc_60_80.json  --alg-stats --num-iter 10000000 --num-threads-override 15 --distribution-filepath ../dto-test-configs/uniform_set_64_1024.csv --results-dirname perc_sweep --num-dsas 1
#python sweep_all_test_settable_size4.py --output-type perf --run-name perc-sweep --cfg-filepath ./configs/sweep_perc_60_80.json  --alg-stats --num-iter 10000000 --num-threads-override 20 --distribution-filepath ../dto-test-configs/uniform_set_64_1024.csv --results-dirname perc_sweep --num-dsas 1

#set outside
#python sweep_all_test_settable_size4.py --output-type perf --run-name perc-sweep --cfg-filepath ./configs/sweep_perc_outside_20_40.json  --alg-stats --num-iter 10000000 --num-threads-override 1 --distribution-filepath ../dto-test-configs/uniform_set_64_1024.csv --results-dirname perc_sweep --num-dsas 1
#python sweep_all_test_settable_size4.py --output-type perf --run-name perc-sweep --cfg-filepath ./configs/sweep_perc_outside_50_70.json  --alg-stats --num-iter 10000000 --num-threads-override 5 --distribution-filepath ../dto-test-configs/uniform_set_64_1024.csv --results-dirname perc_sweep --num-dsas 1
#python sweep_all_test_settable_size4.py --output-type perf --run-name perc-sweep --cfg-filepath ./configs/sweep_perc_outside_60_80.json  --alg-stats --num-iter 10000000 --num-threads-override 10 --distribution-filepath ../dto-test-configs/uniform_set_64_1024.csv --results-dirname perc_sweep --num-dsas 1
#python sweep_all_test_settable_size4.py --output-type perf --run-name perc-sweep --cfg-filepath ./configs/sweep_perc_outside_60_80.json  --alg-stats --num-iter 10000000 --num-threads-override 15 --distribution-filepath ../dto-test-configs/uniform_set_64_1024.csv --results-dirname perc_sweep --num-dsas 1
#python sweep_all_test_settable_size4.py --output-type perf --run-name perc-sweep --cfg-filepath ./configs/sweep_perc_outside_60_80.json  --alg-stats --num-iter 10000000 --num-threads-override 20 --distribution-filepath ../dto-test-configs/uniform_set_64_1024.csv --results-dirname perc_sweep --num-dsas 1

#Normalized cases
#cpy
#python sweep_all_test_settable_size4.py --output-type perf --run-name perc-sweep --cfg-filepath ./configs/sweep_perc_30_50.json  --alg-stats --num-iter 10000000 --num-threads-override 1 --distribution-filepath ../dto-test-configs/uniform_normalized_cpy_64_1024.csv --results-dirname perc_sweep2 --num-dsas 1
#python sweep_all_test_settable_size4.py --output-type perf --run-name perc-sweep --cfg-filepath ./configs/sweep_perc_60_80.json  --alg-stats --num-iter 10000000 --num-threads-override 5 --distribution-filepath ../dto-test-configs/uniform_normalized_cpy_64_1024.csv --results-dirname perc_sweep2 --num-dsas 1
#python sweep_all_test_settable_size4.py --output-type perf --run-name perc-sweep --cfg-filepath ./configs/sweep_perc_70_90.json  --alg-stats --num-iter 10000000 --num-threads-override 10 --distribution-filepath ../dto-test-configs/uniform_normalized_cpy_64_1024.csv --results-dirname perc_sweep2 --num-dsas 1
#python sweep_all_test_settable_size4.py --output-type perf --run-name perc-sweep --cfg-filepath ./configs/sweep_perc_70_90.json  --alg-stats --num-iter 10000000 --num-threads-override 15 --distribution-filepath ../dto-test-configs/uniform_normalized_cpy_64_1024.csv --results-dirname perc_sweep2 --num-dsas 1
#python sweep_all_test_settable_size4.py --output-type perf --run-name perc-sweep --cfg-filepath ./configs/sweep_perc_75_95.json  --alg-stats --num-iter 10000000 --num-threads-override 20 --distribution-filepath ../dto-test-configs/uniform_normalized_cpy_64_1024.csv --results-dirname perc_sweep2 --num-dsas 1

#cpy outside
#python sweep_all_test_settable_size4.py --output-type perf --run-name perc-sweep --cfg-filepath ./configs/sweep_perc_outside_30_50.json  --alg-stats --num-iter 10000000 --num-threads-override 1 --distribution-filepath ../dto-test-configs/uniform_normalized_cpy_64_1024.csv --results-dirname perc_sweep2 --num-dsas 1
#python sweep_all_test_settable_size4.py --output-type perf --run-name perc-sweep --cfg-filepath ./configs/sweep_perc_outside_65_85.json  --alg-stats --num-iter 10000000 --num-threads-override 5 --distribution-filepath ../dto-test-configs/uniform_normalized_cpy_64_1024.csv --results-dirname perc_sweep2 --num-dsas 1
#python sweep_all_test_settable_size4.py --output-type perf --run-name perc-sweep --cfg-filepath ./configs/sweep_perc_outside_70_90.json  --alg-stats --num-iter 10000000 --num-threads-override 10 --distribution-filepath ../dto-test-configs/uniform_normalized_cpy_64_1024.csv --results-dirname perc_sweep2 --num-dsas 1
#python sweep_all_test_settable_size4.py --output-type perf --run-name perc-sweep --cfg-filepath ./configs/sweep_perc_outside_70_90.json  --alg-stats --num-iter 10000000 --num-threads-override 15 --distribution-filepath ../dto-test-configs/uniform_normalized_cpy_64_1024.csv --results-dirname perc_sweep2 --num-dsas 1
#python sweep_all_test_settable_size4.py --output-type perf --run-name perc-sweep --cfg-filepath ./configs/sweep_perc_outside_75_95.json  --alg-stats --num-iter 10000000 --num-threads-override 20 --distribution-filepath ../dto-test-configs/uniform_normalized_cpy_64_1024.csv --results-dirname perc_sweep2 --num-dsas 1

#set
#python sweep_all_test_settable_size4.py --output-type perf --run-name perc-sweep --cfg-filepath ./configs/sweep_perc_20_40.json  --alg-stats --num-iter 10000000 --num-threads-override 1 --distribution-filepath ../dto-test-configs/uniform_normalized_set_64_1024.csv --results-dirname perc_sweep2 --num-dsas 1
#python sweep_all_test_settable_size4.py --output-type perf --run-name perc-sweep --cfg-filepath ./configs/sweep_perc_50_70.json  --alg-stats --num-iter 10000000 --num-threads-override 5 --distribution-filepath ../dto-test-configs/uniform_normalized_set_64_1024.csv --results-dirname perc_sweep2 --num-dsas 1
#python sweep_all_test_settable_size4.py --output-type perf --run-name perc-sweep --cfg-filepath ./configs/sweep_perc_55_75.json  --alg-stats --num-iter 10000000 --num-threads-override 10 --distribution-filepath ../dto-test-configs/uniform_normalized_set_64_1024.csv --results-dirname perc_sweep2 --num-dsas 1
#python sweep_all_test_settable_size4.py --output-type perf --run-name perc-sweep --cfg-filepath ./configs/sweep_perc_60_80.json  --alg-stats --num-iter 10000000 --num-threads-override 15 --distribution-filepath ../dto-test-configs/uniform_normalized_set_64_1024.csv --results-dirname perc_sweep2 --num-dsas 1
#python sweep_all_test_settable_size4.py --output-type perf --run-name perc-sweep --cfg-filepath ./configs/sweep_perc_60_80.json  --alg-stats --num-iter 10000000 --num-threads-override 20 --distribution-filepath ../dto-test-configs/uniform_normalized_set_64_1024.csv --results-dirname perc_sweep2 --num-dsas 1

#set outside
#python sweep_all_test_settable_size4.py --output-type perf --run-name perc-sweep --cfg-filepath ./configs/sweep_perc_outside_20_40.json  --alg-stats --num-iter 10000000 --num-threads-override 1 --distribution-filepath ../dto-test-configs/uniform_normalized_set_64_1024.csv --results-dirname perc_sweep2 --num-dsas 1
#python sweep_all_test_settable_size4.py --output-type perf --run-name perc-sweep --cfg-filepath ./configs/sweep_perc_outside_50_70.json  --alg-stats --num-iter 10000000 --num-threads-override 5 --distribution-filepath ../dto-test-configs/uniform_normalized_set_64_1024.csv --results-dirname perc_sweep2 --num-dsas 1
#python sweep_all_test_settable_size4.py --output-type perf --run-name perc-sweep --cfg-filepath ./configs/sweep_perc_outside_55_75.json  --alg-stats --num-iter 10000000 --num-threads-override 10 --distribution-filepath ../dto-test-configs/uniform_normalized_set_64_1024.csv --results-dirname perc_sweep2 --num-dsas 1
#python sweep_all_test_settable_size4.py --output-type perf --run-name perc-sweep --cfg-filepath ./configs/sweep_perc_outside_60_80.json  --alg-stats --num-iter 10000000 --num-threads-override 15 --distribution-filepath ../dto-test-configs/uniform_normalized_set_64_1024.csv --results-dirname perc_sweep2 --num-dsas 1
#python sweep_all_test_settable_size4.py --output-type perf --run-name perc-sweep --cfg-filepath ./configs/sweep_perc_outside_60_80.json  --alg-stats --num-iter 10000000 --num-threads-override 20 --distribution-filepath ../dto-test-configs/uniform_normalized_set_64_1024.csv --results-dirname perc_sweep2 --num-dsas 1

#python sweep_all_test_settable_size4.py --output-type perf --run-name perc-sweep --cfg-filepath ./configs/junk_74.json  --alg-stats --num-iter 10000000 --num-threads-override 20 --distribution-filepath ../dto-test-configs/uniform_set_64_1024.csv --results-dirname perc_sweep --num-dsas 1
#python sweep_all_test_settable_size4.py --output-type perf --run-name perc-sweep --cfg-filepath ./configs/junk_74.json  --alg-stats --num-iter 10000000 --num-threads-override 20 --distribution-filepath ../dto-test-configs/uniform_normalized_set_64_1024.csv --results-dirname perc_sweep2 --num-dsas 1

# numa convergence
# cpy
#python sweep_numa_cases_test_settable_size4.py --output-type DTO_python --run-name convergence --cfg-filepath ./configs/distribution_auto_cpy.json  --alg-stats --num-iter 1000000 --num-threads-override 1 --distribution-filepath ../dto-test-configs/uniform_cpy_64_1024.csv --results-dirname numa_convergence --num-dsas 1 --exclude-nodsa
#python sweep_numa_cases_test_settable_size4.py --output-type DTO_python --run-name convergence --cfg-filepath ./configs/distribution_auto_cpy.json  --alg-stats --num-iter 1000000 --num-threads-override 5 --distribution-filepath ../dto-test-configs/uniform_cpy_64_1024.csv --results-dirname numa_convergence --num-dsas 1 --exclude-nodsa
#python sweep_numa_cases_test_settable_size4.py --output-type DTO_python --run-name convergence --cfg-filepath ./configs/distribution_auto_cpy.json  --alg-stats --num-iter 1000000 --num-threads-override 10 --distribution-filepath ../dto-test-configs/uniform_cpy_64_1024.csv --results-dirname numa_convergence --num-dsas 1 --exclude-nodsa
#python sweep_numa_cases_test_settable_size4.py --output-type DTO_python --run-name convergence --cfg-filepath ./configs/distribution_auto_cpy.json  --alg-stats --num-iter 1000000 --num-threads-override 15 --distribution-filepath ../dto-test-configs/uniform_cpy_64_1024.csv --results-dirname numa_convergence --num-dsas 1 --exclude-nodsa
#python sweep_numa_cases_test_settable_size4.py --output-type DTO_python --run-name convergence --cfg-filepath ./configs/distribution_auto_cpy.json  --alg-stats --num-iter 1000000 --num-threads-override 20 --distribution-filepath ../dto-test-configs/uniform_cpy_64_1024.csv --results-dirname numa_convergence --num-dsas 1 --exclude-nodsa

# set
#python sweep_numa_cases_test_settable_size4.py --output-type DTO_python --run-name convergence --cfg-filepath ./configs/distribution_auto_set.json  --alg-stats --num-iter 1000000 --num-threads-override 1 --distribution-filepath ../dto-test-configs/uniform_set_64_1024.csv --results-dirname numa_convergence --num-dsas 1 --exclude-nodsa
#python sweep_numa_cases_test_settable_size4.py --output-type DTO_python --run-name convergence --cfg-filepath ./configs/distribution_auto_set.json  --alg-stats --num-iter 1000000 --num-threads-override 5 --distribution-filepath ../dto-test-configs/uniform_set_64_1024.csv --results-dirname numa_convergence --num-dsas 1 --exclude-nodsa
#python sweep_numa_cases_test_settable_size4.py --output-type DTO_python --run-name convergence --cfg-filepath ./configs/distribution_auto_set.json  --alg-stats --num-iter 1000000 --num-threads-override 10 --distribution-filepath ../dto-test-configs/uniform_set_64_1024.csv --results-dirname numa_convergence --num-dsas 1 --exclude-nodsa
#python sweep_numa_cases_test_settable_size4.py --output-type DTO_python --run-name convergence --cfg-filepath ./configs/distribution_auto_set.json  --alg-stats --num-iter 1000000 --num-threads-override 15 --distribution-filepath ../dto-test-configs/uniform_set_64_1024.csv --results-dirname numa_convergence --num-dsas 1 --exclude-nodsa
#python sweep_numa_cases_test_settable_size4.py --output-type DTO_python --run-name convergence --cfg-filepath ./configs/distribution_auto_set.json  --alg-stats --num-iter 1000000 --num-threads-override 20 --distribution-filepath ../dto-test-configs/uniform_set_64_1024.csv --results-dirname numa_convergence --num-dsas 1 --exclude-nodsa

#python sweep_all_test_settable_size4.py --output-type perf --run-name perc-sweep-512K-wq4 --cfg-filepath ./configs/sweep_perc_threads_512K.json  --alg-stats  --num-dsas 1 --results-dirname sweep_perc_threads_512K_wq4

#python sweep_all_test_settable_size4.py --output-type perf --run-name perc-sweep-wq-128 --cfg-filepath ./configs/sweep_perc_threads_512K.json  --alg-stats  --num-dsas 1 --distribution-filepath ../dto-test-configs/uniform_cpy_64_100.csv 

#python sweep_all_test_settable_size4.py --output-type DTO_python --run-name convergence --cfg-filepath ./configs/single_size_auto_busypoll_cpy.json  --alg-stats --num-iter 1000000 --results-dirname convergence_poll_15_wq128 --num-dsas 1
#python sweep_all_test_settable_size4.py --output-type DTO_python --run-name convergence --cfg-filepath ./configs/single_size_auto_busypoll_cpy.json  --alg-stats --num-iter 1000000 --distribution-filepath ../dto-test-configs/uniform_cpy_64_1024.csv --results-dirname convergence_poll_15_wq128 --num-dsas 1
#
#python sweep_all_test_settable_size5.py --output-type perf --run-name bursty_wq128 --cfg-filepath ./configs/sweep_perc_threads_burst_dist.json  --alg-stats  --num-iter 100000000 --num-dsas 1 --distribution-filepath ../dto-test-configs/uniform_cpy_64_100.csv --results-dirname sweepalltest5_bursty_wq128_perf_042125_180112
#sudo ../config_dsa_8.sh
#sleep 20
#python sweep_all_test_settable_size5.py --output-type perf --run-name bursty_wq128 --cfg-filepath ./configs/sweep_perc_threads_burst_dist2.json  --alg-stats  --num-iter 100000000 --num-dsas 1 --distribution-filepath ../dto-test-configs/uniform_cpy_64_100.csv --results-dirname sweepalltest5_bursty_wq128_perf_042125_180112
#sudo ../config_dsa_1_small.sh
#sleep 20
#python sweep_all_test_settable_size5.py --output-type perf --run-name bursty_wq1 --cfg-filepath ./configs/sweep_perc_threads_burst_dist2.json  --alg-stats  --num-iter 100000000 --num-dsas 1 --distribution-filepath ../dto-test-configs/uniform_cpy_64_100.csv --results-dirname sweepalltest5_bursty_wq1_perf_042225_073258

#python sweep_all_test_settable_size6.py --output-type perf --run-name sweep-delays_wq128 --cfg-filepath ./configs/sweep_threads_delays_dist.json  --alg-stats  --num-iter 10000000 --num-dsas 1 --distribution-filepath ../dto-test-configs/uniform_cpy_64_100.csv --results-dirname sweep_delays_wq128 
#python sweep_all_test_settable_size5.py --output-type perf --run-name bursty-algstatstest_wq128 --cfg-filepath ./configs/junk.json  --alg-stats  --num-iter 100000000 --num-dsas 1 --distribution-filepath ../dto-test-configs/uniform_cpy_64_100.csv --dry-run
#python sweep_all_test_settable_size5.py --output-type perf --run-name bursty-nostatstest_wq128 --cfg-filepath ./configs/junk.json  --no-stats  --num-iter 100000000 --num-dsas 1 --distribution-filepath ../dto-test-configs/uniform_cpy_64_100.csv

#sudo ../config_dsa_1_small.sh

#python sweep_all_test_settable_size5.py --output-type perf --run-name bursty-small-wq1 --cfg-filepath ./configs/sweep_perc_threads_burst_dist_small1a.json  --alg-stats  --num-iter 100000000 --num-dsas 1 --distribution-filepath ../dto-test-configs/uniform_cpy_64_100.csv  --results-dirname bursty_small_wq1

#sudo ../config_dsa_1_small.sh
#python sweep_all_test_settable_size5.py --output-type perf --run-name bursty-small-wq1 --cfg-filepath ./configs/sweep_perc_threads_burst_dist_small1.json  --alg-stats  --num-iter 100000000 --num-dsas 1 --distribution-filepath ../dto-test-configs/uniform_cpy_64_100.csv  --results-dirname bursty_small_wq1

#sudo ../config_dsa_8.sh

#python sweep_all_test_settable_size5.py --output-type perf --run-name bursty-small-wq128 --cfg-filepath ./configs/sweep_perc_threads_burst_dist_small1.json  --alg-stats  --num-iter 100000000 --num-dsas 1 --distribution-filepath ../dto-test-configs/uniform_cpy_64_100.csv  --results-dirname bursty_small_wq128

#python sweep_all_test_settable_size5.py --output-type perf --run-name bursty-small-nostats-wq128 --cfg-filepath ./configs/junk.json  --no-stats  --num-iter 10000000 --num-dsas 1 --distribution-filepath ../dto-test-configs/uniform_cpy_64_100.csv  --results-dirname bursty_small_wq128_nostats

#finish the step sweep on wq 128
#python sweep_all_test_settable_size6.py --output-type perf --run-name sweep-delays_wq128 --cfg-filepath ./configs/sweep_threads_delays_dist_delta.json  --alg-stats  --num-iter 10000000 --num-dsas 1 --distribution-filepath ../dto-test-configs/uniform_cpy_64_100.csv --results-dirname sweep_delays_wq128
#redo the ramps on wq 128
#python sweep_all_test_settable_size6.py --output-type perf --run-name ramp_wq128 --cfg-filepath ./configs/sweep_threads_ramp_dist.json  --alg-stats  --num-iter 10000000 --num-dsas 1 --distribution-filepath ../dto-test-configs/uniform_cpy_64_100.csv --results-dirname ramp_wq128
#sudo ../config_dsa_1_small.sh
#step sweep on wq 11
#python sweep_all_test_settable_size6.py --output-type perf --run-name sweep-delays_wq1 --cfg-filepath ./configs/sweep_threads_delays_dist.json  --alg-stats  --num-iter 10000000 --num-dsas 1 --distribution-filepath ../dto-test-configs/uniform_cpy_64_100.csv --results-dirname sweep_delays_wq1
#redo the ramps on wq 128
#python sweep_all_test_settable_size6.py --output-type perf --run-name sweep-delays --cfg-filepath ./configs/sweep_threads_ramp_dist.json  --alg-stats  --num-iter 10000000 --num-dsas 1 --distribution-filepath ../dto-test-configs/uniform_cpy_64_100.csv --results-dirname sweep_delays

#python sweep_all_test_settable_size6.py --output-type perf --run-name sweep-delays2_wq1 --cfg-filepath ./configs/sweep_threads_delays_dist2.json  --alg-stats  --num-iter 10000000 --num-dsas 1 --distribution-filepath ../dto-test-configs/uniform_cpy_64_100.csv --results-dirname sweep_delays2_wq1


#python sweep_all_test_settable_size5.py --output-type perf --run-name bursty-small-wq128 --cfg-filepath ./configs/sweep_perc_threads_burst_dist_small2.json  --alg-stats  --num-iter 10000000 --num-dsas 1 --distribution-filepath ../dto-test-configs/uniform_cpy_64_1024.csv  --results-dirname bursty_small_64_1024_wq128

#python sweep_all_test_settable_size5.py --output-type perf --run-name bursty-small-set-wq128 --cfg-filepath ./configs/sweep_perc_threads_burst_dist_small1.json  --alg-stats  --num-iter 10000000 --num-dsas 1 --distribution-filepath ../dto-test-configs/uniform_set_64_1024.csv  --results-dirname bursty_small_64_1024_set_wq128


#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-perc-uniform-stats_wq128 --cfg-filepath ./configs/auto_dist.json  --alg-stats --num-iter 10000000 --num-dsas 1 --results-dirname sweep_perc_longer_wq128 --distribution-filepath ../dto-test-configs/uniform_cpy_64_1024.csv
#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-perc-uniform_wq128 --cfg-filepath ./configs/sweep_perc_dist.json  --no-stats --num-iter 10000000 --num-dsas 1 --results-dirname sweep_perc_longer_wq128 --distribution-filepath ../dto-test-configs/uniform_cpy_64_1024.csv

#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-size-perc_wq128 --cfg-filepath ./configs/sweep_sizes_auto.json  --alg-stats --num-iter 10000000 --num-dsas 1 --results-dirname sweep_perc_longer_wq128
#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-size-perc_wq128 --cfg-filepath ./configs/sweep_sizes_perc.json  --no-stats --num-iter 10000000 --num-dsas 1 --results-dirname sweep_perc_longer_wq128 


#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-perc-uniform_wq128 --cfg-filepath ./configs/sweep_perc_dist2.json  --alg-stats --num-iter 10000000 --num-dsas 1 --results-dirname sweep_perc_dsa_numa1_wq128 --distribution-filepath ../dto-test-configs/uniform_cpy_64_1024.csv
#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-size-perc_wq128 --cfg-filepath ./configs/sweep_sizes_perc2.json  --alg-stats --num-iter 10000000 --num-dsas 1 --results-dirname sweep_perc_dsa_numa1_wq128 

#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-perc-uniform_wq128 --cfg-filepath ./configs/sweep_perc_dist2.json  --alg-stats --num-iter 10000000 --num-dsas 8 --results-dirname sweep_perc_alldsas_wq128 --distribution-filepath ../dto-test-configs/uniform_cpy_64_1024.csv
#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-size-perc_wq128 --cfg-filepath ./configs/sweep_sizes_perc2.json  --alg-stats --num-iter 10000000 --num-dsas 8 --results-dirname sweep_perc_alldsas_wq128 

#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-perc-uniform_wq128 --cfg-filepath ./configs/sweep_perc_dist2.json  --alg-stats --num-iter 10000000 --num-dsas 1 --results-dirname sweep_perc_dsa_numa0_wq128 --distribution-filepath ../dto-test-configs/uniform_cpy_64_1024.csv
#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-size-perc_wq128 --cfg-filepath ./configs/sweep_sizes_perc2.json  --alg-stats --num-iter 10000000 --num-dsas 1 --results-dirname sweep_perc_dsa_numa0_wq128 

#python sweep_all_test_settable_size5.py --output-type perf --run-name bursty-small-cpy-set-wq128 --cfg-filepath ./configs/sweep_perc_threads_burst_dist_small1.json  --alg-stats  --num-iter 10000000 --num-dsas 1 --distribution-filepath ../dto-test-configs/uniform_cpy_set_64_1024.csv  --results-dirname bursty_small_64_1024_cpy_set_wq128

#sudo ../config_dsa_1_small.sh

#python sweep_all_test_settable_size5.py --output-type perf --run-name bursty-small-set-wq1 --cfg-filepath ./configs/sweep_perc_threads_burst_dist_small1.json  --alg-stats  --num-iter 10000000 --num-dsas 1 --distribution-filepath ../dto-test-configs/uniform_set_64_1024.csv  --results-dirname bursty_small_64_1024_set_wq1

#python sweep_all_test_settable_size5.py --output-type perf --run-name bursty-small-cpy-set-wq1 --cfg-filepath ./configs/sweep_perc_threads_burst_dist_small1.json  --alg-stats  --num-iter 10000000 --num-dsas 1 --distribution-filepath ../dto-test-configs/uniform_cpy_set_64_1024.csv  --results-dirname bursty_small_64_1024_cpy_set_wq1



#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-perc_wq1 --cfg-filepath ./configs/sweep_perc_threads_1024K.json  --alg-stats --num-iter 10000000 --num-dsas 1 --results-dirname sweep_threads_perc_uniform_set_wq1 --distribution-filepath ../dto-test-configs/uniform_set_64_1024.csv

#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-perc_wq1 --cfg-filepath ./configs/sweep_perc_threads_1024K.json  --alg-stats --num-iter 10000000 --num-dsas 1 --results-dirname sweep_threads_perc_uniform_cpy_set_wq1 --distribution-filepath ../dto-test-configs/uniform_cpy_set_64_1024.csv

#sudo ../config_dsa_8.sh


#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-perc_wq128 --cfg-filepath ./configs/sweep_perc_threads_1024K.json  --alg-stats --num-iter 10000000 --num-dsas 1 --results-dirname sweep_threads_perc_uniform_set_wq128 --distribution-filepath ../dto-test-configs/uniform_set_64_1024.csv

#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-perc_wq128 --cfg-filepath ./configs/sweep_perc_threads_1024K.json  --alg-stats --num-iter 10000000 --num-dsas 1 --results-dirname sweep_threads_perc_uniform_cpy_set_wq128 --distribution-filepath ../dto-test-configs/uniform_cpy_set_64_1024.csv

#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-size-perc3_wq128 --cfg-filepath ./configs/sweep_sizes_perc3.json  --alg-stats --num-iter 10000000 --num-dsas 1 --results-dirname sweep_perc_dsas_numa0_wq128 
#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-size-perc3_wq128 --cfg-filepath ./configs/sweep_sizes_perc3.json  --alg-stats --num-iter 10000000 --num-dsas 8 --results-dirname sweep_perc_alldsas_wq128 

#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-perc-uniform_wq128 --cfg-filepath ./configs/sweep_perc_dist2.json  --alg-stats --num-iter 10000000 --num-dsas 1 --results-dirname sweep_perc_dsa_numa1_dist256_wq128 --distribution-filepath ../dto-test-configs/uniform_cpy_256_test.csv

#sudo ../config_dsa_1_numa1.sh

#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-size-perc3_wq128 --cfg-filepath ./configs/sweep_sizes_perc3.json  --alg-stats --num-iter 10000000 --num-dsas 1 --results-dirname sweep_perc_dsas_numa1_wq128 

#sudo ../config_dsa_8.sh

#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-size-perc_wq128 --cfg-filepath ./configs/sweep_sizes_perc.json  --alg-stats --num-iter 1000000 --num-dsas 1 --results-dirname sweep_sizes_perc_wq128

#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-size-perc_wq128 --cfg-filepath ./configs/sweep_threads_sizes_perc.json  --alg-stats --num-iter 10000000 --num-dsas 1 --results-dirname sweep_threads_sizes_perc_set_wq128

#python sweep_all_test_settable_size5.py --output-type DTO_python --run-name pagefaults-1 --cfg-filepath ./configs/test_page_fault_1.json  --alg-stats --num-iter 300000 --num-dsas 1 --results-dirname sweep_page_faults_1
#python sweep_all_test_settable_size5.py --output-type DTO_python --run-name pagefaults-2 --cfg-filepath ./configs/test_page_fault_2.json  --alg-stats --num-iter 300000 --num-dsas 1 --results-dirname sweep_page_faults_2

#python sweep_all_test_settable_size5-2.py --output-type DTO-python --run-name sweep-size-alloc --cfg-filepath ./configs/sweep_sizes_perc_allocs.json  --alg-stats --num-iter 1000000 --num-dsas 1 --results-dirname sweep_sizes_alloc_nohugepages
#python sweep_all_test_settable_size5-2.py --output-type DTO-python --run-name sweep-size-alloc --cfg-filepath ./configs/sweep_sizes_perc_allocs.json  --alg-stats --num-iter 1000000 --num-dsas 1 --results-dirname sweep_sizes_alloc_withhugepages
#python sweep_all_test_settable_size5-2.py --output-type DTO-python --run-name sweep-size-alloc --cfg-filepath ./configs/sweep_sizes_perc_allocs.json  --alg-stats --num-iter 10000000 --num-dsas 1 --results-dirname sweep_sizes_alloc_withhugepages_long
#python sweep_all_test_settable_size5-2.py --output-type DTO-python --run-name sweep-size-alloc --cfg-filepath ./configs/sweep_sizes_perc_allocs.json  --alg-stats --num-iter 10000000 --num-dsas 1 --results-dirname sweep_sizes_alloc_nohugepages_long


#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-size-perc_wq128 --cfg-filepath ./configs/sweep_threads_sizes_perc_cpy_set.json  --alg-stats --num-iter 5000000 --num-dsas 1 --results-dirname sweep_threads_sizes_perc_cpy_set_wq128


#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-size-perc_wq1 --cfg-filepath ./configs/sweep_threads_sizes_perc.json  --alg-stats --num-iter 10000000 --num-dsas 1 --results-dirname sweep_threads_sizes_perc_set_wq1

# start here
#sudo ../config_dsa_1_small.sh
#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-size-perc_wq1 --cfg-filepath ./configs/sweep_threads_sizes_perc_cpy_set.json  --alg-stats --num-iter 5000000 --num-dsas 1 --results-dirname sweep_threads_sizes_perc_cpy_set_wq1



#sudo ../config_dsa_8.sh
#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-size-perc_wq128 --cfg-filepath ./configs/sweep_threads_sizes_perc_mov.json  --alg-stats --num-iter 10000000 --num-dsas 1 --results-dirname sweep_threads_sizes_perc_mov_wq128

#sudo ../config_dsa_1_small.sh
#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-size-perc_wq1 --cfg-filepath ./configs/sweep_threads_sizes_perc_mov.json  --alg-stats --num-iter 10000000 --num-dsas 1 --results-dirname sweep_threads_sizes_perc_mov_wq1


#sudo ../config_dsa_8.sh
#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-size-perc_wq128 --cfg-filepath ./configs/sweep_threads_sizes_perc_cpy.json  --alg-stats --num-iter 10000000 --num-dsas 1 --results-dirname sweep_threads_sizes_perc_cpy_wq128
#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-size-perc_wq128 --cfg-filepath ./configs/junk.json  --alg-stats --num-iter 10000000 --num-dsas 1 --results-dirname sweep_threads_sizes_perc_cpy_wq128_redo

#sudo ../config_dsa_1_small.sh
#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-size-perc_wq1 --cfg-filepath ./configs/sweep_threads_sizes_perc_cpy.json  --alg-stats --num-iter 10000000 --num-dsas 1 --results-dirname sweep_threads_sizes_perc_cpy_wq1

#sudo ../config_dsa_8.sh
#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-size-perc_wq128 --cfg-filepath ./configs/sweep_threads_sizes_perc_combinations_short.json  --alg-stats --num-iter 10000000 --stats-warmup-steps 300000 --exclude-pfs --per-op-instances --num-dsas 1 --results-dirname sweep_threads_sizes_perc_comb_wq128 

#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-size-perc_wq128 --cfg-filepath ./configs/sweep_threads_sizes_perc_combinations.json  --alg-stats --num-iter 10000000 --stats-warmup-steps 300000 --exclude-pfs --per-op-instances --num-dsas 1 --results-dirname sweep_threads_sizes_perc_comb_wq128 
#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-size-perc_wq128 --cfg-filepath ./configs/sweep_threads_sizes_perc_cpy2.json  --alg-stats --num-iter 10000000 --stats-warmup-steps 300000 --exclude-pfs --num-dsas 1 --results-dirname sweep_threads_sizes_perc_cpy_wq128_2
#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-size-perc_wq128 --cfg-filepath ./configs/sweep_threads_sizes_perc_set2.json  --alg-stats --num-iter 10000000 --stats-warmup-steps 300000 --exclude-pfs --num-dsas 1 --results-dirname sweep_threads_sizes_perc_set_wq128_2
#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-size-perc_wq128 --cfg-filepath ./configs/sweep_threads_sizes_perc_mov2.json  --alg-stats --num-iter 10000000 --stats-warmup-steps 300000 --exclude-pfs --num-dsas 1 --results-dirname sweep_threads_sizes_perc_mov_wq128_2

#sudo ../config_dsa_1_small.sh
#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-size-perc_wq1 --cfg-filepath ./configs/sweep_threads_sizes_perc_combinations.json  --alg-stats --num-iter 10000000 --stats-warmup-steps 300000 --exclude-pfs --per-op-instances --num-dsas 1 --results-dirname sweep_threads_sizes_perc_comb_wq1 
#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-size-perc_wq1 --cfg-filepath ./configs/sweep_threads_sizes_perc_cpy2.json  --alg-stats --num-iter 10000000 --stats-warmup-steps 300000 --exclude-pfs --num-dsas 1 --results-dirname sweep_threads_sizes_perc_cpy_wq1_2
#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-size-perc_wq1 --cfg-filepath ./configs/sweep_threads_sizes_perc_set2.json  --alg-stats --num-iter 10000000 --stats-warmup-steps 300000 --exclude-pfs --num-dsas 1 --results-dirname sweep_threads_sizes_perc_set_wq1_2
#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-size-perc_wq1 --cfg-filepath ./configs/sweep_threads_sizes_perc_mov2.json  --alg-stats --num-iter 10000000 --stats-warmup-steps 300000 --exclude-pfs --num-dsas 1 --results-dirname sweep_threads_sizes_perc_mov_wq1_2

#python sweep_all_test_settable_size5.py --output-type perf --run-name separate-procs_wq128 --cfg-filepath ./configs/junk.json  --alg-stats --num-iter 10000000 --num-dsas 1 --results-dirname seperate_proc_cpy_wq128 --separate-processes


#python sweep_all_test_settable_size5.py --output-type perf --run-name mov-overlapping-25_wq128 --cfg-filepath ./configs/junk2.json  --alg-stats --num-iter 10000000 --num-dsas 1 --results-dirname mov_overlapping_wq128 --perc-buffer-overlap 25
#python sweep_all_test_settable_size5.py --output-type perf --run-name mov-overlapping-50_wq128 --cfg-filepath ./configs/junk2.json  --alg-stats --num-iter 10000000 --num-dsas 1 --results-dirname mov_overlapping_wq128 --perc-buffer-overlap 50
#python sweep_all_test_settable_size5.py --output-type perf --run-name mov-overlapping-75_wq128 --cfg-filepath ./configs/junk2.json  --alg-stats --num-iter 10000000 --num-dsas 1 --results-dirname mov_overlapping_wq128 --perc-buffer-overlap 75

#sudo ../config_dsa_4_numa1.sh
#python sweep_all_test_settable_size5.py --output-type perf --run-name mov-overlapping-0_wq128 --cfg-filepath ./configs/junk2.json  --alg-stats --num-iter 10000000 --num-dsas 1 --results-dirname mov_overlapping_dsasamenuma_wq128 --perc-buffer-overlap 0
#python sweep_all_test_settable_size5.py --output-type perf --run-name mov-overlapping-25_wq128 --cfg-filepath ./configs/junk2.json  --alg-stats --num-iter 10000000 --num-dsas 1 --results-dirname mov_overlapping_dsasamenuma_wq128 --perc-buffer-overlap 25
#python sweep_all_test_settable_size5.py --output-type perf --run-name mov-overlapping-50_wq128 --cfg-filepath ./configs/junk2.json  --alg-stats --num-iter 10000000 --num-dsas 1 --results-dirname mov_overlapping_dsasamenuma_wq128 --perc-buffer-overlap 50
#python sweep_all_test_settable_size5.py --output-type perf --run-name mov-overlapping-75_wq128 --cfg-filepath ./configs/junk2.json  --alg-stats --num-iter 10000000 --num-dsas 1 --results-dirname mov_overlapping_dsasamenuma_wq128 --perc-buffer-overlap 75

#sudo ../config_dsa_8.sh
#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-size-perc-bufsize_wq128 --cfg-filepath ./configs/sweep_threads_sizes_perc_bufsizes_cpy_mov.json  --alg-stats --num-iter 10000000 --num-dsas 1 --results-dirname sweep_threads_sizes_perc_bufsizes_cpy_mov_wq128

#python sweep_all_test_settable_size5.py --output-type perf --run-name mov-overlapping-25_wq128 --cfg-filepath ./configs/junk2.json  --alg-stats --num-iter 10000000 --num-dsas 1 --results-dirname mov_overlapping_bufsizes_wq128 --perc-buffer-overlap 25
#python sweep_all_test_settable_size5.py --output-type perf --run-name mov-overlapping-50_wq128 --cfg-filepath ./configs/junk2.json  --alg-stats --num-iter 10000000 --num-dsas 1 --results-dirname mov_overlapping_bufsizes_wq128 --perc-buffer-overlap 50
#python sweep_all_test_settable_size5.py --output-type perf --run-name mov-overlapping-75_wq128 --cfg-filepath ./configs/junk2.json  --alg-stats --num-iter 10000000 --num-dsas 1 --results-dirname mov_overlapping_bufsizes_wq128 --perc-buffer-overlap 75

#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-threads_wq128 --cfg-filepath ./configs/sweep_threads_sizes3.json  --alg-stats --num-iter 10000000 --stats-warmup-steps 300000 --exclude-pfs --num-dsas 1 --results-dirname sweep_threads_small_wq128 
#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-procs_wq128 --cfg-filepath ./configs/sweep_threads_sizes3.json  --alg-stats --num-iter 10000000 --stats-warmup-steps 300000 --exclude-pfs --num-dsas 1 --results-dirname sweep_procs_small_wq128 --separate-processes

#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-threads_wq128 --cfg-filepath ./configs/sweep_threads_sizes_perc_combinations2.json --per-op-instances  --alg-stats --num-iter 10000000 --stats-warmup-steps 300000 --exclude-pfs --num-dsas 1 --results-dirname sweep_threads_comb_small_wq128 

#sudo ../config_dsa_1_small.sh
#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-threads_wq1 --cfg-filepath ./configs/sweep_threads_sizes3.json  --alg-stats --num-iter 10000000 --stats-warmup-steps 300000 --exclude-pfs --num-dsas 1 --results-dirname sweep_threads_small_wq1 
#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-threads_wq1 --cfg-filepath ./configs/sweep_threads_sizes_perc_combinations2.json --per-op-instances --alg-stats --num-iter 10000000 --stats-warmup-steps 300000 --exclude-pfs --num-dsas 1 --results-dirname sweep_threads_small_wq1 

#python sweep_all_test_settable_size5.py --output-type perf --run-name mov-overlapping-50_wq128 --cfg-filepath ./configs/junk2.json  --alg-stats --num-iter 10000000 --num-dsas 1 --results-dirname mov_overlapping_weird_wq128 --perc-buffer-overlap 50

#python sweep_all_test_settable_size5.py --output-type perf --run-name mov-overlapping-50_wq128 --cfg-filepath ./configs/junk3.json  --alg-stats --num-iter 10000 --num-dsas 1  --perc-buffer-overlap 50 --overlap-probability 50 --dry-run

#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-procs_wq128 --cfg-filepath ./configs/junk4.json  --alg-stats --num-iter 10000000 --stats-warmup-steps 300000 --exclude-pfs --num-dsas 1 --results-dirname sweep_procs_small_wq128_rest --separate-processes

#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-procs_wq128 --cfg-filepath ./configs/junk4.json  --alg-stats --num-iter 10000000 --stats-warmup-steps 300000 --exclude-pfs --num-dsas 1 --results-dirname cpy_512K_1thread_wq128
#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-procs_wq128 --cfg-filepath ./configs/junk4.json  --alg-stats --num-iter 10000000 --stats-warmup-steps 300000 --exclude-pfs --num-dsas 1 --results-dirname cpy_256K_1thread_wq128

#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-procs_wq128 --cfg-filepath ./configs/junk4.json  --alg-stats --num-iter 10000000 --stats-warmup-steps 300000 --exclude-pfs --num-dsas 1 --results-dirname cpy_128K_1024K_1thread_wq128

#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-threads_wq128 --cfg-filepath ./configs/sweep_perc_threads_dist.json  --alg-stats --num-iter 10000000 --stats-warmup-steps 300000 --exclude-pfs --num-dsas 1 --results-dirname uniform_64_1024_wq128 --distribution-filepath ../dto-test-configs/uniform_set_64_1024.csv
#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-threads_wq128 --cfg-filepath ./configs/sweep_perc_threads_dist.json  --alg-stats --num-iter 10000000 --stats-warmup-steps 300000 --exclude-pfs --num-dsas 1 --results-dirname uniform_64_1024_wq128 --distribution-filepath ../dto-test-configs/uniform_cpy_64_1024.csv

#python sweep_all_test_settable_size5.py --output-type perf --run-name test-min-size_wq128 --cfg-filepath ./configs/junk.json  --alg-stats --num-iter 10000000 --stats-warmup-steps 300000 --exclude-pfs --num-dsas 1 --results-dirname test_min_size_adj

#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-size-perc-bufsize_wq128 --cfg-filepath ./configs/sweep_threads_sizes_perc_bufsizes_set.json  --alg-stats --num-iter 10000000 --num-dsas 1 --results-dirname sweep_threads_sizes_perc_bufsizes_set_wq128



#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-mov-overlaps_wq128_10 --cfg-filepath ./configs/sweep_sizes_bufsizes_mov_overlaps2.json  --perc-buffer-overlap 10 --overlap-probability 100 --alg-stats --num-iter 10000000 --stats-warmup-steps 300000 --exclude-pfs --num-dsas 1 --results-dirname sweep_move_overlaps_sizes_bufsizes_wq128_2 --overlapping-move-action dsa

#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-mov-overlaps_wq128_00 --cfg-filepath ./configs/sweep_sizes_bufsizes_mov_overlaps.json  --overlap-probability 0 --alg-stats --num-iter 10000000 --stats-warmup-steps 300000 --exclude-pfs --num-dsas 1 --results-dirname sweep_move_overlaps_sizes_bufsizes_wq128

#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-mov-overlaps_wq128_20 --cfg-filepath ./configs/sweep_sizes_bufsizes_mov_overlaps2.json  --perc-buffer-overlap 20 --overlap-probability 100 --alg-stats --num-iter 10000000 --stats-warmup-steps 300000 --exclude-pfs --num-dsas 1 --results-dirname sweep_move_overlaps_sizes_bufsizes_wq128_2 --overlapping-move-action dsa
#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-mov-overlaps_wq128_30 --cfg-filepath ./configs/sweep_sizes_bufsizes_mov_overlaps2.json  --perc-buffer-overlap 30 --overlap-probability 100 --alg-stats --num-iter 10000000 --stats-warmup-steps 300000 --exclude-pfs --num-dsas 1 --results-dirname sweep_move_overlaps_sizes_bufsizes_wq128_2 --overlapping-move-action dsa
#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-mov-overlaps_wq128_40 --cfg-filepath ./configs/sweep_sizes_bufsizes_mov_overlaps2.json  --perc-buffer-overlap 40 --overlap-probability 100 --alg-stats --num-iter 10000000 --stats-warmup-steps 300000 --exclude-pfs --num-dsas 1 --results-dirname sweep_move_overlaps_sizes_bufsizes_wq128_2 --overlapping-move-action dsa
#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-mov-overlaps_wq128_50 --cfg-filepath ./configs/sweep_sizes_bufsizes_mov_overlaps2.json  --perc-buffer-overlap 50 --overlap-probability 100 --alg-stats --num-iter 10000000 --stats-warmup-steps 300000 --exclude-pfs --num-dsas 1 --results-dirname sweep_move_overlaps_sizes_bufsizes_wq128_2 --overlapping-move-action dsa
#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-mov-overlaps_wq128_60 --cfg-filepath ./configs/sweep_sizes_bufsizes_mov_overlaps2.json  --perc-buffer-overlap 60 --overlap-probability 100 --alg-stats --num-iter 10000000 --stats-warmup-steps 300000 --exclude-pfs --num-dsas 1 --results-dirname sweep_move_overlaps_sizes_bufsizes_wq128_2 --overlapping-move-action dsa
#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-mov-overlaps_wq128_70 --cfg-filepath ./configs/sweep_sizes_bufsizes_mov_overlaps2.json  --perc-buffer-overlap 70 --overlap-probability 100 --alg-stats --num-iter 10000000 --stats-warmup-steps 300000 --exclude-pfs --num-dsas 1 --results-dirname sweep_move_overlaps_sizes_bufsizes_wq128_2 --overlapping-move-action dsa
#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-mov-overlaps_wq128_80 --cfg-filepath ./configs/sweep_sizes_bufsizes_mov_overlaps2.json  --perc-buffer-overlap 80 --overlap-probability 100 --alg-stats --num-iter 10000000 --stats-warmup-steps 300000 --exclude-pfs --num-dsas 1 --results-dirname sweep_move_overlaps_sizes_bufsizes_wq128_2 --overlapping-move-action dsa
#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-mov-overlaps_wq128_90 --cfg-filepath ./configs/sweep_sizes_bufsizes_mov_overlaps2.json  --perc-buffer-overlap 90 --overlap-probability 100 --alg-stats --num-iter 10000000 --stats-warmup-steps 300000 --exclude-pfs --num-dsas 1 --results-dirname sweep_move_overlaps_sizes_bufsizes_wq128_2 --overlapping-move-action dsa

#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-freqyencues_wq128_1800 --cfg-filepath ./configs/junk.json  --alg-stats --num-iter 10000000 --stats-warmup-steps 300000 --exclude-pfs --num-dsas 1 --results-dirname cpu_freq_sweep_cpy

#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-freqyencues_wq128_800 --cfg-filepath ./configs/junk.json  --alg-stats --num-iter 10000000 --stats-warmup-steps 300000 --exclude-pfs --num-dsas 1 --results-dirname cpu_freq_sweep_cpy

#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-freqyencues_wq128 --cfg-filepath ./configs/sweep_frequencies.json  --alg-stats --num-iter 10000000 --stats-warmup-steps 300000 --exclude-pfs --num-dsas 1 --results-dirname freq_sweep_cpy_512 --control-frequencies

#python sweep_all_test_settable_size5.py --output-type perf --run-name sweep-freqyencues_wq128 --cfg-filepath ./configs/sweep_frequencies2.json  --alg-stats --num-iter 10000000 --stats-warmup-steps 300000 --exclude-pfs --num-dsas 1 --results-dirname freq_bufsize_sweep_cpy_768 --control-frequencies

#python sweep_all_test_settable_size5.py --output-type micro --run-name tx-size-comp-nostats_wq128 --cfg-filepath ./configs/junk2.json  --dto-version no --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname redis_768_bf_numa1dsa_no_dto

#python sweep_all_test_settable_size5.py --output-type perf --run-name redis-sweep-externdto --cfg-filepath ./configs/reddis_sizes.json  --dto-version extern --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname  redis_sweep_bf_numa1dsa_extern_dto_with_dto_stats

#python sweep_all_test_settable_size5.py --output-type perf --run-name redis-sweep-devdto --cfg-filepath ./configs/reddis_sizes.json  --dto-version dev --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname  redis_sweep_bf_numa1dsa_dev_dto_with_dto_stats 

#python sweep_all_test_settable_size5.py --output-type perf --run-name size-sweep-extern-with --cfg-filepath ./configs/tx_size_comparison_subset.json  --dto-version extern --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname  tx_size_bf_dsa10_cstates_uncmax_extern_with_stats 

#python sweep_all_test_settable_size5.py --output-type micro --run-name size-sweep-nodto --cfg-filepath ./configs/tx_size_comparison_subset3.json  --dto-version no --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname  tx_size_bf_dsa10_cstates_uncmax_no_dto

#python sweep_all_test_settable_size5.py --output-type micro --run-name size-sweep-extern_no --cfg-filepath ./configs/tx_size_comparison_subset.json  --dto-version extern --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname  tx_size_bf_dsa10_cstates_uncmax_extern_no_stats


#python sweep_all_test_settable_size5.py --output-type perf --run-name size-sweep-extern-with-1 --cfg-filepath ./configs/tx_size_comparison_subset.json  --dto-version extern --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname  tx_size_bf_dsa10_cstates_uncmax_extern_with_stats_v2-1 
#python sweep_all_test_settable_size5.py --output-type micro --run-name size-sweep-nodto-1 --cfg-filepath ./configs/tx_size_comparison_subset3.json  --dto-version no --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname  tx_size_bf_dsa10_cstates_uncmax_no_dto_v2-1
#python sweep_all_test_settable_size5.py --output-type micro --run-name size-sweep-extern-no-1 --cfg-filepath ./configs/tx_size_comparison_subset.json  --dto-version extern --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname  tx_size_bf_dsa10_cstates_uncmax_extern_no_stats_v2-1

#python sweep_all_test_settable_size5.py --output-type perf --run-name size-sweep-extern-with-2 --cfg-filepath ./configs/tx_size_comparison_subset.json  --dto-version extern --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname  tx_size_bf_dsa10_cstates_uncmax_extern_with_stats_v2-2 
#python sweep_all_test_settable_size5.py --output-type micro --run-name size-sweep-nodto-2 --cfg-filepath ./configs/tx_size_comparison_subset3.json  --dto-version no --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname  tx_size_bf_dsa10_cstates_uncmax_no_dto_v2-2
#python sweep_all_test_settable_size5.py --output-type micro --run-name size-sweep-extern-no-2 --cfg-filepath ./configs/tx_size_comparison_subset.json  --dto-version extern --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname  tx_size_bf_dsa10_cstates_uncmax_extern_no_stats_v2-2

#python sweep_all_test_settable_size5.py --output-type perf --run-name size-sweep-extern-with-3 --cfg-filepath ./configs/tx_size_comparison_subset.json  --dto-version extern --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname  tx_size_bf_dsa10_cstates_uncmax_extern_with_stats_v2-3 
#python sweep_all_test_settable_size5.py --output-type micro --run-name size-sweep-nodto-3 --cfg-filepath ./configs/tx_size_comparison_subset3.json  --dto-version no --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname  tx_size_bf_dsa10_cstates_uncmax_no_dto_v2-3
#python sweep_all_test_settable_size5.py --output-type micro --run-name size-sweep-extern-no-3 --cfg-filepath ./configs/tx_size_comparison_subset.json  --dto-version extern --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname  tx_size_bf_dsa10_cstates_uncmax_extern_no_stats_v2-3


#python sweep_all_test_settable_size5.py --output-type perf --run-name size-sweep-extern-with-1 --cfg-filepath ./configs/tx_size_comparison_subset4.json  --dto-version extern --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname  tx_size_bf_dsa10_cstates_uncmax_extern_with_stats_v2-1 
#python sweep_all_test_settable_size5.py --output-type micro --run-name size-sweep-nodto-1 --cfg-filepath ./configs/tx_size_comparison_subset5.json  --dto-version no --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname  tx_size_bf_dsa10_cstates_uncmax_no_dto_v2-1
#python sweep_all_test_settable_size5.py --output-type micro --run-name size-sweep-extern-no-1 --cfg-filepath ./configs/tx_size_comparison_sutset4.json  --dto-version extern --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname  tx_size_bf_dsa10_cstates_uncmax_extern_no_stats_v2-1

#python sweep_all_test_settable_size5.py --output-type perf --run-name size-sweep-extern-with-2 --cfg-filepath ./configs/tx_size_comparison_subset4.json  --dto-version extern --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname  tx_size_bf_dsa10_cstates_uncmax_extern_with_stats_v2-2 
#python sweep_all_test_settable_size5.py --output-type micro --run-name size-sweep-nodto-2 --cfg-filepath ./configs/tx_size_comparison_subset5.json  --dto-version no --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname  tx_size_bf_dsa10_cstates_uncmax_no_dto_v2-2
#python sweep_all_test_settable_size5.py --output-type micro --run-name size-sweep-extern-no-2 --cfg-filepath ./configs/tx_size_comparison_subset4.json  --dto-version extern --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname  tx_size_bf_dsa10_cstates_uncmax_extern_no_stats_v2-2

#python sweep_all_test_settable_size5.py --output-type perf --run-name size-sweep-extern-with-3 --cfg-filepath ./configs/tx_size_comparison_subset4.json  --dto-version extern --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname  tx_size_bf_dsa10_cstates_uncmax_extern_with_stats_v2-3 
#python sweep_all_test_settable_size5.py --output-type micro --run-name size-sweep-nodto-3 --cfg-filepath ./configs/tx_size_comparison_subset5.json  --dto-version no --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname  tx_size_bf_dsa10_cstates_uncmax_no_dto_v2-3
#python sweep_all_test_settable_size5.py --output-type micro --run-name size-sweep-extern-no-3 --cfg-filepath ./configs/tx_size_comparison_subset4.json  --dto-version extern --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname  tx_size_bf_dsa10_cstates_uncmax_extern_no_stats_v2-3


#python sweep_all_test_settable_size5.py --output-type perf --run-name size-sweep-turbo-extern-with-1 --cfg-filepath ./configs/tx_size_comparison_subset.json  --dto-version extern --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname  tx_size_turbo_dsa10_cstates_uncmax_extern_with_stats_v2-1 
#python sweep_all_test_settable_size5.py --output-type micro --run-name size-sweep-turbo-nodto-1 --cfg-filepath ./configs/tx_size_comparison_subset3.json  --dto-version no --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname  tx_size_turbo_dsa10_cstates_uncmax_no_dto_v2-1
#python sweep_all_test_settable_size5.py --output-type micro --run-name size-sweep-turbo-extern-no-1 --cfg-filepath ./configs/tx_size_comparison_subset.json  --dto-version extern --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname  tx_size_turbo_dsa10_cstates_uncmax_extern_no_stats_v2-1

#python sweep_all_test_settable_size5.py --output-type perf --run-name size-sweep-turbo-extern-with-2 --cfg-filepath ./configs/tx_size_comparison_subset.json  --dto-version extern --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname  tx_size_turbo_dsa10_cstates_uncmax_extern_with_stats_v2-2 
#python sweep_all_test_settable_size5.py --output-type micro --run-name size-sweep-turbo-nodto-2 --cfg-filepath ./configs/tx_size_comparison_subset3.json  --dto-version no --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname  tx_size_turbo_dsa10_cstates_uncmax_no_dto_v2-2
#python sweep_all_test_settable_size5.py --output-type micro --run-name size-sweep-turbo-extern-no-2 --cfg-filepath ./configs/tx_size_comparison_subset.json  --dto-version extern --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname  tx_size_turbo_dsa10_cstates_uncmax_extern_no_stats_v2-2

#python sweep_all_test_settable_size5.py --output-type perf --run-name size-sweep-turbo-extern-with-3 --cfg-filepath ./configs/tx_size_comparison_subset.json  --dto-version extern --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname  tx_size_turbo_dsa10_cstates_uncmax_extern_with_stats_v2-3 
#python sweep_all_test_settable_size5.py --output-type micro --run-name size-sweep-turbo-nodto-3 --cfg-filepath ./configs/tx_size_comparison_subset3.json  --dto-version no --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname  tx_size_turbo_dsa10_cstates_uncmax_no_dto_v2-3
#python sweep_all_test_settable_size5.py --output-type micro --run-name size-sweep-turbo-extern-no-3 --cfg-filepath ./configs/tx_size_comparison_subset.json  --dto-version extern --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname  tx_size_turbo_dsa10_cstates_uncmax_extern_no_stats_v2-3

#python sweep_all_test_settable_size5.py --output-type micro --run-name size-sweep-turbo-extern-no_1 --cfg-filepath ./configs/junk4.json  --dto-version extern --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname  128k_turbo_dsa10_cstates_uncmax_extern_no_stats_full_1
#python sweep_all_test_settable_size5.py --output-type micro --run-name size-sweep-turbo-extern-no_2 --cfg-filepath ./configs/junk4.json  --dto-version extern --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname  128k_turbo_dsa10_cstates_uncmax_extern_no_stats_full_2
#python sweep_all_test_settable_size5.py --output-type micro --run-name size-sweep-turbo-extern-no_3 --cfg-filepath ./configs/junk4.json  --dto-version extern --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname  128k_turbo_dsa10_cstates_uncmax_extern_no_stats_full_3

#python sweep_all_test_settable_size5.py --output-type perf --run-name size-sweep-turbo-dev-alg-stats_1 --cfg-filepath ./configs/junk5.json  --dto-version dev --alg-stats --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname  128k_turbo_dsa10_cstates_uncmax_dev_alg_stats_full_1

#python sweep_all_test_settable_size5.py --output-type perf --run-name size-sweep-bf-dev-alg-stats_1 --cfg-filepath ./configs/junk5.json  --dto-version dev --alg-stats --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname  128k_bf_dsa10_cstates_uncmax_dev_alg_stats_full_1


#python sweep_all_test_settable_size5.py --output-type micro --run-name global-q-selector-bf-extern-single --cfg-filepath ./configs/q_selector.json  --dto-version extern --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --results-dirname  global_q_selector_bf_singlenuma

#python sweep_all_test_settable_size5.py --output-type micro --run-name local-q-selector-bf-extern-single --cfg-filepath ./configs/q_selector.json  --dto-version extern_localq --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --results-dirname  local_q_selector_bf_singlenuma

#python sweep_all_test_settable_size5.py --output-type micro --run-name global-q-selector-bf-extern-cross --cfg-filepath ./configs/q_selector.json  --dto-version extern --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --cross-numa --results-dirname  global_q_selector_bf_crossnuma

#python sweep_all_test_settable_size5.py --output-type micro --run-name local-q-selector-bf-extern-cross --cfg-filepath ./configs/q_selector.json  --dto-version extern_localq --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --cross-numa --results-dirname  local_q_selector_bf_crossnuma


#python sweep_all_test_settable_size5.py --output-type micro --run-name global-q-selector-bf-extern --cfg-filepath ./configs/q_selector2.json --dto-version extern --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --start-cpu 1 --results-dirname  global_q_selector_bf_2

#python sweep_all_test_settable_size5.py --output-type micro --run-name local-q-selector-bf-extern  --cfg-filepath ./configs/q_selector2.json --dto-version extern --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --start-cpu 1 --results-dirname local_q_selector_bf_2


#python sweep_all_test_settable_size5.py --output-type micro --run-name redis-bf-extern-no-stats --cfg-filepath ./configs/reddis_sizes.json  --dto-version extern --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname  bf_redis_no_stats
#python sweep_all_test_settable_size5.py --output-type perf --run-name redis-bf-extern-with-stats --cfg-filepath ./configs/reddis_sizes.json  --dto-version extern --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname  bf_redis_with_stats
#python sweep_all_test_settable_size5.py --output-type micro --run-name redis-bf-extern-no-dto --cfg-filepath ./configs/reddis_sizes.json  --dto-version no --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname  bf_redis_no_dto


#python sweep_all_test_settable_size5.py --output-type perf --run-name size-sweep-o2-extern-with-stats_1 --cfg-filepath ./configs/tx_size_comparison_subset.json  --dto-version extern --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname  tx_size_bf_o2_extern_with_stats
#python sweep_all_test_settable_size5.py --output-type micro --run-name size-sweep-o2-extern-no-stats_1 --cfg-filepath ./configs/tx_size_comparison_subset.json  --dto-version extern --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname  tx_size_bf_o2_extern_no_stats 
#python sweep_all_test_settable_size5.py --output-type perf --run-name size-sweep-o2-extern-with-stats_2 --cfg-filepath ./configs/tx_size_comparison_subset.json  --dto-version extern --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname  tx_size_bf_o2_extern_with_stats
#python sweep_all_test_settable_size5.py --output-type micro --run-name size-sweep-o2-extern-no-stats_2 --cfg-filepath ./configs/tx_size_comparison_subset.json  --dto-version extern --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname  tx_size_bf_o2_extern_no_stats
#python sweep_all_test_settable_size5.py --output-type micro --run-name size-sweep-o2-extern-no-stats_3 --cfg-filepath ./configs/tx_size_comparison_subset.json  --dto-version extern --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname  tx_size_bf_o2_extern_no_stats
#python sweep_all_test_settable_size5.py --output-type perf --run-name size-sweep-o2-extern-with-stats_3 --cfg-filepath ./configs/tx_size_comparison_subset.json  --dto-version extern --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname  tx_size_bf_o2_extern_with_stats

#python sweep_all_test_settable_size5.py --output-type perf --run-name size-sweep-unopt-extern-with-stats_1 --cfg-filepath ./configs/tx_size_comparison_subset.json  --dto-version extern --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname  tx_size_bf_unopt_extern_with_stats 
#python sweep_all_test_settable_size5.py --output-type micro --run-name size-sweep-unopt-extern-no-stats_1 --cfg-filepath ./configs/tx_size_comparison_subset.json  --dto-version extern --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname  tx_size_bf_unopt_extern_no_stats 
#python sweep_all_test_settable_size5.py --output-type perf --run-name size-sweep-unopt-extern-with-stats_2 --cfg-filepath ./configs/tx_size_comparison_subset.json  --dto-version extern --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname  tx_size_bf_unopt_extern_with_stats
#python sweep_all_test_settable_size5.py --output-type micro --run-name size-sweep-unopt-extern-no-stats_2 --cfg-filepath ./configs/tx_size_comparison_subset.json  --dto-version extern --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname  tx_size_bf_unopt_extern_no_stats
#python sweep_all_test_settable_size5.py --output-type micro --run-name size-sweep-unopt-extern-no-stats_3 --cfg-filepath ./configs/tx_size_comparison_subset.json  --dto-version extern --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname  tx_size_bf_unopt_extern_no_stats
#python sweep_all_test_settable_size5.py --output-type perf --run-name size-sweep-unopt-extern-with-stats_3 --cfg-filepath ./configs/tx_size_comparison_subset.json  --dto-version extern --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname  tx_size_bf_unopt_extern_with_stats

#python sweep_all_test_settable_size5.py --output-type micro --run-name global-q-selector-bf-extern-cross_1 --cfg-filepath ./configs/q_selector.json  --dto-version extern --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --cross-numa --results-dirname  global_q_selector_bf_crossnuma_2
#python sweep_all_test_settable_size5.py --output-type micro --run-name global-q-selector-bf-extern-cross_2 --cfg-filepath ./configs/q_selector.json  --dto-version extern --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --cross-numa --results-dirname  global_q_selector_bf_crossnuma_2
#python sweep_all_test_settable_size5.py --output-type micro --run-name global-q-selector-bf-extern-cross_3 --cfg-filepath ./configs/q_selector.json  --dto-version extern --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --cross-numa --results-dirname  global_q_selector_bf_crossnuma_2
#python sweep_all_test_settable_size5.py --output-type micro --run-name global-q-selector-bf-extern-cross_4 --cfg-filepath ./configs/q_selector.json  --dto-version extern --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --cross-numa --results-dirname  global_q_selector_bf_crossnuma_2
#python sweep_all_test_settable_size5.py --output-type micro --run-name global-q-selector-bf-extern-cross_5 --cfg-filepath ./configs/q_selector.json  --dto-version extern --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --cross-numa --results-dirname  global_q_selector_bf_crossnuma_2
#python sweep_all_test_settable_size5.py --output-type micro --run-name global-q-selector-bf-extern-cross_6 --cfg-filepath ./configs/q_selector.json  --dto-version extern --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --cross-numa --results-dirname  global_q_selector_bf_crossnuma_2
#python sweep_all_test_settable_size5.py --output-type micro --run-name global-q-selector-bf-extern-cross_7 --cfg-filepath ./configs/q_selector.json  --dto-version extern --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --cross-numa --results-dirname  global_q_selector_bf_crossnuma_2
#python sweep_all_test_settable_size5.py --output-type micro --run-name global-q-selector-bf-extern-cross_8 --cfg-filepath ./configs/q_selector.json  --dto-version extern --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --cross-numa --results-dirname  global_q_selector_bf_crossnuma_2
#python sweep_all_test_settable_size5.py --output-type micro --run-name global-q-selector-bf-extern-cross_9 --cfg-filepath ./configs/q_selector.json  --dto-version extern --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --cross-numa --results-dirname  global_q_selector_bf_crossnuma_2
#python sweep_all_test_settable_size5.py --output-type micro --run-name global-q-selector-bf-extern-cross_10 --cfg-filepath ./configs/q_selector.json  --dto-version extern --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --cross-numa --results-dirname global_q_selector_bf_crossnuma_2

#python sweep_all_test_settable_size5.py --output-type micro --run-name local-q-selector-bf-extern-cross_1 --cfg-filepath ./configs/q_selector.json  --dto-version extern_localq --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --cross-numa --results-dirname  local_q_selector_bf_crossnuma_2
#python sweep_all_test_settable_size5.py --output-type micro --run-name local-q-selector-bf-extern-cross_2 --cfg-filepath ./configs/q_selector.json  --dto-version extern_localq --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --cross-numa --results-dirname  local_q_selector_bf_crossnuma_2
#python sweep_all_test_settable_size5.py --output-type micro --run-name local-q-selector-bf-extern-cross_3 --cfg-filepath ./configs/q_selector.json  --dto-version extern_localq --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --cross-numa --results-dirname  local_q_selector_bf_crossnuma_2
#python sweep_all_test_settable_size5.py --output-type micro --run-name local-q-selector-bf-extern-cross_4 --cfg-filepath ./configs/q_selector.json  --dto-version extern_localq --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --cross-numa --results-dirname  local_q_selector_bf_crossnuma_2
#python sweep_all_test_settable_size5.py --output-type micro --run-name local-q-selector-bf-extern-cross_5 --cfg-filepath ./configs/q_selector.json  --dto-version extern_localq --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --cross-numa --results-dirname  local_q_selector_bf_crossnuma_2
#python sweep_all_test_settable_size5.py --output-type micro --run-name local-q-selector-bf-extern-cross_6 --cfg-filepath ./configs/q_selector.json  --dto-version extern_localq --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --cross-numa --results-dirname  local_q_selector_bf_crossnuma_2
#python sweep_all_test_settable_size5.py --output-type micro --run-name local-q-selector-bf-extern-cross_7 --cfg-filepath ./configs/q_selector.json  --dto-version extern_localq --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --cross-numa --results-dirname  local_q_selector_bf_crossnuma_2
#python sweep_all_test_settable_size5.py --output-type micro --run-name local-q-selector-bf-extern-cross_8 --cfg-filepath ./configs/q_selector.json  --dto-version extern_localq --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --cross-numa --results-dirname  local_q_selector_bf_crossnuma_2
#python sweep_all_test_settable_size5.py --output-type micro --run-name local-q-selector-bf-extern-cross_9 --cfg-filepath ./configs/q_selector.json  --dto-version extern_localq --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --cross-numa --results-dirname  local_q_selector_bf_crossnuma_2
#python sweep_all_test_settable_size5.py --output-type micro --run-name local-q-selector-bf-extern-cross_10 --cfg-filepath ./configs/q_selector.json  --dto-version extern_localq --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --cross-numa --results-dirname  local_q_selector_bf_crossnuma_2

#python sweep_all_test_settable_size5.py --output-type micro --run-name global-q-selector-bf-extern-cross_1 --cfg-filepath ./configs/q_selector3.json  --dto-version extern --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --cross-numa --results-dirname  global_q_selector_bf_crossnuma_2
#python sweep_all_test_settable_size5.py --output-type micro --run-name global-q-selector-bf-extern-cross_2 --cfg-filepath ./configs/q_selector3.json  --dto-version extern --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --cross-numa --results-dirname  global_q_selector_bf_crossnuma_2
#python sweep_all_test_settable_size5.py --output-type micro --run-name global-q-selector-bf-extern-cross_3 --cfg-filepath ./configs/q_selector3.json  --dto-version extern --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --cross-numa --results-dirname  global_q_selector_bf_crossnuma_2
#python sweep_all_test_settable_size5.py --output-type micro --run-name global-q-selector-bf-extern-cross_4 --cfg-filepath ./configs/q_selector3.json  --dto-version extern --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --cross-numa --results-dirname  global_q_selector_bf_crossnuma_2
#python sweep_all_test_settable_size5.py --output-type micro --run-name global-q-selector-bf-extern-cross_5 --cfg-filepath ./configs/q_selector3.json  --dto-version extern --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --cross-numa --results-dirname  global_q_selector_bf_crossnuma_2
#python sweep_all_test_settable_size5.py --output-type micro --run-name global-q-selector-bf-extern-cross_6 --cfg-filepath ./configs/q_selector3.json  --dto-version extern --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --cross-numa --results-dirname  global_q_selector_bf_crossnuma_2
#python sweep_all_test_settable_size5.py --output-type micro --run-name global-q-selector-bf-extern-cross_7 --cfg-filepath ./configs/q_selector3.json  --dto-version extern --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --cross-numa --results-dirname  global_q_selector_bf_crossnuma_2
#python sweep_all_test_settable_size5.py --output-type micro --run-name global-q-selector-bf-extern-cross_8 --cfg-filepath ./configs/q_selector3.json  --dto-version extern --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --cross-numa --results-dirname  global_q_selector_bf_crossnuma_2
#python sweep_all_test_settable_size5.py --output-type micro --run-name global-q-selector-bf-extern-cross_9 --cfg-filepath ./configs/q_selector3.json  --dto-version extern --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --cross-numa --results-dirname  global_q_selector_bf_crossnuma_2
#python sweep_all_test_settable_size5.py --output-type micro --run-name global-q-selector-bf-extern-cross_10 --cfg-filepath ./configs/q_selector3.json  --dto-version extern --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --cross-numa --results-dirname global_q_selector_bf_crossnuma_2

#python sweep_all_test_settable_size5.py --output-type micro --run-name local-q-selector-bf-extern-cross_1 --cfg-filepath ./configs/q_selector3.json  --dto-version extern_localq --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --cross-numa --results-dirname  local_q_selector_bf_crossnuma_2
#python sweep_all_test_settable_size5.py --output-type micro --run-name local-q-selector-bf-extern-cross_2 --cfg-filepath ./configs/q_selector3.json  --dto-version extern_localq --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --cross-numa --results-dirname  local_q_selector_bf_crossnuma_2
#python sweep_all_test_settable_size5.py --output-type micro --run-name local-q-selector-bf-extern-cross_3 --cfg-filepath ./configs/q_selector3.json  --dto-version extern_localq --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --cross-numa --results-dirname  local_q_selector_bf_crossnuma_2
#python sweep_all_test_settable_size5.py --output-type micro --run-name local-q-selector-bf-extern-cross_4 --cfg-filepath ./configs/q_selector3.json  --dto-version extern_localq --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --cross-numa --results-dirname  local_q_selector_bf_crossnuma_2
#python sweep_all_test_settable_size5.py --output-type micro --run-name local-q-selector-bf-extern-cross_5 --cfg-filepath ./configs/q_selector3.json  --dto-version extern_localq --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --cross-numa --results-dirname  local_q_selector_bf_crossnuma_2
#python sweep_all_test_settable_size5.py --output-type micro --run-name local-q-selector-bf-extern-cross_6 --cfg-filepath ./configs/q_selector3.json  --dto-version extern_localq --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --cross-numa --results-dirname  local_q_selector_bf_crossnuma_2
#python sweep_all_test_settable_size5.py --output-type micro --run-name local-q-selector-bf-extern-cross_7 --cfg-filepath ./configs/q_selector3.json  --dto-version extern_localq --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --cross-numa --results-dirname  local_q_selector_bf_crossnuma_2
#python sweep_all_test_settable_size5.py --output-type micro --run-name local-q-selector-bf-extern-cross_8 --cfg-filepath ./configs/q_selector3.json  --dto-version extern_localq --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --cross-numa --results-dirname  local_q_selector_bf_crossnuma_2
#python sweep_all_test_settable_size5.py --output-type micro --run-name local-q-selector-bf-extern-cross_9 --cfg-filepath ./configs/q_selector3.json  --dto-version extern_localq --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --cross-numa --results-dirname  local_q_selector_bf_crossnuma_2
#python sweep_all_test_settable_size5.py --output-type micro --run-name local-q-selector-bf-extern-cross_10 --cfg-filepath ./configs/q_selector3.json  --dto-version extern_localq --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --cross-numa --results-dirname  local_q_selector_bf_crossnuma_2


#python sweep_all_test_settable_size5.py --output-type micro --run-name global-q-selector-bf-dev-cross_1 --cfg-filepath ./configs/q_selector.json  --dto-version dev --alg-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --cross-numa --results-dirname  global_q_selector_bf_crossnuma_stats
#python sweep_all_test_settable_size5.py --output-type micro --run-name global-q-selector-bf-dev-cross_2 --cfg-filepath ./configs/q_selector.json  --dto-version dev --alg-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --cross-numa --results-dirname  global_q_selector_bf_crossnuma_stats
#python sweep_all_test_settable_size5.py --output-type micro --run-name global-q-selector-bf-dev-cross_3 --cfg-filepath ./configs/q_selector.json  --dto-version dev --alg-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --cross-numa --results-dirname  global_q_selector_bf_crossnuma_stats

#python sweep_all_test_settable_size5.py --output-type micro --run-name local-q-selector-bf-dev-cross_1 --cfg-filepath ./configs/q_selector.json  --dto-version dev_localq --alg-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --cross-numa --results-dirname  local_q_selector_bf_crossnuma_stats
#python sweep_all_test_settable_size5.py --output-type micro --run-name local-q-selector-bf-dev-cross_2 --cfg-filepath ./configs/q_selector.json  --dto-version dev_localq --alg-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --cross-numa --results-dirname  local_q_selector_bf_crossnuma_stats
#python sweep_all_test_settable_size5.py --output-type micro --run-name local-q-selector-bf-dev-cross_3 --cfg-filepath ./configs/q_selector.json  --dto-version dev_localq --alg-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --cross-numa --results-dirname  local_q_selector_bf_crossnuma_stats

#python sweep_all_test_settable_size5.py --output-type micro --run-name global-q-selector-bf-dev-cross_1 --cfg-filepath ./configs/q_selector_auto.json  --dto-version dev --alg-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --cross-numa --results-dirname  global_q_selector_bf_crossnuma_stats
#python sweep_all_test_settable_size5.py --output-type micro --run-name global-q-selector-bf-dev-cross_2 --cfg-filepath ./configs/q_selector_auto.json  --dto-version dev --alg-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --cross-numa --results-dirname  global_q_selector_bf_crossnuma_stats
#python sweep_all_test_settable_size5.py --output-type micro --run-name global-q-selector-bf-dev-cross_3 --cfg-filepath ./configs/q_selector_auto.json  --dto-version dev --alg-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --cross-numa --results-dirname  global_q_selector_bf_crossnuma_stats

#python sweep_all_test_settable_size5.py --output-type micro --run-name local-q-selector-bf-dev-cross_1 --cfg-filepath ./configs/q_selector_auto.json  --dto-version dev_localq --alg-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --cross-numa --results-dirname  local_q_selector_bf_crossnuma_stats
#python sweep_all_test_settable_size5.py --output-type micro --run-name local-q-selector-bf-dev-cross_2 --cfg-filepath ./configs/q_selector_auto.json  --dto-version dev_localq --alg-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --cross-numa --results-dirname  local_q_selector_bf_crossnuma_stats
#python sweep_all_test_settable_size5.py --output-type micro --run-name local-q-selector-bf-dev-cross_3 --cfg-filepath ./configs/q_selector_auto.json  --dto-version dev_localq --alg-stats --num-iter 10000000 --exclude-pfs --num-dsas 8 --cross-numa --results-dirname  local_q_selector_bf_crossnuma_stats

#python sweep_all_test_settable_size5.py --output-type micro --run-name extern-no-stats_1 --cfg-filepath ./configs/reddis_sizes_dev_vs_up.json  --dto-version extern --no-stats --num-iter 10000000 --exclude-pfs --results-dirname  dev_vs_upstream_extern_no_stats
#python sweep_all_test_settable_size5.py --output-type perf --run-name extern-with-stats_1 --cfg-filepath ./configs//reddis_sizes_dev_vs_up.json  --dto-version extern --num-iter 10000000 --exclude-pfs  --results-dirname  dev_vs_upstream_extern_with_stats 

#python sweep_all_test_settable_size5.py --output-type micro --run-name dev-no-stats_1 --cfg-filepath ./configs/reddis_sizes_dev_vs_up.json  --dto-version dev --no-stats --num-iter 10000000 --exclude-pfs --results-dirname  dev_vs_upstream_dev_no_stats 
#python sweep_all_test_settable_size5.py --output-type perf --run-name dev-with-stats_1 --cfg-filepath ./configs/reddis_sizes_dev_vs_up.json  --dto-version dev --num-iter 10000000 --exclude-pfs  --results-dirname  dev_vs_upstream_dev_with_stats 

#python sweep_all_test_settable_size5.py --output-type micro --run-name dev-with-times-8dsas_1 --cfg-filepath ./configs/reddis_sizes_dev_vs_up3.json  --dto-version dev --alg-stats --num-iter 1000000 --exclude-pfs --num-dsas 8 --results-dirname  dev_with_times_8dsas 

#python sweep_all_test_settable_size5.py --output-type micro --run-name dev-with-times-1dsa_1 --cfg-filepath ./configs/reddis_sizes_dev_vs_up3.json  --dto-version dev --alg-stats --num-iter 1000000 --exclude-pfs  --num-dsas 1 --results-dirname  dev_with_times_1dsa

#python sweep_all_test_settable_size5.py --output-type micro --run-name in-cache-prob-sweep-1dsa-dev-with-times_1 --cfg-filepath ./configs/reddis_800_in_cache_prob_auto.json  --dto-version dev_time --alg-stats --num-iter 1000000 --exclude-pfs  --num-dsas 1 --results-dirname  in-cache-prob-dev_with_times_1dsa

#python sweep_all_test_settable_size5.py --output-type micro --run-name in-cache-prob-sweep-1dsa-dev-no-stats_1 --cfg-filepath ./configs/reddis_800_in_cache_prob.json  --dto-version dev --no-stats --num-iter 10000000 --exclude-pfs  --num-dsas 1 --results-dirname  in-cache-prob-dev_no_stats_1dsa

#python sweep_all_test_settable_size5.py --output-type perf --run-name in-cache-prob-sweep-1dsa-extern-with-stats_1 --cfg-filepath ./configs/reddis_800_in_cache_prob.json  --dto-version extern --num-iter 10000000 --exclude-pfs  --num-dsas 1 --results-dirname  in-cache-prob-extern_with_stats_1dsa

#python sweep_all_test_settable_size5.py --output-type micro --run-name in-cache-prob-sweep-1dsa-extern-no-stats_1 --cfg-filepath ./configs/reddis_800_in_cache_prob.json  --dto-version extern --no-stats --num-iter 10000000 --exclude-pfs  --num-dsas 1 --results-dirname  in-cache-prob-extern_no_stats_1dsa

#python sweep_all_test_settable_size5.py --output-type perf --run-name in-cache-prob-sweep-1dsa-extern-with-stats_1 --cfg-filepath ./configs/reddis_800_in_cache_prob_perc_sweep.json  --dto-version extern --num-iter 10000000 --exclude-pfs  --num-dsas 1 --results-dirname  in-cache-prob-extern_with_stats_1dsa

#python sweep_all_test_settable_size5.py --output-type micro --run-name raw-op-times-extern-no-stats_1 --cfg-filepath ./configs/raw_time_collection.json  --dto-version extern --no-stats --collect-raw-times --num-iter 1000000 --exclude-pfs  --num-dsas 1 --results-dirname  raw_op_times_extern_no_stats

#python sweep_all_test_settable_size5.py --output-type micro --run-name raw-op-times-extern-no-stats2_1 --cfg-filepath ./configs/raw_time_collection.json  --dto-version extern --no-stats --collect-raw-times --num-iter 1000000 --exclude-pfs  --num-dsas 1 --results-dirname  raw_op_times_extern_no_stats_32bit


#python sweep_all_test_settable_size5.py --output-type perf --run-name in-cache-prob-sweep-1dsa-extern-with-stats_1 --cfg-filepath ./configs/reddis_800_in_cache_prob_perc_sweep.json  --dto-version extern --num-iter 10000000 --exclude-pfs  --num-dsas 1 --results-dirname  in-cache-prob-extern_with_stats_1dsa
#python sweep_all_test_settable_size5.py --output-type micro --run-name in-cache-prob-sweep-1dsa-dev-with-times_1 --cfg-filepath ./configs/reddis_800_in_cache_prob_auto.json  --dto-version dev_time --alg-stats --num-iter 1000000 --exclude-pfs  --num-dsas 1 --results-dirname  in-cache-prob-dev_with_times_1dsa

#python sweep_all_test_settable_size5.py --output-type micro --run-name raw-op-times-extern-no-stats2_1 --cfg-filepath ./configs/raw_time_collection_subset_threads.json  --dto-version extern --no-stats --collect-raw-times --num-iter 1000000 --exclude-pfs  --num-dsas 1 --results-dirname  raw_op_times_extern_no_stats_threads

#python sweep_all_test_settable_size5.py --output-type perf --run-name in-cache-prob-sweep-1dsa-extern-with-stats_1 --cfg-filepath ./configs/reddis_800_in_cache_prob_perc_sweep_threads2.json  --dto-version extern --num-iter 10000000 --exclude-pfs  --num-dsas 1 --results-dirname  in-cache-prob-extern_with_stats_1dsa_2
#python sweep_all_test_settable_size5.py --output-type micro --run-name in-cache-prob-sweep-1dsa-dev-with-times_1 --cfg-filepath ./configs/reddis_800_in_cache_prob_auto_threads.json  --dto-version dev_time --alg-stats --num-iter 1000000 --exclude-pfs  --num-dsas 1 --results-dirname  in-cache-prob-dev_with_times_1dsa

#python sweep_all_test_settable_size5.py --output-type perf --run-name in-cache-prob-sweep-1dsa-minwait-extern-with-stats_3 --cfg-filepath ./configs/reddis_800_in_cache_prob_auto_1threads_min_wait.json  --dto-version extern --num-iter 10000000 --exclude-pfs  --num-dsas 1 --results-dirname  in-cache-prob-extern_with_stats_1dsa_minwaits3

#python sweep_all_test_settable_size5.py --output-type micro --run-name in-cache-prob-sweep-1dsa-minwait-dev-alg-stats_3 --cfg-filepath ./configs/reddis_800_in_cache_prob_auto_1threads_min_wait.json  --dto-version dev --alg-stats --num-iter 10000000 --exclude-pfs  --num-dsas 1 --results-dirname  in-cache-prob-dev_alg_stats_1dsa_minwaits3

#python sweep_all_test_settable_size5.py --output-type micro --run-name in-cache-prob-sweep-1dsa-minwait-extern-no-stats_3 --cfg-filepath ./configs/reddis_800_in_cache_prob_auto_1threads_min_wait.json  --dto-version extern --no-stats --num-iter 10000000 --exclude-pfs  --num-dsas 1 --results-dirname  in-cache-prob-extern_no_stats_1dsa_minwaits3


#python sweep_all_test_settable_size5.py --output-type micro --run-name in-cache-prob-sweep-1dsa-minwait-dev-with-times_3 --cfg-filepath ./configs/reddis_800_in_cache_prob_auto_1threads_min_wait.json  --dto-version dev_time --alg-stats --num-iter 1000000 --exclude-pfs  --num-dsas 1 --results-dirname  in-cache-prob-dev_with_times_1dsa_minwaits3
#python sweep_all_test_settable_size5.py --output-type micro --run-name in-cache-prob-sweep-1dsa-minwait-extern-raw-times_3 --cfg-filepath ./configs/reddis_800_in_cache_prob_auto_1threads_min_wait.json  --dto-version extern --no-stats --collect-raw-times --num-iter 1000000 --exclude-pfs  --num-dsas 1 --results-dirname  in-cache-prob-extern-raw_times_1dsa_minwaits3

#python sweep_all_test_settable_size5.py --output-type micro --run-name in-cache-prob-sweep-1dsa-minwait-dev-raw-times_3 --cfg-filepath ./configs/reddis_800_in_cache_prob_auto_1threads_min_wait.json  --dto-version dev_time --collect-raw-times --alg-stats --num-iter 1000000 --exclude-pfs  --num-dsas  1 --results-dirname  in-cache-prob-dev_raw_times_1dsa_minwaits3
#python sweep_all_test_settable_size5.py --output-type micro --run-name in-cache-prob-sweep-1dsa-minwait-uniform-dev-raw-times --cfg-filepath ./configs/reddis_800_in_cache_prob_auto_1threads_min_wait.json  --dto-version dev_time --collect-raw-times --alg-stats --num-iter 1000000 --exclude-pfs  --num-dsas  1 --results-dirname  in-cache-prob-dev_raw_times_1dsa_minwait_uniform

#python sweep_all_test_settable_size5.py --output-type perf --run-name tx-size-in-cache-perc-extern-with-stats --cfg-filepath ./configs/tx_size_comparison_subset.json  --dto-version extern --num-iter 10000000 --exclude-pfs  --num-dsas 1 --results-dirname  tx_size_in_cache_perc_sweep_minwaits 
#python sweep_all_test_settable_size5.py --output-type perf --run-name tx-size-in-cache-perc-extern-with-stats --cfg-filepath ./configs/tx_size_comparison_subset_auto.json  --dto-version extern --num-iter 10000000 --exclude-pfs  --num-dsas 1 --results-dirname  tx_size_in_cache_perc_sweep_minwaits

python sweep_all_test_settable_size5.py --output-type micro --run-name in-cache-prob-sweep-1dsa-minwait-dev-raw-times_3 --cfg-filepath ./configs/reddis_800_in_cache_prob_perc_sweep.json  --dto-version dev_time --collect-raw-times --alg-stats --num-iter 1000000  --exclude-pfs  --num-dsas  1 --results-dirname  in-cache-prob-dev_raw_times_1dsa_minwaits3_perc_sweep

