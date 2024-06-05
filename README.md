# sylar-from-suycx

直接参考：

[C++高性能分布式服务器框架](https://github.com/sylar-yin/sylar)

[从零开始重写sylar C++高性能分布式服务器框架](https://github.com/zhongluqiang/sylar-from-scratch)

## 概述

[整体介绍](docs/README.md)

[协程模块介绍](docs/suycx/README.md)

## 编译&&使用

### 编译

依赖：`yaml-cpp` `protobuf`

```shell
# 源码安装，最好放到一个之后可以随时查看的地方
git clone https://github.com/jbeder/yaml-cpp.git
cd cd yaml-cpp
mkdir build
cd build
cmake ..
make -j4
make install
```

[protobuf安装](https://github.com/SuycxZMZ/KVstorageBaseRaft-cpp)

编译本项目：

```shell
cd build
rm -rf *
cmake ..
make -j4
# 正常情况下在 lib 文件夹下已经生成了库文件
# 在 bin 文件夹下已经生成了 tests 文件夹下测试代码的可执行文件
```

### 使用

头文件放到可以搜索到的地方,编译的时候链接库文件

## TODO

RPC模块
