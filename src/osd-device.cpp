/*
 * @Author: Jingwen Bai
 * @Date: 2024-07-04 11:07:00
 * @Description: osd device
 * @Filename: osd-device.cpp
 */

#include <iostream>
#include <fstream>
#include <cstring>
#include <unistd.h>
#include <cmath>

#include "../include/osd-device.hpp"

using namespace fdevice;

namespace sst {
namespace device {
namespace osd {

OsdDevice::OsdDevice()
    : m_height(0),
      m_width(0) {
}

OsdDevice::~OsdDevice() {
    std::cout << "OsdDevice Destructor" << std::endl;
}

void OsdDevice::Initialize(int width, int height) {
    m_width = width;
    m_height = height;

    if (LoadLutFile(m_osd_lut_path.c_str()) != 0) {
        std::cerr << "[OsdDevice] Failed to load LUT: " << m_osd_lut_path << std::endl;
    }

    m_osd_handle = osd_open_device();
    osd_init_device(m_osd_handle, OSD_LAYER_SIZE, (char*)m_pcolor_lut);

    // 参考“位图示例”思路：只创建少量 GRAPHIC layer，再单独创建一个固定的 bitmap layer。
    int quad_dma_size = 1024;
    for (int layer_index = 0; layer_index < 2; layer_index++) {
        osd_alloc_buffer(m_osd_handle, m_layer_dma[layer_index].dma, quad_dma_size);
        usleep(250000);
        osd_alloc_buffer(m_osd_handle, m_layer_dma[layer_index].dma_2, quad_dma_size);

        int dma_fd = osd_get_buffer_fd(m_osd_handle, m_layer_dma[layer_index].dma);

        LAYER_ATTR_S osd_layer;
        std::memset(&osd_layer, 0, sizeof(osd_layer));
        osd_layer.codeTYPE = SS_TYPE_QUADRANGLE;
        osd_layer.layer_data_QR.osd_buf.buf_type = BUFFER_TYPE_DMABUF;
        osd_layer.layer_data_QR.osd_buf.buf.fd_dmabuf = dma_fd;
        osd_layer.layerStart.layer_start_x = 0;
        osd_layer.layerStart.layer_start_y = 0;
        osd_layer.layerSize.layer_width = m_width;
        osd_layer.layerSize.layer_height = m_height;
        osd_layer.layer_rgn = {TYPE_GRAPHIC, {m_width, m_height}};

        osd_create_layer(m_osd_handle, (ssLAYER_HANDLE)layer_index, &osd_layer);
        osd_set_layer_buffer(m_osd_handle, (ssLAYER_HANDLE)layer_index, m_layer_dma[layer_index]);
    }

    // 固定位图层 layer 2
    {
        int layer_index = 2;
        int texture_dma_size = 0x20000;
        osd_alloc_buffer(m_osd_handle, m_layer_dma[layer_index].dma, texture_dma_size);
        usleep(250000);
        osd_alloc_buffer(m_osd_handle, m_layer_dma[layer_index].dma_2, texture_dma_size);
        int dma_fd = osd_get_buffer_fd(m_osd_handle, m_layer_dma[layer_index].dma);

        LAYER_ATTR_S osd_layer;
        std::memset(&osd_layer, 0, sizeof(osd_layer));
        osd_layer.codeTYPE = SS_TYPE_RLE;
        osd_layer.layer_data_RLE.osd_buf.buf_type = BUFFER_TYPE_DMABUF;
        osd_layer.layer_data_RLE.osd_buf.buf.fd_dmabuf = dma_fd;
        osd_layer.layerStart.layer_start_x = 0;
        osd_layer.layerStart.layer_start_y = 0;
        osd_layer.layerSize.layer_width = m_width;
        osd_layer.layerSize.layer_height = m_height;
        osd_layer.layer_rgn = {TYPE_IMAGE, {m_width, m_height}};

        int ret = osd_create_layer(m_osd_handle, (ssLAYER_HANDLE)layer_index, &osd_layer);
        std::cout << "[OsdDevice] create bitmap layer ret=" << ret << std::endl;
        ret = osd_set_layer_buffer(m_osd_handle, (ssLAYER_HANDLE)layer_index, m_layer_dma[layer_index]);
        std::cout << "[OsdDevice] set bitmap layer buffer ret=" << ret << std::endl;
    }

    // 关闭位图交替显示测试，避免 layer 2 一直刷 char_Q/char_R 影响语音响应速度测试。
    // StartTextureTestThread();
}

void OsdDevice::Release() {
    std::cout << "OsdDevice Release" << std::endl;

    for (int i = 0; i < OSD_LAYER_SIZE; i++) {
        osd_destroy_layer(m_osd_handle, (ssLAYER_HANDLE)i);

        if (m_layer_dma[i].dma != nullptr)
            osd_delete_buffer(m_osd_handle, m_layer_dma[i].dma);
        if (m_layer_dma[i].dma_2 != nullptr)
            osd_delete_buffer(m_osd_handle, m_layer_dma[i].dma_2);
    }

    if (m_pcolor_lut != nullptr) {
        delete[] m_pcolor_lut;
        m_pcolor_lut = nullptr;
    }

    if (m_osd_handle != 0) {
        osd_close_device(m_osd_handle);
        m_osd_handle = 0;
    }
}

int OsdDevice::LoadLutFile(const char* filename) {
    std::ifstream file(filename, std::ios::binary | std::ios::in | std::ios::ate);
    if (!file) {
        std::cerr << "无法打开文件 " << filename << std::endl;
        return -1;
    }

    m_file_size = static_cast<int>(file.tellg());
    if (m_file_size <= 0) {
        return -1;
    }

    if (m_pcolor_lut != nullptr) {
        delete[] m_pcolor_lut;
        m_pcolor_lut = nullptr;
    }

    m_pcolor_lut = new uint8_t[m_file_size];
    file.seekg(0, std::ios::beg);
    file.read((char*)m_pcolor_lut, m_file_size);
    file.close();

    return 0;
}

void OsdDevice::DrawBitmapLayerTexture(const char* filename, int layer_id, int x, int y,
                                       fdevice::ALPHATYPE alpha) {
    BITMAP_INFO_S bm_info;
    bm_info.pSSbmpFile = filename;
    bm_info.alpha = alpha;
    bm_info.position.x = x;
    bm_info.position.y = y;

    std::cout << "[OSD-TEST] show " << filename << " on fixed bitmap layer" << std::endl;
    int ret = osd_add_texture_layer(m_osd_handle, (ssLAYER_HANDLE)layer_id, &bm_info);
    std::cout << "[OSD] osd_add_texture_layer(" << filename << ") ret=" << ret << std::endl;
    if (ret != 0) return;

    ret = osd_flush_texture_layer(m_osd_handle, (ssLAYER_HANDLE)layer_id);
    std::cout << "[OSD] osd_flush_texture_layer ret=" << ret << std::endl;
}

void* OsdDevice::TextureTestThreadEntry(void* arg) {
    OsdDevice* self = static_cast<OsdDevice*>(arg);
    if (self != nullptr) {
        self->RunTextureLutTests();
    }
    return nullptr;
}

void OsdDevice::StartTextureTestThread() {
    pthread_t tid;
    int ret = pthread_create(&tid, nullptr, OsdDevice::TextureTestThreadEntry, this);
    std::cout << "[OSD-TEST] pthread_create ret=" << ret << std::endl;
    if (ret == 0) {
        pthread_detach(tid);
    }
}

void OsdDevice::RunTextureLutTests() {
    const int layer_id = 2;

    sleep(2);

    // 按“位图示例”的方式：同一个固定 bitmap layer，按帧切换不同已编码位图。
    for (int i = 0; i < 60; ++i) {
        DrawBitmapLayerTexture(m_char_q_path.c_str(), layer_id, 180, 120);
        DrawBitmapLayerTexture(m_char_r_path.c_str(), layer_id, 260, 120);
    }
}

void OsdDevice::Draw(std::vector<OsdQuadRangle> &quad_rangle) {
    if (quad_rangle.size() == 0) {
        osd_clean_all_layer(m_osd_handle);
        return;
    }

    for (auto &q : quad_rangle) {
        GenQrangleBox(q.box, q.border);
        COVER_ATTR_S qrangle_attr = {q.color, q.type, q.alpha, m_qrangle_out, m_qrangle_in};
        osd_add_quad_rangle(m_osd_handle, &qrangle_attr);
    }

    osd_flush_quad_rangle(m_osd_handle);
}

void OsdDevice::Draw(std::vector<OsdQuadRangle> &quad_rangle, int layer_id) {
    if (quad_rangle.size() == 0) {
        osd_clean_layer(m_osd_handle, (ssLAYER_HANDLE)layer_id);
        return;
    }

    for (auto &q : quad_rangle) {
        GenQrangleBox(q.box, q.border);
        COVER_ATTR_S qrangle_attr = {q.color, q.type, q.alpha, m_qrangle_out, m_qrangle_in};
        osd_add_quad_rangle_layer(m_osd_handle, (ssLAYER_HANDLE)layer_id, &qrangle_attr);
    }

    osd_flush_quad_rangle_layer(m_osd_handle, (ssLAYER_HANDLE)layer_id);
}

void OsdDevice::Draw(std::vector<std::array<float, 4>>& boxes,
                     int border,
                     int layer_id,
                     tagQUADRANGLETYPE type,
                     tagALPHATYPE alpha,
                     int color) {
    if (boxes.size() == 0) {
        osd_clean_layer(m_osd_handle, (ssLAYER_HANDLE)layer_id);
        return;
    }

    for (auto &box : boxes) {
        GenQrangleBox(box, border);
        COVER_ATTR_S qrangle_attr = {color, type, alpha, m_qrangle_out, m_qrangle_in};
        osd_add_quad_rangle_layer(m_osd_handle, (ssLAYER_HANDLE)layer_id, &qrangle_attr);
    }

    osd_flush_quad_rangle_layer(m_osd_handle, (ssLAYER_HANDLE)layer_id);
}

void OsdDevice::GenQrangleBox(std::array<float, 4>& det, int border) {
    std::array<int, 16> box;

    box[0]  = std::min(m_width,  std::max(0, int(det[0] + border)));
    box[1]  = std::min(m_height, std::max(0, int(det[1] + border)));
    box[2]  = std::min(m_width,  std::max(0, int(det[0] + border)));
    box[3]  = std::min(m_height, std::max(0, int(det[3] - border)));
    box[4]  = std::min(m_width,  std::max(0, int(det[2] - border)));
    box[5]  = std::min(m_height, std::max(0, int(det[3] - border)));
    box[6]  = std::min(m_width,  std::max(0, int(det[2] - border)));
    box[7]  = std::min(m_height, std::max(0, int(det[1] + border)));

    box[8]  = std::min(m_width,  std::max(0, int(det[0] - border)));
    box[9]  = std::min(m_height, std::max(0, int(det[1] - border)));
    box[10] = std::min(m_width,  std::max(0, int(det[0] - border)));
    box[11] = std::min(m_height, std::max(0, int(det[3] + border)));
    box[12] = std::min(m_width,  std::max(0, int(det[2] + border)));
    box[13] = std::min(m_height, std::max(0, int(det[3] + border)));
    box[14] = std::min(m_width,  std::max(0, int(det[2] + border)));
    box[15] = std::min(m_height, std::max(0, int(det[1] - border)));

    m_qrangle_in.points[0] = {box[0],  box[1]};
    m_qrangle_in.points[1] = {box[2],  box[3]};
    m_qrangle_in.points[2] = {box[4],  box[5]};
    m_qrangle_in.points[3] = {box[6],  box[7]};

    m_qrangle_out.points[0] = {box[8],  box[9]};
    m_qrangle_out.points[1] = {box[10], box[11]};
    m_qrangle_out.points[2] = {box[12], box[13]};
    m_qrangle_out.points[3] = {box[14], box[15]};
}

void OsdDevice::GenQranglePolygon(const std::array<std::array<float, 2>, 4>& pts, int border) {
    float cx = 0.0f, cy = 0.0f;
    for (int i = 0; i < 4; ++i) {
        cx += pts[i][0];
        cy += pts[i][1];
    }
    cx /= 4.0f;
    cy /= 4.0f;

    for (int i = 0; i < 4; ++i) {
        float x = pts[i][0];
        float y = pts[i][1];

        float vx = x - cx;
        float vy = y - cy;
        float norm = std::sqrt(vx * vx + vy * vy);
        if (norm < 1e-3f) norm = 1.0f;

        float ox = x + border * vx / norm;
        float oy = y + border * vy / norm;
        float ix = x - border * vx / norm;
        float iy = y - border * vy / norm;

        ox = std::min((float)m_width,  std::max(0.0f, ox));
        oy = std::min((float)m_height, std::max(0.0f, oy));
        ix = std::min((float)m_width,  std::max(0.0f, ix));
        iy = std::min((float)m_height, std::max(0.0f, iy));

        m_qrangle_out.points[i] = {(int)ox, (int)oy};
        m_qrangle_in.points[i]  = {(int)ix, (int)iy};
    }
}

void OsdDevice::DrawQuads(const std::vector<std::array<std::array<float, 2>, 4>>& quads,
                          int border,
                          int layer_id,
                          tagQUADRANGLETYPE type,
                          tagALPHATYPE alpha,
                          int color) {
    if (quads.empty()) {
        osd_clean_layer(m_osd_handle, (ssLAYER_HANDLE)layer_id);
        return;
    }

    for (const auto& quad : quads) {
        GenQranglePolygon(quad, border);
        COVER_ATTR_S qrangle_attr = {color, type, alpha, m_qrangle_out, m_qrangle_in};
        osd_add_quad_rangle_layer(m_osd_handle, (ssLAYER_HANDLE)layer_id, &qrangle_attr);
    }

    osd_flush_quad_rangle_layer(m_osd_handle, (ssLAYER_HANDLE)layer_id);
}

} // namespace osd
} // namespace device
} // namespace sst
