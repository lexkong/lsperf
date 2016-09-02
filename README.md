# lsperf

# 安装

```
tar -xvzf lsperf.tar.gz
cd lsperf
make
```

# 功能

+ 设置Block Size

+ 设置 O\_DIRECT, O\_SYNC, O_TRUNC

+ 顺序同步IO读，顺序同步IO写，顺序异步IO读，顺序异步IO写，随机同步IO读，随机同步IO写，随机异步IO读，随机异步IO写

+ 测试CPU利用率

+ 测试带宽(MBPS), IOPS

+ 设置队列深度(iodepth, only for aio engine)

+ 多线程

+ 可以利用fsync call 再测试最后将数据刷新至硬盘

+ 清除 OS cache

# 使用方法

## 顺序同步IO写 (good)

```
./lsperf -w -f /test/test -b 8k -v -s 1G -F
```

## 顺序同步IO读 (good)

```
./lsperf -r -f /test/test -b 8k -v -s 1G -C
```

## 顺序异步IO写 (good)

```
./lsperf -w -f /test/test -b 8k -v -s 1G -A -i 32
```

## 顺序异步IO读 (good)

```
./lsperf -r -f /test/test -b 8k -v -s 1G -C -A -i 32
```
## 随机同步IO写 (数据测试跟fio差别有点大, lsperf - fio = 200M)

```
./lsperf -w -f /test/test -b 8k -v -s 1G -F -E
```

## 随机同步IO读 (数据测试跟fio差别有点大, lsperf - fio = 50M)

```
./lsperf -r -f /test/test -b 8k -v -s 1G -C -E
```


## 随机异步IO写 (good)

```
./lsperf -w -f /test/test -b 8k -v -s 1G -E -A -i 32
```

## 随机异步IO读 (good)

```
./lsperf -r -f /test/test -b 8k -v -s 1G -C -E -A -i 32
```
