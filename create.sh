#/bin/sh

mkdir /sys/fs/cgroup/blkio/fo1
mkdir /sys/fs/cgroup/blkio/fo2
mkdir /sys/fs/cgroup/blkio/fo3
mkdir /sys/fs/cgroup/blkio/fo4

echo "8:16 10240000" > /sys/fs/cgroup/blkio/fo1/blkio.throttle.rw_bps_device 
echo "8:16 10240000" > /sys/fs/cgroup/blkio/fo2/blkio.throttle.rw_bps_device
echo "8:16 20480000" > /sys/fs/cgroup/blkio/fo3/blkio.throttle.rw_bps_device
echo "8:16 20480000" > /sys/fs/cgroup/blkio/fo4/blkio.throttle.rw_bps_device


echo "8:48 10240000" > /sys/fs/cgroup/blkio/fo1/blkio.throttle.write_bps_device
echo "8:48 10240000" > /sys/fs/cgroup/blkio/fo2/blkio.throttle.write_bps_device
echo "8:48 20480000" > /sys/fs/cgroup/blkio/fo3/blkio.throttle.write_bps_device
echo "8:48 20480000" > /sys/fs/cgroup/blkio/fo4/blkio.throttle.write_bps_device

