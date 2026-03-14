/*
 * @Filename: demo_face.cpp (QR version)
 * @Author: Your Name
 * @Date: 2026-xx-xx
 */

#include <fstream>
#include <iostream>
#include <cstring>
#include <fcntl.h>
#include <regex>
#include <dirent.h>
#include <unistd.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <queue>
#include <vector>
#include <algorithm>
#include <cstdint>

#include "include/utils.hpp"
#include "include/qr_decoder.hpp"

using namespace std;

// 全局变量和同步对象
std::mutex mtx_image;
std::condition_variable cv_image_ready;
std::atomic<bool> stop_inference(false);
std::atomic<int> frame_count(0);

struct QrOverlayState {
    int frame_id = -1;
    std::vector<QrDecodeResult> right_results;
};

// 说明：
// 1) 当前工程默认使用“直接改写Y平面像素”的方式画框，保证在缺少OSD头文件/库时也能运行。
// 2) 若板端SDK提供 osd_lib_api.h，可在此处替换为OSD实现：
//    osd_open_device -> osd_init_device -> osd_alloc_buffer -> osd_create_layer -> osd_set_layer_buffer
//    每帧调用 osd_add_quad_rangle_layer + osd_flush_quad_rangle_layer
//    退出时调用 osd_destroy_layer + osd_delete_buffer。

std::mutex g_qr_mtx;
QrOverlayState g_qr_overlay;

// 图像队列结构
struct ImagePair {
    ssne_tensor_t img1;
    ssne_tensor_t img2;
    int frame_id;
};

std::queue<ImagePair> image_queue;
const int MAX_QUEUE_SIZE = 2;

// 全局退出标志（线程安全）
bool g_exit_flag = false;
std::mutex g_mtx;

void keyboard_listener() {
    std::string input;
    std::cout << "键盘监听线程已启动，输入 'q' 退出程序..." << std::endl;

    while (true) {
        std::cin >> input;
        std::lock_guard<std::mutex> lock(g_mtx);
        if (input == "q" || input == "Q") {
            g_exit_flag = true;
            std::cout << "检测到退出指令，通知主线程退出..." << std::endl;
            break;
        } else {
            std::cout << "输入无效（仅 'q' 有效），请重新输入：" << std::endl;
        }
    }
}

bool check_exit_flag() {
    std::lock_guard<std::mutex> lock(g_mtx);
    return g_exit_flag;
}

// ===================== 需要你按SDK字段适配 =====================
struct GrayView {
    const uint8_t *data = nullptr;
    int width = 0;
    int height = 0;
    int stride = 0;
};

static bool TensorToGrayView(const ssne_tensor_t &t, GrayView *out) {
    if (!out)
        return false;

    // 仅处理 Y8 输入
    if (get_data_format(t) != SSNE_Y_8)
        return false;

    out->data = reinterpret_cast<const uint8_t *>(get_data(t));
    out->width = static_cast<int>(get_width(t));
    out->height = static_cast<int>(get_height(t));

    // 当前这版 ssne_api.h 没有 stride 接口，先按紧凑 Y8 处理
    out->stride = out->width;

    return (out->data && out->width > 0 && out->height > 0);
}
// ============================================================

static inline void DrawPixel(uint8_t *buf, int width, int height, int stride, int x, int y, uint8_t value) {
    if (!buf || x < 0 || y < 0 || x >= width || y >= height) return;
    buf[y * stride + x] = value;
}

static void DrawRect(uint8_t *buf, int width, int height, int stride,
                     int x1, int y1, int x2, int y2, uint8_t value, int thickness = 2) {
    if (!buf) return;
    if (x1 > x2) std::swap(x1, x2);
    if (y1 > y2) std::swap(y1, y2);
    x1 = std::max(0, std::min(x1, width - 1));
    x2 = std::max(0, std::min(x2, width - 1));
    y1 = std::max(0, std::min(y1, height - 1));
    y2 = std::max(0, std::min(y2, height - 1));

    for (int t = 0; t < thickness; ++t) {
        for (int x = x1; x <= x2; ++x) {
            DrawPixel(buf, width, height, stride, x, y1 + t, value);
            DrawPixel(buf, width, height, stride, x, y2 - t, value);
        }
        for (int y = y1; y <= y2; ++y) {
            DrawPixel(buf, width, height, stride, x1 + t, y, value);
            DrawPixel(buf, width, height, stride, x2 - t, y, value);
        }
    }
}

