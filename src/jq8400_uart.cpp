#include "../include/jq8400_uart.hpp"

#include <cctype>
#include <cstdio>
#include <mutex>
#include <string>
#include <unistd.h>

#include "uart_api.h"

// 与昨天成功版保持一致：启动不自动发 ASCII、不切 Flash；实际发送真正二进制。
static const bool kEnableJq8400AsciiMonitorTest = false;
static const bool kEnableJq8400SwitchToFlashBeforePlay = false;
static const bool kEnableJq8400SlowByteSend = true;
static const useconds_t kJq8400InterByteDelayUs = 1500;
static const useconds_t kJq8400InterCommandDelayUs = 200000;

struct Jq8400Context {
    uart_handle_t uart = NULL;
    bool ready = false;
    std::mutex mtx;
};

static Jq8400Context g_jq8400;

static uint8_t JqChecksum(const uint8_t* data, size_t len_without_checksum) {
    uint32_t sum = 0;
    for (size_t i = 0; i < len_without_checksum; ++i) {
        sum += data[i];
    }
    return static_cast<uint8_t>(sum & 0xFF);
}

static int JqSendCommand(const uint8_t* data, uint32_t len) {
    if (!g_jq8400.ready || g_jq8400.uart == NULL || data == nullptr || len == 0) {
        printf("[JQ8400][ERR] UART not ready or invalid data\n");
        return -1;
    }

    if (len > 32) {
        printf("[JQ8400][ERR] command too long: %u bytes, UART FIFO limit is 32 bytes\n", len);
        return -1;
    }

    std::lock_guard<std::mutex> lock(g_jq8400.mtx);

    printf("[JQ8400][BIN] send:");
    for (uint32_t i = 0; i < len; ++i) {
        printf(" %02X", data[i]);
    }

    int ret = 0;

    if (kEnableJq8400SlowByteSend) {
        // 逐字节送入 TX0。UART 线上仍然是真正二进制字节，不是 ASCII 文本。
        for (uint32_t i = 0; i < len; ++i) {
            ret = uart_send_data(g_jq8400.uart, UART_TX0, &data[i], 1);
            if (ret != 0) {
                printf(" ret=%d failed_at_byte=%u\n", ret, i);
                return ret;
            }
            usleep(kJq8400InterByteDelayUs);
        }
    } else {
        ret = uart_send_data(g_jq8400.uart, UART_TX0, data, len);
        if (ret != 0) {
            printf(" ret=%d\n", ret);
            return ret;
        }
    }

    printf(" ret=0\n");
    usleep(kJq8400InterCommandDelayUs);
    return 0;
}

static int JqSwitchToFlash() {
    uint8_t cmd[5] = {0xAA, 0x0B, 0x01, 0x02, 0x00};
    cmd[4] = JqChecksum(cmd, 4);
    return JqSendCommand(cmd, sizeof(cmd));
}

static int JqSendAsciiForMonitor() {
    static const uint8_t msg[] = "HELLO_JQ8400\r\n";
    printf("[JQ8400] send ASCII monitor text: %s", msg);
    return JqSendCommand(msg, sizeof(msg) - 1);
}

int JqPlayTrack(uint16_t track_index) {
    uint8_t cmd[6] = {
        0xAA, 0x07, 0x02,
        static_cast<uint8_t>((track_index >> 8) & 0xFF),
        static_cast<uint8_t>(track_index & 0xFF),
        0x00
    };
    cmd[5] = JqChecksum(cmd, 5);
    return JqSendCommand(cmd, sizeof(cmd));
}

int JqPlayPrevious() {
    // JQ8400 上一首：AA 05 00 AF
    uint8_t cmd[4] = {0xAA, 0x05, 0x00, 0x00};
    cmd[3] = JqChecksum(cmd, 3);
    return JqSendCommand(cmd, sizeof(cmd));
}

int JqPlayNext() {
    // JQ8400 下一首：AA 06 00 B0
    uint8_t cmd[4] = {0xAA, 0x06, 0x00, 0x00};
    cmd[3] = JqChecksum(cmd, 3);
    return JqSendCommand(cmd, sizeof(cmd));
}

int JqSetVolume(uint8_t volume) {
    if (volume > 30) volume = 30;
    uint8_t cmd[5] = {0xAA, 0x13, 0x01, volume, 0x00};
    cmd[4] = JqChecksum(cmd, 4);
    return JqSendCommand(cmd, sizeof(cmd));
}

static void JqRunAutoSequence() {
    printf("[JQ8400][AUTO] binary sequence: flash -> volume20 -> track1 -> track2 -> track3\n");
    JqSwitchToFlash();
    JqSetVolume(20);
    JqPlayTrack(1);
    JqPlayTrack(2);
    JqPlayTrack(3);
}

