#include "../include/qr_decoder.hpp"

#include <algorithm>
#include <cstring>

QrDecoder::QrDecoder()
    : scanner_(zbar::zbar_image_scanner_create()) {
    if (scanner_) {
        zbar::zbar_image_scanner_set_config(
            scanner_, zbar::ZBAR_NONE, zbar::ZBAR_CFG_ENABLE, 0);
        zbar::zbar_image_scanner_set_config(
            scanner_, zbar::ZBAR_QRCODE, zbar::ZBAR_CFG_ENABLE, 1);
    }
}

QrDecoder::~QrDecoder() {
    if (scanner_) {
        zbar::zbar_image_scanner_destroy(scanner_);
        scanner_ = nullptr;
    }
}

bool QrDecoder::DecodeY800(const uint8_t* gray,
                           int width,
                           int height,
                           int stride,
                           std::vector<QrDecodeResult>* results) {
    if (results) {
        results->clear();
    }

    if (!scanner_ || !gray || !results || width <= 0 || height <= 0 || stride < width) {
        return false;
    }

    const size_t need = static_cast<size_t>(width) * static_cast<size_t>(height);
    if (scratch_.size() < need) {
        scratch_.resize(need);
    }

    if (stride == width) {
        std::memcpy(scratch_.data(), gray, need);
    } else {
        for (int y = 0; y < height; ++y) {
            std::memcpy(
                scratch_.data() + static_cast<size_t>(y) * static_cast<size_t>(width),
                gray + static_cast<size_t>(y) * static_cast<size_t>(stride),
                static_cast<size_t>(width));
        }
    }

    zbar::zbar_image_t* image = zbar::zbar_image_create();
    if (!image) {
        return false;
    }

    zbar::zbar_image_set_format(image, zbar_fourcc('Y', '8', '0', '0'));
    zbar::zbar_image_set_size(
        image,
        static_cast<unsigned int>(width),
        static_cast<unsigned int>(height));
    zbar::zbar_image_set_data(image, scratch_.data(), need, nullptr);

    const int n = zbar::zbar_scan_image(scanner_, image);

    if (n > 0) {
        const zbar::zbar_symbol_t* symbol = zbar::zbar_image_first_symbol(image);
        for (; symbol; symbol = zbar::zbar_symbol_next(symbol)) {
            QrDecodeResult out;

            const zbar::zbar_symbol_type_t sym_type =
                zbar::zbar_symbol_get_type(symbol);
            out.type = zbar::zbar_get_symbol_name(sym_type);

            const char* data = zbar::zbar_symbol_get_data(symbol);
            out.data = data ? data : "";

            const unsigned int nloc = zbar::zbar_symbol_get_loc_size(symbol);
            out.num_corners = static_cast<int>(std::min<unsigned int>(nloc, 4));

            if (nloc >= 4) {
                for (int i = 0; i < 4; ++i) {
                    float x = static_cast<float>(zbar::zbar_symbol_get_loc_x(symbol, i));
                    float y = static_cast<float>(zbar::zbar_symbol_get_loc_y(symbol, i));

                    x = std::max(0.0f, std::min(x, static_cast<float>(width - 1)));
                    y = std::max(0.0f, std::min(y, static_cast<float>(height - 1)));

                    out.corners[i] = {x, y};
                }
                out.valid_polygon = true;
            }

            results->push_back(out);
        }
    }

    zbar::zbar_image_destroy(image);
    return (n >= 0);
}
