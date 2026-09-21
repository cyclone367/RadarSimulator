# RadarSimulator

**基于 C++ / Qt 6 / TCP / CMake 实现的桌面雷达模拟器**

模拟雷达目标运动、扫描探测、航迹跟踪与短期位置预测，并使用 Qt Widgets 绘制 PPI（Plan Position Indicator）雷达界面。

项目同时实现了基于 `QTcpServer / QTcpSocket` 的 TCP 通信、自定义文本协议、TCP 数据分包处理，以及独立的协议测试和 TCP 客户端测试。

---

## 项目效果

### Windows

![RadarSimulator Windows](docs/radar_simulator.png)

### WSL2 Ubuntu

![RadarSimulator WSL2](docs/radar_simulator_wsl2.png)

### QEMU Ubuntu

![RadarSimulator QEMU Ubuntu](docs/radar_simulator_qemu.png)

---

## 主要功能

### 雷达模拟

* 多目标运动模拟
* 雷达 360° 扫描
* 可调扫描速度
* 可调波束宽度
* 可调最大探测距离
* 目标距离和方位角实时变化

### 目标探测与航迹跟踪

* 根据雷达波束和探测距离判断目标是否被检测
* 自动建立 `RadarTrack`
* 连续检测结果更新航迹
* `Tracking / Lost` 状态管理
* 目标命中次数统计
* 最近历史航迹保存
* 速度和角速度估计

### 目标预测

根据当前航迹数据：

* 方位角
* 距离
* 估计速度
* 估计角速度

计算目标短期预测位置，并在 PPI 界面显示：

```text
+1 s
+2 s
+3 s
```

---

## PPI 雷达显示

使用 Qt `QWidget + QPainter` 实现自定义雷达显示区域。

界面包含：

* 雷达扫描圆
* 距离环
* N / E / S / W 方向标识
* 当前扫描波束
* 当前目标位置
* 历史航迹
* 预测位置
* `Tracking / Lost` 状态
* 目标编号
* 目标选择
* 航迹信息 Inspector

目标数据采用：

```text
Azimuth + Range
```

表示，并转换为屏幕坐标后进行绘制。

```text
Radar Data
    │
    ▼
极坐标
    │
    ▼
屏幕坐标
    │
    ▼
QPainter
    │
    ▼
PPI Radar
```

---

# 系统架构

项目按照功能职责划分为四个主要模块：

```text
                         ┌──────────────────┐
                         │    MainWindow    │
                         │    界面与控制     │
                         └───────┬──────────┘
                                 │
                    ┌────────────┴────────────┐
                    │                         │
                    ▼                         ▼
           ┌─────────────────┐       ┌─────────────────┐
           │   RadarWidget   │       │   RadarServer   │
           │    PPI绘制      │       │   TCP + 调度    │
           └────────┬────────┘       └────────┬────────┘
                    │                         │
                    │                         ▼
                    │                ┌──────────────────┐
                    └───────────────►│ RadarSimulation  │
                                     │ 目标/扫描/探测/   │
                                     │ 航迹计算          │
                                     └──────────────────┘
```

### MainWindow

负责应用程序界面和用户操作：

* 主窗口
* 参数设置
* Start / Stop
* 航迹表格
* Track Inspector
* 雷达状态显示

### RadarWidget

负责 PPI 雷达界面绘制：

* 雷达背景
* 距离环
* 扫描波束
* 目标
* 历史航迹
* 预测位置
* 目标选择

核心原则：

> **RadarWidget 负责“画”，RadarSimulation 负责“算”。**

### RadarServer

负责网络通信和运行调度：

* `QTcpServer`
* `QTcpSocket`
* TCP 客户端管理
* 命令接收与处理
* 雷达模拟周期调度
* 向客户端发送检测信息

### RadarSimulation

负责核心雷达模拟算法：

* 目标运动
* 雷达扫描
* 目标探测
* 航迹建立
* 航迹更新
* `Tracking / Lost`
* 速度估计
* 角速度估计
* 历史数据
* 短期位置预测

---

# 数据处理流程

雷达模拟数据主要经过以下过程：

```text
RadarTarget
    │
    │ 雷达扫描
    ▼
RadarDetection
    │
    │ 连续检测
    ▼
RadarTrack
    │
    ├── 当前状态
    ├── 速度估计
    ├── 历史数据
    └── 位置预测
    │
    ▼
RadarWidget
    │
    ▼
PPI 雷达界面
```

其中：

* `RadarTarget` 表示模拟目标的真实运动状态
* `RadarDetection` 表示一次雷达检测结果
* `RadarTrack` 表示由多次检测结果形成的目标航迹

---

# TCP 通信

项目使用 Qt Network 模块实现 TCP Server。

核心组件：

```text
QTcpServer
QTcpSocket
```

默认监听端口：

```text
9000
```

客户端可以通过文本命令控制或查询雷达状态。

例如：

```text
GET_STATUS
GET_POSITION
MOVE 200 150
STOP
```

典型通信：

```text
GET_STATUS
        ↓
STATUS OK
```

```text
GET_POSITION
        ↓
POS 125 350
```

```text
MOVE 200 150
        ↓
STATUS MOVED
```

---

# 自定义应用层协议

项目使用基于 `CRLF` 的简单文本协议。

例如：

```text
GET_STATUS\r\n
```

服务器返回：

