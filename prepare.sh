#/bin/sh

gcc sla_transparent.c throttler.c -o throttler
rm /tmp/fifo_user*

for ((i=0; i<$1; i ++))
do  
    echo "makefifo " $i
    ./makefifo /tmp/fifo_user${i}_r
    ./makefifo /tmp/fifo_user${i}_w
done 
