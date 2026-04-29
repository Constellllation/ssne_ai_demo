#pragma once

#include <cstdint>
#include <string>

// JQ8400 UART 控制模块
// 说明：本模块保持“昨天成功版”的发送策略：
//   1) UART_TX0 发送真正二进制命令，不发送 ASCII 调试文本；
//   2) 默认逐字节发送；
//   3) 字节间隔 1500us，命令间隔 200ms；
//   4) 启动不自动播放，键盘输入才发命令。

bool InitJq8400Uart();
void CloseJq8400Uart();
bool Jq8400IsReady();

void RunJq8400StartupTest();
bool TryHandleJqKeyboardCommand(const std::string& input);

int JqPlayTrack(uint16_t track_index);
int JqPlayPrevious();
int JqPlayNext();
int JqSetVolume(uint8_t volume);
