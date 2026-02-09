#include "step_analyzer.h"
#include "httplib.h"
#include "nlohmann/json.hpp"
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <random>
#include <sstream>

using json = nlohmann::json;

static std::string make_temp_step_path() {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dis(0, 0x7FFFFFFF);
  std::ostringstream os;
  os << "/tmp/step_" << dis(gen) << ".stp";
  return os.str();
}

static double mm_to_in(double mm) {
  return mm * 0.03937007874;
}

static double mm2_to_in2(double mm2) {
  return mm2 * 0.0015500031;
}

static double mm3_to_in3(double mm3) {
  return mm3 * 0.0000610237441;
}

static json make_metric(const std::string& key,
                        const std::string& display_name,
                        const std::string& description,
                        const std::string& unit,
                        const std::string& category,
                        const std::string& occt_extraction,
                        const json& value,
                        const json& feeds) {
  json metric;
  metric["key"] = key;
  metric["display_name"] = display_name;
  metric["description"] = description;
  metric["unit"] = unit;
  metric["category"] = category;
  metric["occt_extraction"] = occt_extraction;
  metric["feeds"] = feeds;
  metric["value"] = value;
  return metric;
}

static json result_to_json(const step_analyzer::AnalysisResult& r) {
  json j;
  json metrics = json::array();

  json bbox_value;
  bbox_value["min"] = {mm_to_in(r.boundingBox.min[0]), mm_to_in(r.boundingBox.min[1]), mm_to_in(r.boundingBox.min[2])};
  bbox_value["max"] = {mm_to_in(r.boundingBox.max[0]), mm_to_in(r.boundingBox.max[1]), mm_to_in(r.boundingBox.max[2])};
  bbox_value["dimensions"] = {mm_to_in(r.boundingBox.dimensions[0]), mm_to_in(r.boundingBox.dimensions[1]), mm_to_in(r.boundingBox.dimensions[2])};
  metrics.push_back(make_metric(
    "bounding_box",
    "Bounding Box Dimensions",
    "Length × Width × Height and min/max coordinates",
    "in",
    "Geometry",
    "Bnd_Box via BRepBndLib::Add()",
    bbox_value,
    {"stock sizing", "fixturing", "stock volume estimation"}
  ));

  metrics.push_back(make_metric(
    "part_volume",
    "Part Volume",
    "Solid volume of the final part",
    "in^3",
    "Mass Properties",
    "BRepGProp::VolumeProperties()",
    mm3_to_in3(r.volume_mm3),
    {"material cost", "cycle time estimation"}
  ));

  metrics.push_back(make_metric(
    "total_surface_area",
    "Total Surface Area",
    "Overall external + internal surface area",
    "in^2",
    "Mass Properties",
    "BRepGProp::SurfaceProperties()",
    mm2_to_in2(r.surfaceArea_mm2),
    {"finishing time", "coating cost"}
  ));

  metrics.push_back(make_metric(
    "thin_wall_thickness",
    "Thin Wall Thickness",
    "Minimum distance between parallel or opposing planar faces",
    "in",
    "Manufacturability",
    "Plane normal comparison + distance between parallel planes",
    mm_to_in(r.minWallThickness_mm),
    {"deflection risk", "fixturing"}
  ));

  json topo;
  topo["faces"] = r.numFaces;
  topo["edges"] = r.numEdges;
  topo["vertices"] = r.numVertices;
  metrics.push_back(make_metric(
    "topology_counts",
    "Number of Faces / Edges / Vertices",
    "Topological complexity metrics",
    "count",
    "Geometry",
    "TopExp::MapShapes()",
    topo,
    {"complexity scoring", "toolpath planning"}
  ));

  json tool_access;
  tool_access["large_percent"] = r.toolAccess.large_percent;
  tool_access["medium_percent"] = r.toolAccess.medium_percent;
  tool_access["small_percent"] = r.toolAccess.small_percent;
  metrics.push_back(make_metric(
    "tool_accessibility_distribution",
    "Feature Size / Tool Accessibility Distribution",
    "Percent of cylindrical surface area accessible by tool diameter class",
    "percent",
    "Manufacturability",
    "Cylindrical face area bucketed by diameter thresholds",
    tool_access,
    {"tool selection", "cycle time estimation"}
  ));

  json face_dist = json::array();
  for (const auto& area : r.faceAreas) {
    json item;
    item["type"] = area.type;
    item["area_in2"] = mm2_to_in2(area.area_mm2);
    face_dist.push_back(item);
  }
  metrics.push_back(make_metric(
    "face_type_distribution",
    "Face Type Distribution",
    "Surface area by face type for geometry classification",
    "in^2",
    "Geometry",
    "BRepAdaptor_Surface + BRepGProp::SurfaceProperties()",
    face_dist,
    {"geometry classification", "toolpath strategy"}
  ));

  json hole_diams = json::array();
  for (double d : r.holeDiameters_mm) {
    hole_diams.push_back(mm_to_in(d));
  }
  metrics.push_back(make_metric(
    "hole_diameters",
    "Hole Diameters",
    "Distinct diameters for each hole present",
    "in",
    "Features",
    "Cylindrical face detection and radius grouping",
    hole_diams,
    {"drill selection", "tooling cost"}
  ));

  json curvature;
  curvature["min_abs_in_inv"] = r.curvature.min_abs_mm_inv / 0.03937007874;
  curvature["avg_abs_in_inv"] = r.curvature.avg_abs_mm_inv / 0.03937007874;
  curvature["max_abs_in_inv"] = r.curvature.max_abs_mm_inv / 0.03937007874;
  metrics.push_back(make_metric(
    "curvature_statistics",
    "Curvature Statistics",
    "Max/average/min absolute curvature",
    "1/in",
    "Geometry",
    "BRepLProp_SLProps at mid-UV of each face",
    curvature,
    {"geometry classification", "finishing difficulty"}
  ));

  metrics.push_back(make_metric(
    "surface_area_breakdown",
    "Surface Area Breakdown",
    "Surface area by face type",
    "in^2",
    "Geometry",
    "BRepAdaptor_Surface + BRepGProp::SurfaceProperties()",
    face_dist,
    {"finishing strategy", "coating cost"}
  ));

  j["metrics"] = metrics;

  j["features"]["holes"] = json::array();
  for (const auto& h : r.holes) {
    json hh;
    hh["id"] = h.id;
    hh["type"] = h.type;
    hh["diameter_in"] = mm_to_in(h.diameter_mm);
    hh["depth_in"] = mm_to_in(h.depth_mm);
    hh["axis"] = {h.axis[0], h.axis[1], h.axis[2]};
    hh["centroid_in"] = {mm_to_in(h.centroid[0]), mm_to_in(h.centroid[1]), mm_to_in(h.centroid[2])};
    hh["faces"] = h.faces;
    j["features"]["holes"].push_back(hh);
  }

  j["features"]["pockets"] = json::array();
  for (const auto& p : r.pockets) {
    json pp;
    pp["id"] = p.id;
    pp["depth_in"] = mm_to_in(p.depth_mm);
    pp["cornerRadius_in"] = mm_to_in(p.cornerRadius_mm);
    pp["volume_in3"] = mm3_to_in3(p.volume_mm3);
    pp["openingArea_in2"] = mm2_to_in2(p.openingArea_mm2);
    pp["bounds_in"] = {mm_to_in(p.bounds_mm[0]), mm_to_in(p.bounds_mm[1]), mm_to_in(p.bounds_mm[2])};
    pp["faces"] = p.faces;
    j["features"]["pockets"].push_back(pp);
  }

  j["meta"]["units"] = "inch";
  j["meta"]["kernel"] = "OpenCascade";
  j["meta"]["source"] = "step";
  return j;
}

