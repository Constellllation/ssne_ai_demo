/*
 * @Filename: demo_face.cpp (QR + OSD polygon version)
 */

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>

#include "include/common.hpp"
#include "include/utils.hpp"
#include "include/qr_decoder.hpp"

using namespace std;

// ===================== 全局同步对象 =====================
std::mutex mtx_image;
std::condition_variable cv_image_ready;
std::atomic<bool> stop_inference(false);

std::mutex g_param_mtx;

// 四边形：4 个顶点
struct QrQuad {
    std::array<std::array<float, 2>, 4> corners;
};

struct BoxTransformParams {
    float kx = 1.00f;
    float ky = 1.00f;
    float dx = 0.0f;
    float dy = 0.0f;
};

BoxTransformParams g_box_params;

static BoxTransformParams GetBoxParams() {
    std::lock_guard<std::mutex> lock(g_param_mtx);
    return g_box_params;
}

static void PrintBoxParams() {
    std::lock_guard<std::mutex> lock(g_param_mtx);
    printf("[PARAM] KX=%.3f KY=%.3f DX=%.1f DY=%.1f\n",
           g_box_params.kx, g_box_params.ky, g_box_params.dx, g_box_params.dy);
}

// 全局可视化器指针：推理线程直接画框
VISUALIZER* g_visualizer = nullptr;

// 图像队列结构
struct ImagePair {
    ssne_tensor_t img1;   // 上半屏 / right
    ssne_tensor_t img2;   // 下半屏 / left
    int frame_id;
};

std::queue<ImagePair> image_queue;
const int MAX_QUEUE_SIZE = 2;

// 全局退出标志（线程安全）
bool g_exit_flag = false;
std::mutex g_mtx;

void keyboard_listener() {
    std::string input;
    std::cout << "键盘监听线程已启动。\n"
              << "q:退出\n"
              << "a/d: DX -/+ 1\n"
              << "j/l: KX -/+ 0.01\n"
              << "w/s: DY -/+ 1\n"
              << "i/k: KY -/+ 0.01\n"
              << "p: 打印当前参数\n";

    while (true) {
        std::cin >> input;

        if (input == "q" || input == "Q") {
            std::lock_guard<std::mutex> lock(g_mtx);
            g_exit_flag = true;
            std::cout << "检测到退出指令，通知主线程退出..." << std::endl;
            break;
        }

        bool changed = false;
        {
            std::lock_guard<std::mutex> lock(g_param_mtx);

            if (input == "a") {
                g_box_params.dx -= 1.0f;
                changed = true;
            } else if (input == "d") {
                g_box_params.dx += 1.0f;
                changed = true;
            } else if (input == "j") {
                g_box_params.kx -= 0.01f;
                changed = true;
            } else if (input == "l") {
                g_box_params.kx += 0.01f;
                changed = true;
            } else if (input == "w") {
                g_box_params.dy -= 1.0f;
                changed = true;
            } else if (input == "s") {
                g_box_params.dy += 1.0f;
                changed = true;
            } else if (input == "i") {
                g_box_params.ky -= 0.01f;
                changed = true;
            } else if (input == "k") {
                g_box_params.ky += 0.01f;
                changed = true;
            } else if (input == "p") {
            } else {
                std::cout << "无效输入。可用: a d j l w s i k p q" << std::endl;
            }
        }

        if (input == "p" || changed) {
            PrintBoxParams();
        }
    }
}

bool check_exit_flag() {
    std::lock_guard<std::mutex> lock(g_mtx);
    return g_exit_flag;
}

// ===================== Tensor -> GrayView =====================
struct GrayView {
    const uint8_t* data = nullptr;
    int width = 0;
    int height = 0;
    int stride = 0;
};

static bool TensorToGrayView(const ssne_tensor_t &t, GrayView *out) {
    if (!out) {
        return false;
    }

    if (get_data_format(t) != SSNE_Y_8) {
        return false;
    }

    out->data = reinterpret_cast<const uint8_t *>(get_data(t));
    out->width = static_cast<int>(get_width(t));
    out->height = static_cast<int>(get_height(t));
    out->stride = out->width;

    return (out->data && out->width > 0 && out->height > 0);
}

// 把二维码四个顶点送给 OSD。
// 这里仍然保留 KX/KY/DX/DY，方便你在线调试。
static void PushQrQuads(const std::vector<QrDecodeResult>& qr_results,
                        int y_offset,
                        std::vector<QrQuad>* out_quads) {
    if (!out_quads) return;

    BoxTransformParams params = GetBoxParams();

    const float cx = 320.0f;
    const float cy = 240.0f;

    for (const auto& r : qr_results) {
        if (!r.valid_polygon) continue;

        QrQuad q;
        for (int i = 0; i < 4; ++i) {
            float x = r.corners[i][0];
            float y = r.corners[i][1];

            // 绕中心缩放，再平移
            x = cx + params.kx * (x - cx) + params.dx;
            y = cy + params.ky * (y - cy) + params.dy;

            q.corners[i][0] = x;
            q.corners[i][1] = y + y_offset;
        }
        out_quads->push_back(q);
    }
}

