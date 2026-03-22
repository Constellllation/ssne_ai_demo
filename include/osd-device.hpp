#ifndef SST_OSD_DEVICE_HPP_
#define SST_OSD_DEVICE_HPP_

#include <array>
#include <string>
#include <vector>

#include "osd_lib_api.h"
#include "common.hpp"

#define BUFFER_TYPE_DMABUF 0x1
#define OSD_LAYER_SIZE 5

namespace sst {
namespace device {
namespace osd {

typedef struct {
    std::array<float, 4> box;
    int border;
    int layer_id;
    fdevice::QUADRANGLETYPE type;
    fdevice::ALPHATYPE alpha;
    int color;
} OsdQuadRangle;

class OsdDevice {
public:
    OsdDevice();
    ~OsdDevice();

    void Initialize(int width, int height);
    void Release();

    void Draw(std::vector<OsdQuadRangle>& quad_rangle);
    void Draw(std::vector<std::array<float, 4>>& boxes,
              int border,
              int layer_id,
              fdevice::QUADRANGLETYPE type,
              fdevice::ALPHATYPE alpha,
              int color);
    void Draw(std::vector<OsdQuadRangle>& quad_rangle, int layer_id);

    void DrawQuads(const std::vector<std::array<std::array<float, 2>, 4>>& quads,
                   int border,
                   int layer_id,
                   fdevice::QUADRANGLETYPE type,
                   fdevice::ALPHATYPE alpha,
                   int color);

private:
    int LoadLutFile(const char* filename);
    void GenQrangleBox(std::array<float, 4>& det, int border);
    void GenQranglePolygon(const std::array<std::array<float, 2>, 4>& pts, int border);

private:
    handle_t m_osd_handle;
    std::string m_osd_lut_path = "/app_demo/app_assets/colorLUT.sscl";
    uint8_t* m_pcolor_lut = nullptr;
    int m_file_size = 0;
    int m_height = 0;
    int m_width = 0;
    fdevice::DMA_BUFFER_ATTR_S m_layer_dma[OSD_LAYER_SIZE];
    fdevice::VERTEXS_S m_qrangle_out = {0}, m_qrangle_in = {0};
};

} // namespace osd
} // namespace device
} // namespace sst

#endif // SST_OSD_DEVICE_HPP_
