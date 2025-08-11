

python sweep_all_test_settable_size5.py --output-type micro --run-name redis-bf-extern-no-stats-1 --cfg-filepath ./configs/reddis_sizes.json  --dto-version extern --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname  bf_redis_no_stats
python sweep_all_test_settable_size5.py --output-type perf --run-name redis-bf-extern-with-stats-1 --cfg-filepath ./configs/reddis_sizes.json  --dto-version extern --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname  bf_redis_with_stats
python sweep_all_test_settable_size5.py --output-type micro --run-name redis-bf-extern-no-dto-1 --cfg-filepath ./configs/reddis_sizes.json  --dto-version no --no-stats --num-iter 10000000 --exclude-pfs --num-dsas 1 --results-dirname  bf_redis_no_dto