void inference_thread_func(int img_height) {
    cout << "[Thread] QR inference thread started!" << endl;

    QrDecoder decoder;
    std::vector<QrDecodeResult> qr_results;
    std::vector<QrQuad> local_quads;

    std::string last_right_payload;
    std::string last_left_payload;

    while (!stop_inference) {
        ImagePair img_pair;
        bool has_image = false;

        {
            std::unique_lock<std::mutex> lock(mtx_image);
            cv_image_ready.wait(lock, [] {
                return !image_queue.empty() || stop_inference.load();
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

        local_quads.clear();

        // ===================== 右路 / 上半屏 =====================
        GrayView gv1;
        if (TensorToGrayView(img_pair.img1, &gv1)) {
            bool ok = decoder.DecodeY800(
                gv1.data, gv1.width, gv1.height, gv1.stride, &qr_results
            );

            if (ok && !qr_results.empty()) {
                std::string current_payload = qr_results[0].data;
                if (current_payload != last_right_payload) {
                    for (size_t i = 0; i < qr_results.size(); ++i) {
                        printf("[Frame %d] Right QR (%s): %s\n",
                               img_pair.frame_id,
                               qr_results[i].type.c_str(),
                               qr_results[i].data.c_str());

                        if (qr_results[i].valid_polygon) {
                            printf("  P0=(%.1f,%.1f) P1=(%.1f,%.1f) P2=(%.1f,%.1f) P3=(%.1f,%.1f)\n",
                                   qr_results[i].corners[0][0], qr_results[i].corners[0][1],
                                   qr_results[i].corners[1][0], qr_results[i].corners[1][1],
                                   qr_results[i].corners[2][0], qr_results[i].corners[2][1],
                                   qr_results[i].corners[3][0], qr_results[i].corners[3][1]);
                        }
                    }
                    last_right_payload = current_payload;
                }

                PushQrQuads(qr_results, 0, &local_quads);
            }
        }

        // ===================== 左路 / 下半屏 =====================
        GrayView gv2;
        if (TensorToGrayView(img_pair.img2, &gv2)) {
            bool ok = decoder.DecodeY800(
                gv2.data, gv2.width, gv2.height, gv2.stride, &qr_results
            );

            if (ok && !qr_results.empty()) {
                std::string current_payload = qr_results[0].data;
                if (current_payload != last_left_payload) {
                    for (size_t i = 0; i < qr_results.size(); ++i) {
                        printf("[Frame %d] Left QR (%s): %s\n",
                               img_pair.frame_id,
                               qr_results[i].type.c_str(),
                               qr_results[i].data.c_str());

                        if (qr_results[i].valid_polygon) {
                            printf("  P0=(%.1f,%.1f) P1=(%.1f,%.1f) P2=(%.1f,%.1f) P3=(%.1f,%.1f)\n",
                                   qr_results[i].corners[0][0], qr_results[i].corners[0][1],
                                   qr_results[i].corners[1][0], qr_results[i].corners[1][1],
                                   qr_results[i].corners[2][0], qr_results[i].corners[2][1],
                                   qr_results[i].corners[3][0], qr_results[i].corners[3][1]);
                        }
                    }
                    last_left_payload = current_payload;
                }

                PushQrQuads(qr_results, img_height, &local_quads);
            }
        }

        if (g_visualizer != nullptr) {
            g_visualizer->DrawQuads(local_quads);
        }
    }

    cout << "[Thread] QR inference thread stopped!" << endl;
}

int main() {
    uint8_t load_flag = 0;
    int img_width = 640;
    int img_height = 480;

    if (ssne_initial()) {
        fprintf(stderr, "SSNE initialization failed!\n");
        return -1;
    }

    std::array<int, 2> img_shape = {img_width, img_height};
    std::array<int, 2> osd_shape = {img_width, img_height * 2};

    IMAGEPROCESSOR processor;
    processor.Initialize(&img_shape);

    VISUALIZER visualizer;
    visualizer.Initialize(osd_shape);
    g_visualizer = &visualizer;

    cout << "[INFO] QR mode initialized!" << endl;
    PrintBoxParams();
    usleep(200000);

    std::thread inference_thread(inference_thread_func, img_height);
    cout << "[INFO] QR inference thread started!" << endl;

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

            if (image_queue.size() >= MAX_QUEUE_SIZE) {
                image_queue.pop();
            }

            ImagePair img_pair;
            img_pair.img1 = img_sensor[0];
            img_pair.img2 = img_sensor[1];
            img_pair.frame_id = num_frames;
            image_queue.push(img_pair);
            cv_image_ready.notify_one();
        }

        num_frames += 1;
    }

    cout << "[INFO] Main loop finished, stopping inference thread..." << endl;

    {
        std::unique_lock<std::mutex> lock(mtx_image);
        stop_inference = true;
        cv_image_ready.notify_one();
    }

    if (inference_thread.joinable()) {
        inference_thread.join();
        cout << "[INFO] Inference thread joined successfully!" << endl;
    }

    if (listener_thread.joinable()) {
        listener_thread.join();
    }

    visualizer.Release();
    processor.Release();

    if (ssne_release()) {
        fprintf(stderr, "SSNE release failed!\n");
        return -1;
    }

    return 0;
}