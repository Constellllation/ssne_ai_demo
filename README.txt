本包基于你上传的“昨天最后成功版 demo_face.cpp”重新整理。

核心原则：
1. 保持昨天成功版的 JQ8400 发送策略：
   - UART_TX0 发送真正二进制命令；
   - 默认逐字节发送；
   - 字节间隔 1500us；
   - 命令后延时 200ms；
   - 启动不自动播放、不发 HELLO、不切 Flash。
2. 将 UART/JQ8400 逻辑拆到：
   - include/jq8400_uart.hpp
   - src/jq8400_uart.cpp
3. demo_face.cpp 保留二维码识别/画框主流程，只调用 JQ8400 模块接口。
4. src/osd-device.cpp 关闭位图交替显示测试线程：不再自动 StartTextureTestThread()。
5. CMakeLists.txt / Paths.cmake / scripts/run.sh 已包含 UART 库和 uart_kmod.ko 加载。

覆盖位置：
- demo_face.cpp
- include/jq8400_uart.hpp
- src/jq8400_uart.cpp
- src/osd-device.cpp
- CMakeLists.txt
- cmake_config/Paths.cmake
- scripts/run.sh

键盘命令：
- 1 / 2 / 3 / ... ：播放对应序号音频
- n / next：下一首
- b / prev / previous：上一首
- v30：音量 30
- v20：音量 20
- auto：flash -> volume20 -> play 1 -> play 2 -> play 3
- h / help：帮助
- q：退出
