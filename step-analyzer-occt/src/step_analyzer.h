#ifndef STEP_ANALYZER_H
#define STEP_ANALYZER_H

#include <string>
#include <vector>

namespace step_analyzer {

struct BoundingBox {
  double min[3];
  double max[3];
  double dimensions[3];
};

struct Hole {
  std::string id;
  std::string type;  // "through" | "blind" | "counterbore" | "countersink"
  double diameter_mm;
  double depth_mm;
  double axis[3];
  std::vector<std::string> faces;
};

struct Pocket {
  std::string id;
  double depth_mm;
  double cornerRadius_mm;
  std::vector<std::string> faces;
};

struct AnalysisResult {
  bool success;
  std::string error_message;
  BoundingBox boundingBox;
  double volume_mm3;
  double surfaceArea_mm2;
  std::vector<Hole> holes;
  std::vector<Pocket> pockets;
};

AnalysisResult analyze_step(const std::string& step_file_path);

}  // namespace step_analyzer

#endif
