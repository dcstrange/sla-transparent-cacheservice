#/bin/sh

cgexec -g "blkio:fo3" ./smr-ssd-cache 100000 25000 7 /tmp/fifo_user2_r /tmp/fifo_user2_w 2 > /tmp/test7_user2_equal.txt &
cgexec -g "blkio:fo4" ./smr-ssd-cache 100000 25000 2 /tmp/fifo_user3_r /tmp/fifo_user3_w 3 > /tmp/test7_user3_equal.txt &
cgexec -g "blkio:fo1" ./smr-ssd-cache 100000 25000 5 /tmp/fifo_user0_r /tmp/fifo_user0_w 0 > /tmp/test7_user0_equal.txt &
cgexec -g "blkio:fo2" ./smr-ssd-cache 100000 25000 6 /tmp/fifo_user1_r /tmp/fifo_user1_w 1 > /tmp/test7_user1_equal.txt &

./throttler > /tmp/test7_throttler.txt &
thro_pid=$!

while true
do
        ps -a | grep "smr"
        if [ $? -eq 1 ]
        then
            kill thro_pid
        else
            sleep 10s
        fi
done

