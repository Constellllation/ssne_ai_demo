#include "../include/qr_decoder.hpp"

#include <cstring>
#include <algorithm>

QrDecoder::QrDecoder()
    : scanner_(zbar::zbar_image_scanner_create())
{
    // 只开QR，减少CPU负担
    zbar::zbar_image_scanner_set_config(scanner_, zbar::ZBAR_NONE,
                                        zbar::ZBAR_CFG_ENABLE, 0);
    zbar::zbar_image_scanner_set_config(scanner_, zbar::ZBAR_QRCODE,
                                        zbar::ZBAR_CFG_ENABLE, 1);

    // 可选调优（按需打开）：
    // zbar::zbar_image_scanner_set_config(scanner_, zbar::ZBAR_QRCODE, zbar::ZBAR_CFG_X_DENSITY, 1);
    // zbar::zbar_image_scanner_set_config(scanner_, zbar::ZBAR_QRCODE, zbar::ZBAR_CFG_Y_DENSITY, 1);
}

QrDecoder::~QrDecoder()
{
    if (scanner_)
        zbar::zbar_image_scanner_destroy(scanner_);
}

bool QrDecoder::DecodeY800(const uint8_t *gray, int width, int height, int stride,
                           std::vector<QrDecodeResult> *results)
{
    if (results)
        results->clear();

    if (!scanner_ || !gray || !results || width <= 0 || height <= 0 || stride < width)
        return false;

    const size_t need = static_cast<size_t>(width) * static_cast<size_t>(height);
    if (scratch_.size() < need)
        scratch_.resize(need);

    // stride==width 可直接整块拷贝，否则逐行拷贝
    if (stride == width) {
        memcpy(scratch_.data(), gray, need);
    } else {
        for (int y = 0; y < height; ++y) {
            memcpy(scratch_.data() + static_cast<size_t>(y) * width,
                   gray + static_cast<size_t>(y) * stride,
                   static_cast<size_t>(width));
        }
    }

    zbar::zbar_image_t *image = zbar::zbar_image_create();
    if (!image)
        return false;

    zbar::zbar_image_set_format(image, zbar_fourcc('Y', '8', '0', '0'));
    zbar::zbar_image_set_size(image, width, height);

    // 注意: 这里用NULL cleanup，意味着数据生命周期由本类scratch_管理
    zbar::zbar_image_set_data(image, scratch_.data(), need, NULL);

    const int n = zbar::zbar_scan_image(scanner_, image);

    if (n > 0) {
        const zbar::zbar_symbol_t *symbol = zbar::zbar_image_first_symbol(image);
        for (; symbol; symbol = zbar::zbar_symbol_next(symbol)) {
            QrDecodeResult out;
            out.type = zbar::zbar_get_symbol_name(zbar::zbar_symbol_get_type(symbol));

            const char *data = zbar::zbar_symbol_get_data(symbol);
            out.data = data ? data : "";

            const int loc_count = zbar::zbar_symbol_get_loc_size(symbol);
            if (loc_count > 0) {
                int min_x = zbar::zbar_symbol_get_loc_x(symbol, 0);
                int min_y = zbar::zbar_symbol_get_loc_y(symbol, 0);
                int max_x = min_x;
                int max_y = min_y;
                for (int i = 1; i < loc_count; ++i) {
                    const int x = zbar::zbar_symbol_get_loc_x(symbol, i);
                    const int y = zbar::zbar_symbol_get_loc_y(symbol, i);
                    min_x = std::min(min_x, x);
                    min_y = std::min(min_y, y);
                    max_x = std::max(max_x, x);
                    max_y = std::max(max_y, y);
                }
                out.x1 = min_x;
                out.y1 = min_y;
                out.x2 = max_x;
                out.y2 = max_y;
            }

            results->push_back(out);
        }
    }

    zbar::zbar_image_destroy(image);
    return (n >= 0);
}