int main() {
  const char* port_env = std::getenv("PORT");
  int port = port_env ? std::atoi(port_env) : 10000;

  httplib::Server svr;

  svr.Post("/analyze", [](const httplib::Request& req, httplib::Response& res) {
    if (!req.has_file("file")) {
      res.status = 400;
      res.set_content("{\"error\":\"Missing multipart field 'file'\"}", "application/json");
      return;
    }
    const auto& file = req.get_file_value("file");
    if (file.length == 0 || file.offset + file.length > req.body.size()) {
      res.status = 400;
      res.set_content("{\"error\":\"Invalid file part\"}", "application/json");
      return;
    }
    std::string step_path = make_temp_step_path();
    std::ofstream ofs(step_path, std::ios::binary);
    if (!ofs) {
      res.status = 500;
      res.set_content("{\"error\":\"Could not write temp file\"}", "application/json");
      return;
    }
    ofs.write(req.body.data() + file.offset, file.length);
    ofs.close();
    if (!ofs) {
      std::remove(step_path.c_str());
      res.status = 500;
      res.set_content("{\"error\":\"Could not write temp file\"}", "application/json");
      return;
    }

    step_analyzer::AnalysisResult result = step_analyzer::analyze_step(step_path);
    std::remove(step_path.c_str());

    if (!result.success) {
      res.status = 422;
      json err;
      err["error"] = result.error_message;
      res.set_content(err.dump(), "application/json");
      return;
    }

    json j = result_to_json(result);
    res.set_content(j.dump(), "application/json");
  });

  svr.Get("/health", [](const httplib::Request&, httplib::Response& res) {
    res.set_content("{\"status\":\"ok\"}", "application/json");
  });

  std::cout << "Listening on 0.0.0.0:" << port << std::endl;
  svr.listen("0.0.0.0", port);
  return 0;
}
