i=0
while [ $i -lt 1 ]; do
    echo "Loop $i"
    i=$((i+1))
done > out_crash.txt
