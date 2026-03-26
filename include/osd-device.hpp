/*
 * @Author: Jingwen Bai
 * @Date: 2024-07-04 11:07:00
 * @Description:
 * @Filename: osd-device.hpp
 */
#ifndef SST_OSD_DEVICE_HPP_
#define SST_OSD_DEVICE_HPP_

#include <array>
#include <vector>
#include <string>
#include "osd_lib_api.h"
#include "common.hpp"

#define BUFFER_TYPE_DMABUF 0x1
#define OSD_LAYER_SIZE 5

namespace sst{
namespace device{
namespace osd{

typedef struct {
    std::array<float, 4> box;
    int border;
    int layer_id;
    fdevice::QUADRANGLETYPE type;
    fdevice::ALPHATYPE alpha;
    int color;
} OsdQuadRangle;

typedef struct {
    std::string path;
    int x;
    int y;
} OsdTextureItem;

class OsdDevice {
public:
    OsdDevice();
    ~OsdDevice();

    void Initialize(int width, int height);
    void Release();

    void Draw(std::vector<OsdQuadRangle> &quad_rangle);
    void Draw(std::vector<std::array<float, 4>>& boxes,
              int border,
              int layer_id,
              fdevice::QUADRANGLETYPE type,
              fdevice::ALPHATYPE alpha,
              int color);
    void Draw(std::vector<OsdQuadRangle> &quad_rangle, int layer_id);

    void DrawQuads(const std::vector<std::array<std::array<float, 2>, 4>>& quads,
                   int border,
                   int layer_id,
                   fdevice::QUADRANGLETYPE type,
                   fdevice::ALPHATYPE alpha,
                   int color);

    // 新增：在 texture 层上按位置贴多张字符位图
    void DrawTextures(const std::vector<OsdTextureItem>& items, int layer_id);

private:
    int LoadLutFile(const char* filename);
    void DrawTexture(const char* filename, int layer_id);
    void GenQrangleBox(std::array<float, 4>& det, int border);
    void GenQranglePolygon(const std::array<std::array<float, 2>, 4>& pts, int border);

private:
    handle_t m_osd_handle;
    // 这里默认用“字符贴图工具统一转换后”的共用 LUT
    std::string m_osd_lut_path = "/app_demo/app_assets/chars_colorLUT.sscl";
    std::string m_texture_path = "/app_demo/app_assets/qr_ok_test.ssbmp";
    uint8_t *m_pcolor_lut = nullptr;
    int m_file_size = 0;
    int m_height, m_width;
    fdevice::DMA_BUFFER_ATTR_S m_layer_dma[OSD_LAYER_SIZE];
    fdevice::VERTEXS_S m_qrangle_out={0}, m_qrangle_in={0};
};

} // namespace osd
} // namespace device
} // namespace sst

#endif // SST_OSD_DEVICE_HPP_