```text
STATUS OK\r\n
```

协议相关代码位于：

```text
shared/
├── LineBuffer.hpp
└── LineProtocol.hpp
```

---

## TCP 数据分包处理

TCP 是字节流协议，一次 `readyRead()` 不一定对应一条完整消息。

例如：

```text
GET_STATUS\r\n
```

可能被拆成：

```text
GET_STA
```

和：

```text
TUS\r\n
```

因此项目使用 `LineBuffer` 对接收到的数据进行缓存和组包。

同时支持一次接收到多条消息：

```text
GET_STATUS\r\nGET_POSITION\r\n
```

数据处理流程：

```text
QTcpSocket
     │
     ▼
LineBuffer
     │
     ▼
LineProtocol
     │
     ▼
Command
     │
     ▼
RadarServer
```

这样将 TCP 字节流处理和具体业务命令解析进行了分离。

---

# 项目结构

```text
RadarSimulator/
│
├── docs/
│   ├── radar_simulator.png
│   ├── radar_simulator_wsl2.png
│   └── radar_simulator_qemu.png
│
├── shared/
│   ├── LineBuffer.hpp
│   └── LineProtocol.hpp
│
├── tests/
│   ├── protocol_test.cpp
│   └── tcp_client_test.cpp
│
├── CMakeLists.txt
├── .gitignore
├── LICENSE
├── README.md
│
├── main.cpp
│
├── mainwindow.cpp
├── mainwindow.h
├── mainwindow.ui
│
├── RadarServer.cpp
├── RadarServer.h
│
├── RadarSimulation.cpp
├── RadarSimulation.h
│
├── RadarWidget.cpp
└── RadarWidget.h
```

---

# 技术栈

| 技术         | 使用场景       |
| ---------- | ---------- |
| C++17      | 核心程序开发     |
| Qt 6       | 桌面应用开发     |
| Qt Widgets | GUI        |
| QPainter   | PPI 雷达绘制   |
| QTcpServer | TCP 服务端    |
| QTcpSocket | TCP 网络通信   |
| QTimer     | 周期性模拟与界面刷新 |
| CMake      | 项目构建       |
| Git        | 版本控制       |

---

# 编译环境

项目使用 CMake 构建。

主要开发环境：

```text
Windows
Qt 6.x
C++17
CMake 3.19+
MinGW 64-bit
```

项目 CMake 配置使用 Qt：

```text
Core
Widgets
Network
```

---

# 运行环境验证

目前已经完成以下环境的实际运行验证。

### Windows

已验证：

* Qt 6.x
* C++17
* CMake
* MinGW 64-bit
* RadarSimulator GUI 正常运行

### WSL2 Ubuntu

已验证：

* CMake 配置
* C++ 编译
* RadarSimulator GUI 运行

### QEMU + Ubuntu

进一步在 QEMU 虚拟机中的 Ubuntu 环境完成：

* Ubuntu 图形环境运行
* RadarSimulator GUI 启动
* RadarSimulator 正常运行

整体验证路径：

```text
Windows
   │
   ▼
WSL2 Ubuntu
   │
   ▼
QEMU
   │
   ▼
Ubuntu
   │
   ▼
RadarSimulator
```

以上环境均为本项目的实际运行验证结果。

---

# 编译与运行

## 1. 克隆项目

```bash
git clone https://github.com/cyclone367/RadarSimulator.git
cd RadarSimulator
```

## 2. 配置项目

```bash
cmake -S . -B build
```

## 3. 编译

```bash
cmake --build build
```

编译完成后运行生成的 `RadarSimulator` 程序。

> Qt 项目运行时需要对应的 Qt 运行库和平台插件。具体运行方式取决于当前操作系统及 Qt 安装环境。

---

# 测试

项目包含两个独立测试程序。

## protocol_test

用于验证通信协议和数据处理：

* 命令解析
* 参数解析
* CRLF
* 空消息
* 未知命令
* TCP 分包
* 多条消息
* 完整消息处理流程

编译：

```bash
cmake --build build --target protocol_test
```

运行：

```text
protocol_test.exe
```

测试成功：

```text
All tests passed!
```

---

## tcp_client_test

用于测试实际 TCP Server 通信。

首先启动：

```text
RadarSimulator
```

确认 RadarServer 监听：

```text
9000
```

然后编译：

```bash
cmake --build build --target tcp_client_test
```

运行客户端测试。

测试示例：

```text
GET_STATUS
→ STATUS OK

GET_POSITION
→ POS 125 350

MOVE 200 150
→ STATUS MOVED
```

---

# 项目重点

本项目主要实践以下 C++ / Qt 开发内容：

* Qt Widgets 桌面应用开发
* Qt 自定义绘图
* `QPainter` 图形绘制
* `QTcpServer / QTcpSocket` TCP 通信
* TCP 字节流与消息边界处理
* 自定义应用层协议
* C++ 类职责划分
* 定时器驱动的实时模拟
* 目标检测与航迹跟踪
* 运动参数估计
* 历史数据管理
* 短期位置预测
* CMake 多目标项目组织
* 独立测试程序
* Windows / WSL2 / QEMU Ubuntu 多环境运行验证

---

# License

本项目采用 MIT License。

详见 [LICENSE](LICENSE)。

---

# Author

**Junfeng Li**

GitHub：

https://github.com/cyclone367
