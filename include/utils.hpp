#pragma once

#include "common.hpp"
#include "osd-device.hpp"

#include <array>
#include <vector>

namespace utils {
void Merge(FaceDetectionResult* result, size_t low, size_t mid, size_t high);
void MergeSort(FaceDetectionResult* result, size_t low, size_t high);
void SortDetectionResult(FaceDetectionResult* result);
void NMS(FaceDetectionResult* result, float iou_threshold, int top_k);
} // namespace utils

struct QrQuad {
    std::array<std::array<float, 2>, 4> corners;
};

class VISUALIZER {
public:
    void Initialize(std::array<int, 2>& in_img_shape);
    void Release();
    void Draw();
    void Draw(const std::vector<std::array<float, 4>>& boxes);
    void DrawQuads(const std::vector<QrQuad>& quads);

private:
    sst::device::osd::OsdDevice osd_device;
};
