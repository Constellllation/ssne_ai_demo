#ifndef SSNE_AI_DEMO_QR_DECODER_HPP
#define SSNE_AI_DEMO_QR_DECODER_HPP

#include <cstdint>
#include <string>
#include <vector>

#include <zbar.h>

struct QrDecodeResult {
    std::string type;
    std::string data;
    int x1 = 0;
    int y1 = 0;
    int x2 = 0;
    int y2 = 0;
};

class QrDecoder {
  public:
    QrDecoder();
    ~QrDecoder();

    QrDecoder(const QrDecoder &) = delete;
    QrDecoder &operator=(const QrDecoder &) = delete;

    // 输入: 灰度Y8图像
    // gray: 图像起始地址
    // width/height: 图像尺寸
    // stride: 每行字节数(>=width)
    // results: 输出解码结果
    // 返回: true=扫描流程成功执行(即使未识别到码); false=参数或内部错误
    bool DecodeY800(const uint8_t *gray, int width, int height, int stride,
                    std::vector<QrDecodeResult> *results);

  private:
    zbar::zbar_image_scanner_t *scanner_;
    std::vector<uint8_t> scratch_; // 复用缓存，避免每帧重复分配
};

#endif // SSNE_AI_DEMO_QR_DECODER_HPP
