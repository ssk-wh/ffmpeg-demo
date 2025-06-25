# 说明

此项目旨在演示 linux 打包软件所需配置内容

# 编译

打开终端，进入项目源码目录后
mkdir build
cd build
cmake ..
make 

# 运行

编译成功后，build 目录中会产生 recorder 和 detector 两个二进制，直接运行即可。

# 打包

进入源码目录
运行下面的命令后，将在源码的上一级生成 deb 软件包。
dpkg-buildpackage -us -uc -nc -j8