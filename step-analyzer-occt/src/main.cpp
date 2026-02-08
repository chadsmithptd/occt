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

static json result_to_json(const step_analyzer::AnalysisResult& r) {
  json j;
  j["boundingBox"]["min"] = {r.boundingBox.min[0], r.boundingBox.min[1], r.boundingBox.min[2]};
  j["boundingBox"]["max"] = {r.boundingBox.max[0], r.boundingBox.max[1], r.boundingBox.max[2]};
  j["boundingBox"]["dimensions"] = {r.boundingBox.dimensions[0], r.boundingBox.dimensions[1], r.boundingBox.dimensions[2]};
  j["massProperties"]["volume_mm3"] = r.volume_mm3;
  j["massProperties"]["surfaceArea_mm2"] = r.surfaceArea_mm2;
  j["features"]["holes"] = json::array();
  for (const auto& h : r.holes) {
    json hh;
    hh["id"] = h.id;
    hh["type"] = h.type;
    hh["diameter_mm"] = h.diameter_mm;
    hh["depth_mm"] = h.depth_mm;
    hh["axis"] = {h.axis[0], h.axis[1], h.axis[2]};
    hh["faces"] = h.faces;
    j["features"]["holes"].push_back(hh);
  }
  j["features"]["pockets"] = json::array();
  for (const auto& p : r.pockets) {
    json pp;
    pp["id"] = p.id;
    pp["depth_mm"] = p.depth_mm;
    pp["cornerRadius_mm"] = p.cornerRadius_mm;
    pp["faces"] = p.faces;
    j["features"]["pockets"].push_back(pp);
  }
  j["meta"]["units"] = "mm";
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