static void DrawQrResultsOnView(ssne_tensor_t *out, const std::vector<QrDecodeResult> &results, int y_offset, int view_h) {
    if (!out)
        return;

    if (get_data_format(*out) != SSNE_Y_8)
        return;

    uint8_t *buf = reinterpret_cast<uint8_t *>(get_data(*out));
    const int width = static_cast<int>(get_width(*out));
    const int height = static_cast<int>(get_height(*out));
    // 当前 SDK 无 stride accessor，按紧凑 Y8 处理
    const int stride = width;

    if (!buf || width <= 0 || height <= 0)
        return;

    if (y_offset < 0 || y_offset >= height)
        return;

    const int draw_h = std::max(0, std::min(view_h, height - y_offset));
    if (draw_h <= 0)
        return;

    // output_sensor 是上下拼接图：上半部分=右路(640x480), 下半部分=左路
    for (const auto &r : results) {
        DrawRect(buf + y_offset * stride, width, draw_h, stride, r.x1, r.y1, r.x2, r.y2, 255, 2);
    }
}

void inference_thread_func() {
    cout << "[Thread] QR inference thread started!" << endl;

    QrDecoder decoder;
    std::vector<QrDecodeResult> qr_results;

    while (!stop_inference) {
        ImagePair img_pair;
        bool has_image = false;

        {
            std::unique_lock<std::mutex> lock(mtx_image);
            cv_image_ready.wait(lock, [] {
                return !image_queue.empty() || stop_inference;
            });

            if (stop_inference && image_queue.empty()) {
                break;
            }

            if (!image_queue.empty()) {
                img_pair = image_queue.front();
                image_queue.pop();
                has_image = true;
            }
        }

        if (!has_image) {
            continue;
        }

        // 先解码右路
        GrayView gv1;
        if (TensorToGrayView(img_pair.img1, &gv1)) {
            bool ok = decoder.DecodeY800(gv1.data, gv1.width, gv1.height, gv1.stride, &qr_results);
            if (ok) {
                std::lock_guard<std::mutex> lock(g_qr_mtx);
                g_qr_overlay.frame_id = img_pair.frame_id;
                g_qr_overlay.right_results = qr_results;
            }
            if (ok && !qr_results.empty()) {
                for (size_t i = 0; i < qr_results.size(); ++i) {
                    printf("[Frame %d] Right QR (%s): %s [box=%d,%d,%d,%d]\n",
                           img_pair.frame_id,
                           qr_results[i].type.c_str(),
                           qr_results[i].data.c_str(),
                           qr_results[i].x1, qr_results[i].y1,
                           qr_results[i].x2, qr_results[i].y2);
                }
            }
        } else {
            // printf("[Frame %d] Right tensor map failed\n", img_pair.frame_id);
        }

        // 如需双路解码，取消注释
        /*
        GrayView gv2;
        if (TensorToGrayView(img_pair.img2, &gv2)) {
            bool ok = decoder.DecodeY800(gv2.data, gv2.width, gv2.height, gv2.stride, &qr_results);
            if (ok && !qr_results.empty()) {
                for (size_t i = 0; i < qr_results.size(); ++i) {
                    printf("[Frame %d] Left QR (%s): %s\n",
                           img_pair.frame_id,
                           qr_results[i].type.c_str(),
                           qr_results[i].data.c_str());
                }
            }
        }
        */
    }

    cout << "[Thread] QR inference thread stopped!" << endl;
}

