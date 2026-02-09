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

struct FaceTypeArea {
  std::string type;
  double area_mm2;
};

struct CurvatureStats {
  double min_abs_mm_inv;
  double max_abs_mm_inv;
  double avg_abs_mm_inv;
};

struct ToolAccessibility {
  double large_percent;
  double medium_percent;
  double small_percent;
};

struct Hole {
  std::string id;
  std::string type;  // "through" | "blind" | "counterbore" | "countersink"
  double diameter_mm;
  double depth_mm;
  double axis[3];
  double centroid[3];
  std::vector<std::string> faces;
};

struct Pocket {
  std::string id;
  double depth_mm;
  double cornerRadius_mm;
  double volume_mm3;
  double openingArea_mm2;
  double bounds_mm[3];
  std::vector<std::string> faces;
};

struct AnalysisResult {
  bool success;
  std::string error_message;
  BoundingBox boundingBox;
  double volume_mm3;
  double surfaceArea_mm2;
  double minWallThickness_mm;
  int numFaces;
  int numEdges;
  int numVertices;
  ToolAccessibility toolAccess;
  CurvatureStats curvature;
  std::vector<FaceTypeArea> faceAreas;
  std::vector<double> holeDiameters_mm;
  std::vector<Hole> holes;
  std::vector<Pocket> pockets;
};

AnalysisResult analyze_step(const std::string& step_file_path);

}  // namespace step_analyzer

#endif
