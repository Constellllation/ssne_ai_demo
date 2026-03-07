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

#include "include/utils.hpp"
#include "include/qr_decoder.hpp"

using namespace std;

// 全局变量和同步对象
std::mutex mtx_image;
std::condition_variable cv_image_ready;
std::atomic<bool> stop_inference(false);
std::atomic<int> frame_count(0);

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
    if (!out) return false;

    // TODO: 按你SDK实际字段名修改以下4行
    // 例如可能是: t.vir_addr / t.width / t.height / t.stride
    out->data = reinterpret_cast<const uint8_t *>(t.vir_addr);
    out->width = t.width;
    out->height = t.height;
    out->stride = t.stride;

    if (!out->data || out->width <= 0 || out->height <= 0 || out->stride < out->width)
        return false;
    return true;
}
// ============================================================

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
            if (ok && !qr_results.empty()) {
                for (size_t i = 0; i < qr_results.size(); ++i) {
                    printf("[Frame %d] Right QR (%s): %s\n",
                           img_pair.frame_id,
                           qr_results[i].type.c_str(),
                           qr_results[i].data.c_str());
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

        if (load_flag == 0) {
            copy_double_tensor_buffer(img_sensor[0], img_sensor[1], output_sensor[0]);
        } else {
            copy_double_tensor_buffer(img_sensor[0], img_sensor[1], output_sensor[1]);
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