int main() {
    /******************************************************************************************
     * 1. 参数配置
     ******************************************************************************************/
    uint8_t load_flag = 0;  // 0: 当前load偶帧; 1: 当前load奇帧
    int img_width = 640;
    int img_height = 480;

    /******************************************************************************************
     * 2. 系统初始化
     ******************************************************************************************/
    if (ssne_initial()) {
        fprintf(stderr, "SSNE initialization failed!\n");
    }

    array<int, 2> img_shape = {img_width, img_height};
    IMAGEPROCESSOR processor;
    processor.Initialize(&img_shape);

    cout << "[INFO] QR mode initialized!" << endl;
    cout << "sleep for 0.2 second!" << endl;
    sleep(0.2);

    // 启动推理线程（二维码）
    std::thread inference_thread(inference_thread_func);
    cout << "[INFO] QR inference thread started!" << endl;

    // 键盘监听线程
    std::thread listener_thread(keyboard_listener);

    uint16_t num_frames = 0;
    ssne_tensor_t img_sensor[2];
    ssne_tensor_t output_sensor[2];
    output_sensor[0] = create_tensor(img_width, img_height * 2, SSNE_Y_8, SSNE_BUF_AI);
    output_sensor[1] = create_tensor(img_width, img_height * 2, SSNE_Y_8, SSNE_BUF_AI);

    processor.GetDualImage(&img_sensor[0], &img_sensor[1]);
    copy_double_tensor_buffer(img_sensor[0], img_sensor[1], output_sensor[0]);
    copy_double_tensor_buffer(img_sensor[0], img_sensor[1], output_sensor[1]);

    int res = set_isp_debug_config(output_sensor[0], output_sensor[1]);

    while (num_frames < 2) {
        res = start_isp_debug_load();
        (void)res;
        num_frames++;
    }

    /******************************************************************************************
     * 3. 主处理循环
     ******************************************************************************************/
    while (!check_exit_flag()) {
        processor.GetDualImage(&img_sensor[0], &img_sensor[1]);

        get_even_or_odd_flag(load_flag);

        ssne_tensor_t *current_out = nullptr;
        if (load_flag == 0) {
            copy_double_tensor_buffer(img_sensor[0], img_sensor[1], output_sensor[0]);
            current_out = &output_sensor[0];
        } else {
            copy_double_tensor_buffer(img_sensor[0], img_sensor[1], output_sensor[1]);
            current_out = &output_sensor[1];
        }

        {
            std::lock_guard<std::mutex> lock(g_qr_mtx);
            // 仅叠加最近一帧(或上一帧)的结果，避免推理线程慢时旧框长时间残留
            if (g_qr_overlay.frame_id >= 0 && static_cast<int>(num_frames) - g_qr_overlay.frame_id <= 1) {
                DrawQrResultsOnView(current_out, g_qr_overlay.right_results, 0, img_height);
            }
        }

        res = start_isp_debug_load();
        (void)res;

        {
            std::unique_lock<std::mutex> lock(mtx_image);
            if (image_queue.size() < MAX_QUEUE_SIZE) {
                ImagePair img_pair;
                img_pair.img1 = img_sensor[0];
                img_pair.img2 = img_sensor[1];
                img_pair.frame_id = num_frames;

                image_queue.push(img_pair);
                cv_image_ready.notify_one();
            }
            // 队列满就丢帧，保证主循环实时
        }

        num_frames += 1;
    }

    /******************************************************************************************
     * 4. 停止线程
     ******************************************************************************************/
    cout << "[INFO] Main loop finished, stopping inference thread..." << endl;

    if (listener_thread.joinable()) {
        listener_thread.join();
    }

    {
        std::unique_lock<std::mutex> lock(mtx_image);
        stop_inference = true;
        cv_image_ready.notify_one();
    }

    if (inference_thread.joinable()) {
        inference_thread.join();
        cout << "[INFO] Inference thread joined successfully!" << endl;
    }

    /******************************************************************************************
     * 5. 资源释放
     ******************************************************************************************/
    processor.Release();

    if (ssne_release()) {
        fprintf(stderr, "SSNE release failed!\n");
        return -1;
    }

    return 0;
}