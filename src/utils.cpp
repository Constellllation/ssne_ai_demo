#include "../include/utils.hpp"

#include <vector>

void VISUALIZER::Initialize(std::array<int, 2>& in_img_shape) {
    osd_device.Initialize(in_img_shape[0], in_img_shape[1]);
}

void VISUALIZER::Draw() {
    std::vector<sst::device::osd::OsdQuadRangle> quad_rangle_vec;
    sst::device::osd::OsdQuadRangle q;

    q.color = 1;
    q.box = {150.0f, 120.0f, 500.0f, 320.0f};
    q.border = 2;
    q.layer_id = 0;
    q.alpha = fdevice::TYPE_ALPHA100;
    q.type = fdevice::TYPE_HOLLOW;

    quad_rangle_vec.emplace_back(q);
    osd_device.Draw(quad_rangle_vec, 0);
}

void VISUALIZER::Draw(const std::vector<std::array<float, 4>>& boxes) {
    std::vector<sst::device::osd::OsdQuadRangle> quad_rangle_vec;
    quad_rangle_vec.reserve(boxes.size());

    for (size_t i = 0; i < boxes.size(); ++i) {
        sst::device::osd::OsdQuadRangle q;
        q.box = {boxes[i][0], boxes[i][1], boxes[i][2], boxes[i][3]};
        q.color = 1;
        q.border = 2;
        q.layer_id = 0;
        q.alpha = fdevice::TYPE_ALPHA100;
        q.type = fdevice::TYPE_HOLLOW;
        quad_rangle_vec.emplace_back(q);
    }

    osd_device.Draw(quad_rangle_vec, 0);
}

void VISUALIZER::DrawQuads(const std::vector<QrQuad>& quads) {
    std::vector<std::array<std::array<float, 2>, 4>> osd_quads;
    osd_quads.reserve(quads.size());

    for (const auto& q : quads) {
        osd_quads.push_back(q.corners);
    }

    osd_device.DrawQuads(
        osd_quads,
        2,
        0,
        fdevice::TYPE_HOLLOW,
        fdevice::TYPE_ALPHA100,
        1);
}

//void VISUALIZER::DrawTextureItems(
//    const std::vector<sst::device::osd::OsdTextureItem>& items) {
//    osd_device.DrawTextures(items, 4);
//}

void VISUALIZER::Release() {
    osd_device.Release();
}