bool TryHandleJqKeyboardCommand(const std::string& input) {
    if (input.empty()) return false;

    if (input == "auto") {
        JqRunAutoSequence();
        return true;
    }

    bool all_digit = true;
    for (char c : input) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            all_digit = false;
            break;
        }
    }

    if (all_digit) {
        int track = std::stoi(input);
        if (track <= 0 || track > 65535) {
            printf("[JQ8400][ERR] invalid track index: %d\n", track);
            return true;
        }

        printf("[JQ8400][KEY] play track %d\n", track);
        JqPlayTrack(static_cast<uint16_t>(track));
        return true;
    }

    if (input == "n" || input == "next") {
        printf("[JQ8400][KEY] next track\n");
        JqPlayNext();
        return true;
    }

    if (input == "b" || input == "prev" || input == "previous") {
        printf("[JQ8400][KEY] previous track\n");
        JqPlayPrevious();
        return true;
    }

    if (input == "v30") {
        printf("[JQ8400][KEY] set volume 30\n");
        JqSetVolume(30);
        return true;
    }

    if (input == "v20") {
        printf("[JQ8400][KEY] set volume 20\n");
        JqSetVolume(20);
        return true;
    }

    if (input == "h" || input == "help") {
        printf("\n[JQ8400 keyboard commands]\n");
        printf("  1        play track 1\n");
        printf("  2        play track 2\n");
        printf("  3        play track 3\n");
        printf("  10       play track 10\n");
        printf("  n/next   next track, send AA 06 00 B0\n");
        printf("  b/prev   previous track, send AA 05 00 AF\n");
        printf("  v30      set volume 30\n");
        printf("  v20      set volume 20\n");
        printf("  auto     flash -> volume20 -> play 1 -> play 2 -> play 3\n");
        printf("  q        quit demo\n");
        printf("\n");
        return true;
    }

    return false;
}

bool InitJq8400Uart() {
    g_jq8400.uart = uart_init();
    if (g_jq8400.uart == NULL) {
        printf("[WARN] uart_init failed, QR demo will continue without voice module.\n");
        printf("[WARN] Please ensure UART kernel module is loaded: insmod /lib/modules/$(uname -r)/extra/uart_kmod.ko\n");
        return false;
    }

    int ret = 0;

    ret = uart_set_baudrate(g_jq8400.uart, UART_TX0, 9600);
    if (ret != 0) {
        printf("[ERR] uart_set_baudrate TX0 failed, ret=%d\n", ret);
        uart_close(g_jq8400.uart);
        g_jq8400.uart = NULL;
        return false;
    }

    ret = uart_set_baudrate(g_jq8400.uart, UART_RX0, 9600);
    if (ret != 0) {
        printf("[ERR] uart_set_baudrate RX0 failed, ret=%d\n", ret);
        uart_close(g_jq8400.uart);
        g_jq8400.uart = NULL;
        return false;
    }

    ret = uart_set_parity(g_jq8400.uart, UART_TX0, UART_PARITY_NONE);
    if (ret != 0) {
        printf("[ERR] uart_set_parity TX0 failed, ret=%d\n", ret);
        uart_close(g_jq8400.uart);
        g_jq8400.uart = NULL;
        return false;
    }

    ret = uart_set_parity(g_jq8400.uart, UART_RX0, UART_PARITY_NONE);
    if (ret != 0) {
        printf("[ERR] uart_set_parity RX0 failed, ret=%d\n", ret);
        uart_close(g_jq8400.uart);
        g_jq8400.uart = NULL;
        return false;
    }

    g_jq8400.ready = true;
    printf("[INFO] JQ8400 UART configured: 9600 8N1 (GPIO default mux assumed)\n");
    return true;
}

void CloseJq8400Uart() {
    if (g_jq8400.uart != NULL) {
        uart_close(g_jq8400.uart);
        g_jq8400.uart = NULL;
    }
    g_jq8400.ready = false;
}

bool Jq8400IsReady() {
    return g_jq8400.ready;
}

void RunJq8400StartupTest() {
    if (!g_jq8400.ready) return;

    printf("[INFO] JQ8400 startup test: ASCII monitor + play track 1 only\n");
    printf("[INFO] This version does NOT switch storage first; it matches your TeraTerm macro: AA 07 02 00 01 B4\n");

    // 等语音模块/电平转换模块稳定，也方便你先打开 PC 串口监听窗口。
    usleep(3000 * 1000);

    if (kEnableJq8400AsciiMonitorTest) {
        // 用 CH340 监听 TXS B1 / JQ8400 RX 时，TeraTerm 主窗口应能看到 HELLO_JQ8400。
        JqSendAsciiForMonitor();
        usleep(500 * 1000);
    }

    if (kEnableJq8400SwitchToFlashBeforePlay) {
        JqSwitchToFlash();
        usleep(500 * 1000);
    }

    // 完全复现你在 TeraTerm 里验证成功的播放命令。
    // 连发三次，便于示波器/CH340抓取；模块通常会忽略重复播放或重新触发播放。
    JqPlayTrack(1);
    usleep(500 * 1000);
    JqPlayTrack(1);
    usleep(500 * 1000);
    JqPlayTrack(1);
}
