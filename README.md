# sylar-from-suycx

直接参考：

[C++高性能分布式服务器框架](https://github.com/sylar-yin/sylar)

[从零开始重写sylar C++高性能分布式服务器框架](https://github.com/zhongluqiang/sylar-from-scratch)

## 概述

[整体介绍](docs/README.md)

[协程模块介绍](docs/suycx/README.md)

## 编译&&使用

### 编译

依赖：`yaml-cpp` `protobuf` `zookeeper`

```shell
# 装一些工具
sudo apt-get update
sudo apt-get install -y build-essential autoconf automake libtool curl git unzip

# 源码安装，最好放到一个之后可以随时查看的地方
git clone https://github.com/jbeder/yaml-cpp.git
cd yaml-cpp
mkdir build
cd build
cmake ..
make -j4
make install

# protobuf安装 本项目的根目录下的tools-packages里放的有3.12.4源码包，最好放到一个之后可以随时查看的地方
sudo unzip protobuf-3.12.4.zip
cd protobuf-3.12.4
./autogen.sh
./configure
make -j4
sudo make install
sudo ldconfig
# 检查安装是否成功
protoc --version

# zookeeper编译安装c开发包
tar -zxvf zookeeper-3.4.10.tar.gz
cd zookeeper-3.4.10
cd src/c
./configure
# 修改一下makefile
sudo vim Makefile
# 这一行注释掉AM_CFLAGS = -Wall -Werror，保存退出
make -j4
sudo make install 
```

编译本项目：

```shell
git clone https://github.com/SuycxZMZ/sylar-from-suycx.git
cd sylar-from-suycx
mkdir build
cd build
cmake ..
make -j4
# 头文件会安装到 /usr/local/include/sylar 文件夹
# 编译生成的动态库安装到 /usr/local/lib 文件夹
sudo make install 
```

### 使用

头文件引入示例：

基本组件头文件引入 #include "sylar/..."

附加应用组件 #include "sylar/rpc/..."

编译的时候链接库文件

## TODO

RPC模块，目前只做完了最简单的mprpc复现，配置读取，支持长连接等待优化。
