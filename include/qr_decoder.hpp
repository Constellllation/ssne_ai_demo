#ifndef SSNE_AI_DEMO_QR_DECODER_HPP
#define SSNE_AI_DEMO_QR_DECODER_HPP

#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include "zbar.h"

struct QrDecodeResult {
    std::string type;
    std::string data;

    // 四个顶点，按 zbar 返回顺序保存
    // corners[i] = {x, y}
    std::array<std::array<float, 2>, 4> corners{};

    // zbar 实际给了多少个点
    int num_corners = 0;

    // 是否有可用多边形
    bool valid_polygon = false;
};

class QrDecoder {
public:
    QrDecoder();
    ~QrDecoder();

    QrDecoder(const QrDecoder&) = delete;
    QrDecoder& operator=(const QrDecoder&) = delete;

    bool DecodeY800(const uint8_t* gray,
                    int width,
                    int height,
                    int stride,
                    std::vector<QrDecodeResult>* results);

private:
    zbar::zbar_image_scanner_t* scanner_;
    std::vector<uint8_t> scratch_;
};

#endif