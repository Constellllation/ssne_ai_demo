/*
 * Revert package: demo_face.cpp
 * 回退到不显示文字，仅保留二维码框和 QR_OK 位图测试
 *
 * JQ8400 binary slow-send patch:
 *   基于键盘监听可用版本；UART_TX0 恢复发送真正二进制命令。
 *   为验证稳定性，默认逐字节发送，并在命令之间加入 200ms 间隔。
 */

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>
#include <cmath>

#include "include/common.hpp"
#include "include/utils.hpp"
#include "include/qr_decoder.hpp"
#include "include/jq8400_uart.hpp"


using namespace std;

// 保持昨天成功版本的行为：启动不自动播，不随 QR 触发；键盘输入才发命令。
static const bool kEnableJq8400StartupTest = false;
static const bool kEnableJq8400PlayOnNewQr = false;
std::mutex mtx_image;
std::condition_variable cv_image_ready;
std::atomic<bool> stop_inference(false);

std::mutex g_param_mtx;

struct BoxTransformParams {
    float kx = 0.50f;
    float ky = 1.00f;
    float dx = -160.0f;
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

struct SmoothedQuad {
    bool valid = false;
    std::array<std::array<float, 2>, 4> corners{};
};

SmoothedQuad g_right_smooth_quad;
VISUALIZER* g_visualizer = nullptr;

struct ImagePair {
    ssne_tensor_t img1;
    ssne_tensor_t img2;
    int frame_id;
};

std::queue<ImagePair> image_queue;
const int MAX_QUEUE_SIZE = 1;

bool g_exit_flag = false;
std::mutex g_mtx;

void keyboard_listener() {
    std::string input;
    std::cout << "键盘监听线程已启动。\n"
              << "语音测试命令：\n"
              << "  1/2/3/... : 播放对应序号音频\n"
              << "  n/next    : 下一首\n"
              << "  b/prev    : 上一首\n"
              << "  v30       : 音量30\n"
              << "  v20       : 音量20\n"
              << "  auto      : 连续发送 flash/v20/1/2/3 二进制命令\n"
              << "  h         : 打印语音命令帮助\n"
              << "原二维码框调参命令：\n"
              << "  a/d: DX -/+ 1\n"
              << "  j/l: KX -/+ 0.01\n"
              << "  w/s: DY -/+ 1\n"
              << "  i/k: KY -/+ 0.01\n"
              << "  p: 打印当前参数\n"
              << "  q: 退出\n";

    while (true) {
        std::cin >> input;

        if (input == "q" || input == "Q") {
            std::lock_guard<std::mutex> lock(g_mtx);
            g_exit_flag = true;
            std::cout << "检测到退出指令，通知主线程退出..." << std::endl;
            break;
        }

        // 优先处理语音模块命令：输入 1 回车就发送 AA 07 02 00 01 B4。
        if (TryHandleJqKeyboardCommand(input)) {
            continue;
        }

        bool changed = false;
        {
            std::lock_guard<std::mutex> lock(g_param_mtx);

            if (input == "a") { g_box_params.dx -= 1.0f; changed = true; }
            else if (input == "d") { g_box_params.dx += 1.0f; changed = true; }
            else if (input == "j") { g_box_params.kx -= 0.01f; changed = true; }
            else if (input == "l") { g_box_params.kx += 0.01f; changed = true; }
            else if (input == "w") { g_box_params.dy -= 1.0f; changed = true; }
            else if (input == "s") { g_box_params.dy += 1.0f; changed = true; }
            else if (input == "i") { g_box_params.ky -= 0.01f; changed = true; }
            else if (input == "k") { g_box_params.ky += 0.01f; changed = true; }
            else if (input == "p") {}
            else {
                std::cout << "无效输入。语音: 1/2/3/n/next/b/prev/v30/v20/h；调参: a d j l w s i k p q" << std::endl;
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

struct GrayView {
    const uint8_t* data = nullptr;
    int width = 0;
    int height = 0;
    int stride = 0;
};

static bool TensorToGrayView(const ssne_tensor_t &t, GrayView *out) {
    if (!out) return false;
    if (get_data_format(t) != SSNE_Y_8) return false;

    out->data = reinterpret_cast<const uint8_t *>(get_data(t));
    out->width = static_cast<int>(get_width(t));
    out->height = static_cast<int>(get_height(t));
    out->stride = out->width;

    return (out->data && out->width > 0 && out->height > 0);
}

struct GrayBuffer {
    std::vector<uint8_t> data;
    int width = 0;
    int height = 0;
    int stride = 0;
};

static bool CropAndDownsampleGray(const GrayView& src,
                                  int roi_x,
                                  int roi_y,
                                  int roi_w,
                                  int roi_h,
                                  int dst_w,
                                  int dst_h,
                                  GrayBuffer* out) {
    if (!out) return false;
    if (!src.data || src.width <= 0 || src.height <= 0 || src.stride < src.width) return false;
    if (roi_x < 0 || roi_y < 0 || roi_w <= 0 || roi_h <= 0) return false;
    if (roi_x + roi_w > src.width || roi_y + roi_h > src.height) return false;
    if (dst_w <= 0 || dst_h <= 0) return false;

    out->width = dst_w;
    out->height = dst_h;
    out->stride = dst_w;
    out->data.resize(static_cast<size_t>(dst_w) * static_cast<size_t>(dst_h));

    for (int dy = 0; dy < dst_h; ++dy) {
        int sy = roi_y + (dy * roi_h) / dst_h;
        const uint8_t* src_row = src.data + static_cast<size_t>(sy) * static_cast<size_t>(src.stride);
        uint8_t* dst_row = out->data.data() + static_cast<size_t>(dy) * static_cast<size_t>(dst_w);

        for (int dx = 0; dx < dst_w; ++dx) {
            int sx = roi_x + (dx * roi_w) / dst_w;
            dst_row[dx] = src_row[sx];
        }
    }

    return true;
}

static void RemapQrResultsFromRoi(std::vector<QrDecodeResult>* results,
                                  int roi_x,
                                  int roi_y,
                                  int roi_w,
                                  int roi_h,
                                  int dec_w,
                                  int dec_h) {
    if (!results) return;
    if (roi_w <= 0 || roi_h <= 0 || dec_w <= 0 || dec_h <= 0) return;

    const float sx = static_cast<float>(roi_w) / static_cast<float>(dec_w);
    const float sy = static_cast<float>(roi_h) / static_cast<float>(dec_h);

    for (auto& r : *results) {
        if (!r.valid_polygon) continue;
        for (int i = 0; i < 4; ++i) {
            r.corners[i][0] = roi_x + r.corners[i][0] * sx;
            r.corners[i][1] = roi_y + r.corners[i][1] * sy;
        }
    }
}

static void PushQrQuads(const std::vector<QrDecodeResult>& qr_results,
                        int y_offset,
                        std::vector<QrQuad>* out_quads) {
    if (!out_quads) return;

    BoxTransformParams params = GetBoxParams();

    const float cx = 320.0f;
    const float cy = 240.0f;

    for (const auto& r : qr_results) {
        if (r.sym_type != static_cast<int>(zbar::ZBAR_QRCODE)) continue;
        if (!r.valid_polygon) continue;

        QrQuad q;
        for (int i = 0; i < 4; ++i) {
            float x = r.corners[i][0];
            float y = r.corners[i][1];

            x = cx + params.kx * (x - cx) + params.dx;
            y = cy + params.ky * (y - cy) + params.dy;

            q.corners[i][0] = x;
            q.corners[i][1] = y + y_offset;
        }
        out_quads->push_back(q);
    }
}

static void SmoothQuad(const QrQuad& input, SmoothedQuad* state, float alpha) {
    if (!state) return;

    if (!state->valid) {
        state->corners = input.corners;
        state->valid = true;
        return;
    }

    for (int i = 0; i < 4; ++i) {
        state->corners[i][0] =
            alpha * input.corners[i][0] + (1.0f - alpha) * state->corners[i][0];
        state->corners[i][1] =
            alpha * input.corners[i][1] + (1.0f - alpha) * state->corners[i][1];
    }
}

static bool IsQuadAlmostSame(const QrQuad& a, const SmoothedQuad& b, float thres) {
    if (!b.valid) return false;

    const float th2 = thres * thres;
    for (int i = 0; i < 4; ++i) {
        float dx = a.corners[i][0] - b.corners[i][0];
        float dy = a.corners[i][1] - b.corners[i][1];
        if ((dx * dx + dy * dy) > th2) return false;
    }
    return true;
}

void inference_thread_func(int img_height) {
    (void)img_height;
    cout << "[Thread] QR inference thread started!" << endl;

    QrDecoder decoder;
    std::vector<QrDecodeResult> qr_results;
    std::vector<QrQuad> local_quads;
    std::string last_right_payload;

    while (!stop_inference) {
        ImagePair img_pair;
        bool has_image = false;

        {
            std::unique_lock<std::mutex> lock(mtx_image);
            cv_image_ready.wait(lock, [] {
                return !image_queue.empty() || stop_inference.load();
            });

            if (stop_inference && image_queue.empty()) break;

            if (!image_queue.empty()) {
                img_pair = image_queue.front();
                image_queue.pop();
                has_image = true;
            }
        }

        if (!has_image) continue;

        local_quads.clear();

        GrayView gv1;
        if (TensorToGrayView(img_pair.img1, &gv1)) {
            const int roi_x = gv1.width / 8;
            const int roi_y = gv1.height / 8;
            const int roi_w = gv1.width * 3 / 4;
            const int roi_h = gv1.height * 3 / 4;

            const int dec_w = 320;
            const int dec_h = 240;

            GrayBuffer dec_buf;
            bool prep_ok = CropAndDownsampleGray(
                gv1, roi_x, roi_y, roi_w, roi_h, dec_w, dec_h, &dec_buf);

            if (prep_ok) {
                bool ok = decoder.DecodeY800(
                    dec_buf.data.data(),
                    dec_buf.width,
                    dec_buf.height,
                    dec_buf.stride,
                    &qr_results
                );

                if (ok && !qr_results.empty()) {
                    RemapQrResultsFromRoi(&qr_results, roi_x, roi_y, roi_w, roi_h, dec_w, dec_h);

                    std::string current_payload = qr_results[0].data;
                    if (current_payload != last_right_payload) {
                        for (size_t i = 0; i < qr_results.size(); ++i) {
                            printf("[Frame %d] Right QR (%s): %s\n",
                                   img_pair.frame_id,
                                   qr_results[i].type.c_str(),
                                   qr_results[i].data.c_str());
                        }
                        last_right_payload = current_payload;
                        if (kEnableJq8400PlayOnNewQr && Jq8400IsReady()) {
                            JqPlayTrack(1);
                        }
                    }

                    PushQrQuads(qr_results, 0, &local_quads);
                }
            }
        }

        if (g_visualizer != nullptr) {
            if (!local_quads.empty()) {
                QrQuad current = local_quads[0];

                if (!IsQuadAlmostSame(current, g_right_smooth_quad, 2.0f)) {
                    SmoothQuad(current, &g_right_smooth_quad, 0.75f);
                }

                std::vector<QrQuad> draw_quads;
                QrQuad draw_quad;
                draw_quad.corners = g_right_smooth_quad.corners;
                draw_quads.push_back(draw_quad);

                g_visualizer->DrawQuads(draw_quads);
                //g_visualizer->DrawTextureItems(
                   // BuildPayloadTextureItems(last_right_payload, draw_quad));
            } else {
                g_right_smooth_quad.valid = false;
                std::vector<QrQuad> empty_quads;
                g_visualizer->DrawQuads(empty_quads);
                //std::vector<sst::device::osd::OsdTextureItem> empty_texts;
                //g_visualizer->DrawTextureItems(empty_texts);
            }
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

    if (InitJq8400Uart() && kEnableJq8400StartupTest) {
        RunJq8400StartupTest();
    }

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

    CloseJq8400Uart();
    visualizer.Release();
    processor.Release();

    if (ssne_release()) {
        fprintf(stderr, "SSNE release failed!\n");
        return -1;
    }

    return 0;
}
