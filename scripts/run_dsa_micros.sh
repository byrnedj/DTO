declare -a sizes=(8 16 32 64 128 256 512)

OUTFILE='./results/test_dsa_perf_micros'
echo " " > $OUTFILE
for sz in "${sizes[@]}" 
do
    echo "##############./src/dsa_perf_micros -n1 -s${sz}k -j -f -i10000000 -k56 -w1 -zF,F -o3 >> "$OUTFILE
    for (( i=1; i<=3; i++ )); do
        /home/jjsydir/dsa-perf-micros/src/dsa_perf_micros -n1 -s${sz}k -j -f -i10000000 -k56 -w1 -zF,F -o3 >> $OUTFILE
    done
done



