

tries=100
 


while [ "$tries" -gt 0 ]; do
    $1
    tries=$(( tries - 1 ))
done