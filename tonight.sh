#!/bin/bash
cache_blksize=(20595 24795 48167 19554 106230 65278 39736 51410 36268 43761)
fifo_blksize=(3987 4941 9513 3754 21206 13054 7645 10273 6731 7451)


for i in "${!cache_blksize[@]}";
do
        ./smr-ssd-cache 0 0 $i 0 0 10000000 ${fifo_blksize[$i]} 0 1 > /home/outputs/pore_test_outputfiles/rd_hit_wt_$(($i)).l
done

