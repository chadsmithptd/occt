#include "step_analyzer.h"
#include <STEPControl_Reader.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepLProp_SLProps.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Edge.hxx>
#include <TopExp_Explorer.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <GeomAbs_CurveType.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <gp_Dir.hxx>
#include <gp_Ax1.hxx>
#include <gp_Circ.hxx>
#include <gp_Vec.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopExp.hxx>
#include <cmath>
#include <map>
#include <set>
#include <sstream>
#include <algorithm>

namespace step_analyzer {

static const double TOL_ANGLE = 1e-6;
static const double TOL_RADIUS = 1e-6;

static std::string face_id(int index) {
  std::ostringstream os;
  os << "face_" << index;
  return os.str();
}

static bool vectors_parallel(const double a[3], const double b[3]) {
  double d = a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
  return std::fabs(std::fabs(d) - 1.0) < TOL_ANGLE;
}

static bool same_axis(const double a[3], const double b[3]) {
  return vectors_parallel(a, b) || (std::fabs(a[0]+b[0]) < TOL_ANGLE &&
         std::fabs(a[1]+b[1]) < TOL_ANGLE && std::fabs(a[2]+b[2]) < TOL_ANGLE);
}

static void dir_to_array(const gp_Dir& d, double out[3]) {
  out[0] = d.X(); out[1] = d.Y(); out[2] = d.Z();
}

static double dot3(const gp_Dir& a, const gp_Dir& b) {
  return a.X() * b.X() + a.Y() * b.Y() + a.Z() * b.Z();
}

static double planar_face_distance(const gp_Pln& a, const gp_Pln& b) {
  gp_Dir na = a.Axis().Direction();
  gp_Dir nb = b.Axis().Direction();
  double alignment = std::fabs(dot3(na, nb));
  if (std::fabs(alignment - 1.0) > 1e-4) {
    return -1.0;
  }
  gp_Pnt pa = a.Location();
  gp_Pnt pb = b.Location();
  gp_Vec v(pa, pb);
  double dist = std::fabs(v.Dot(gp_Vec(na)));
  return dist;
}

AnalysisResult analyze_step(const std::string& step_file_path) {
  AnalysisResult result = {};
  result.success = false;
  result.volume_mm3 = 0.0;
  result.surfaceArea_mm2 = 0.0;
  result.minWallThickness_mm = 0.0;
  result.numFaces = 0;
  result.numEdges = 0;
  result.numVertices = 0;
  result.toolAccess = {0.0, 0.0, 0.0};
  result.curvature = {0.0, 0.0, 0.0};
  for (int i = 0; i < 3; i++) {
    result.boundingBox.min[i] = result.boundingBox.max[i] = 0.0;
    result.boundingBox.dimensions[i] = 0.0;
  }

  STEPControl_Reader reader;
  IFSelect_ReturnStatus status = reader.ReadFile(step_file_path.c_str());
  if (status != IFSelect_RetDone) {
    result.error_message = "STEP file could not be read";
    return result;
  }
  reader.TransferRoots();
  TopoDS_Shape shape = reader.OneShape();
  if (shape.IsNull()) {
    result.error_message = "No shape in STEP file";
    return result;
  }

  Bnd_Box bbox;
  BRepBndLib::Add(shape, bbox, Standard_True);
  Standard_Real xmin = 0, ymin = 0, zmin = 0, xmax = 0, ymax = 0, zmax = 0;
  if (!bbox.IsVoid()) {
    bbox.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    result.boundingBox.min[0] = xmin;
    result.boundingBox.min[1] = ymin;
    result.boundingBox.min[2] = zmin;
    result.boundingBox.max[0] = xmax;
    result.boundingBox.max[1] = ymax;
    result.boundingBox.max[2] = zmax;
    result.boundingBox.dimensions[0] = xmax - xmin;
    result.boundingBox.dimensions[1] = ymax - ymin;
    result.boundingBox.dimensions[2] = zmax - zmin;
  }

  GProp_GProps vProps;
  try {
    BRepGProp::VolumeProperties(shape, vProps, Standard_False, Standard_True, Standard_False);
    result.volume_mm3 = vProps.Mass();
  } catch (...) {
    result.volume_mm3 = 0.0;
  }

  GProp_GProps sProps;
  try {
    BRepGProp::SurfaceProperties(shape, sProps, Standard_True, Standard_False);
    result.surfaceArea_mm2 = sProps.Mass();
  } catch (...) {
    result.surfaceArea_mm2 = 0.0;
  }

  TopTools_IndexedMapOfShape faceMap;
  TopExp::MapShapes(shape, TopAbs_FACE, faceMap);
  int numFaces = faceMap.Extent();
  result.numFaces = numFaces;

  TopTools_IndexedMapOfShape edgeMap;
  TopExp::MapShapes(shape, TopAbs_EDGE, edgeMap);
  result.numEdges = edgeMap.Extent();

  TopTools_IndexedMapOfShape vertexMap;
  TopExp::MapShapes(shape, TopAbs_VERTEX, vertexMap);
  result.numVertices = vertexMap.Extent();

  std::vector<int> cylindrical_face_indices;
  std::map<int, double> face_radius;
  std::map<int, double> face_axis[3];
  std::map<int, gp_Pnt> face_axis_origin;
  std::map<int, std::pair<double, double>> face_cyl_extent;
  std::map<int, gp_Pln> planar_faces;
  std::map<std::string, double> face_type_area;
  std::vector<double> hole_diameters;

  double curvature_sum = 0.0;
  int curvature_samples = 0;
  double curvature_min = 0.0;
  double curvature_max = 0.0;
  bool curvature_init = false;

  double total_cyl_area = 0.0;
  double tool_large_area = 0.0;
  double tool_medium_area = 0.0;
  double tool_small_area = 0.0;

  for (int idx = 1; idx <= numFaces; idx++) {
    TopoDS_Face face = TopoDS::Face(faceMap(idx));
    BRepAdaptor_Surface adapt(face, Standard_True);
    GeomAbs_SurfaceType stype = adapt.GetType();

    GProp_GProps faceProps;
    BRepGProp::SurfaceProperties(face, faceProps);
    double face_area = faceProps.Mass();

    std::string type_label = "other";
    if (stype == GeomAbs_Plane) type_label = "planar";
    else if (stype == GeomAbs_Cylinder) type_label = "cylindrical";
    else if (stype == GeomAbs_Cone) type_label = "conical";
    else if (stype == GeomAbs_Sphere) type_label = "spherical";
    else if (stype == GeomAbs_Torus) type_label = "toroidal";
    else if (stype == GeomAbs_BSplineSurface || stype == GeomAbs_BezierSurface) type_label = "freeform";
    face_type_area[type_label] += face_area;

    if (stype == GeomAbs_Cylinder) {
      gp_Cylinder cyl = adapt.Cylinder();
      double r = cyl.Radius();
      if (r > 1e-9 && r < 1e9) {
        gp_Ax1 ax = cyl.Axis();
        gp_Dir d = ax.Direction();
        double axis[3];
        dir_to_array(d, axis);
        cylindrical_face_indices.push_back(idx);
        face_radius[idx] = r;
        face_axis[0][idx] = axis[0];
        face_axis[1][idx] = axis[1];
        face_axis[2][idx] = axis[2];
        face_axis_origin[idx] = ax.Location();
        total_cyl_area += face_area;
        double diameter = 2.0 * r;
        if (diameter > 12.7) tool_large_area += face_area;
        else if (diameter >= 6.35) tool_medium_area += face_area;
        else tool_small_area += face_area;

        Bnd_Box fbox;
        BRepBndLib::Add(face, fbox, Standard_True);
        if (!fbox.IsVoid()) {
          Standard_Real fx1, fy1, fz1, fx2, fy2, fz2;
          fbox.Get(fx1, fy1, fz1, fx2, fy2, fz2);
          gp_Pnt c = ax.Location();
          double t1 = (fx1 - c.X()) * axis[0] + (fy1 - c.Y()) * axis[1] + (fz1 - c.Z()) * axis[2];
          double t2 = (fx2 - c.X()) * axis[0] + (fy2 - c.Y()) * axis[1] + (fz2 - c.Z()) * axis[2];
          face_cyl_extent[idx] = std::make_pair(std::min(t1, t2), std::max(t1, t2));
        }
      }
    }

    if (stype == GeomAbs_Plane) {
      planar_faces[idx] = adapt.Plane();
    }

    Standard_Real u1, u2, v1, v2;
    adapt.Surface().Bounds(u1, u2, v1, v2);
    double u = (u1 + u2) * 0.5;
    double v = (v1 + v2) * 0.5;
    BRepLProp_SLProps props(adapt.Surface().Surface(), u, v, 2, 1e-6);
    if (props.IsCurvatureDefined()) {
      double k1 = props.MaxCurvature();
      double k2 = props.MinCurvature();
      double abs_k = std::max(std::fabs(k1), std::fabs(k2));
      curvature_sum += abs_k;
      curvature_samples++;
      if (!curvature_init) {
        curvature_min = abs_k;
        curvature_max = abs_k;
        curvature_init = true;
      } else {
        curvature_min = std::min(curvature_min, abs_k);
        curvature_max = std::max(curvature_max, abs_k);
      }
    }
  }

  if (curvature_samples > 0) {
    result.curvature.avg_abs_mm_inv = curvature_sum / static_cast<double>(curvature_samples);
    result.curvature.min_abs_mm_inv = curvature_min;
    result.curvature.max_abs_mm_inv = curvature_max;
  }

  if (total_cyl_area > 1e-9) {
    result.toolAccess.large_percent = (tool_large_area / total_cyl_area) * 100.0;
    result.toolAccess.medium_percent = (tool_medium_area / total_cyl_area) * 100.0;
    result.toolAccess.small_percent = (tool_small_area / total_cyl_area) * 100.0;
  }

  for (const auto& entry : face_type_area) {
    FaceTypeArea fta;
    fta.type = entry.first;
    fta.area_mm2 = entry.second;
    result.faceAreas.push_back(fta);
  }

  double min_wall = 0.0;
  bool wall_init = false;
  if (!planar_faces.empty()) {
    for (auto it1 = planar_faces.begin(); it1 != planar_faces.end(); ++it1) {
      for (auto it2 = std::next(it1); it2 != planar_faces.end(); ++it2) {
        double dist = planar_face_distance(it1->second, it2->second);
        if (dist <= 0.0) {
          continue;
        }
        if (!wall_init) {
          min_wall = dist;
          wall_init = true;
        } else {
          min_wall = std::min(min_wall, dist);
        }
      }
    }
  }
  result.minWallThickness_mm = wall_init ? min_wall : 0.0;

  std::set<int> used_cyl;
  int hole_count = 0;
  for (size_t i = 0; i < cylindrical_face_indices.size(); i++) {
    int fi = cylindrical_face_indices[i];
    if (used_cyl.count(fi))
      continue;
    hole_count++;
    Hole h;
    std::ostringstream id;
    id << "hole_" << hole_count;
    h.id = id.str();
    h.diameter_mm = 2.0 * face_radius[fi];
    double ax[3] = { face_axis[0][fi], face_axis[1][fi], face_axis[2][fi] };
    h.axis[0] = ax[0];
    h.axis[1] = ax[1];
    h.axis[2] = ax[2];
    h.centroid[0] = 0.0;
    h.centroid[1] = 0.0;
    h.centroid[2] = 0.0;
    h.faces.push_back(face_id(fi));
    used_cyl.insert(fi);

    double tMin = face_cyl_extent[fi].first;
    double tMax = face_cyl_extent[fi].second;
    for (size_t j = i + 1; j < cylindrical_face_indices.size(); j++) {
      int fj = cylindrical_face_indices[j];
      if (used_cyl.count(fj))
        continue;
      if (std::fabs(face_radius[fj] - face_radius[fi]) > TOL_RADIUS)
        continue;
      double ax2[3] = { face_axis[0][fj], face_axis[1][fj], face_axis[2][fj] };
      if (!same_axis(ax, ax2))
        continue;
      used_cyl.insert(fj);
      h.faces.push_back(face_id(fj));
      tMin = std::min(tMin, face_cyl_extent[fj].first);
      tMax = std::max(tMax, face_cyl_extent[fj].second);
    }
    h.depth_mm = std::max(0.0, tMax - tMin);
    gp_Pnt origin = face_axis_origin[fi];
    double tMid = (tMin + tMax) * 0.5;
    h.centroid[0] = origin.X() + ax[0] * tMid;
    h.centroid[1] = origin.Y() + ax[1] * tMid;
    h.centroid[2] = origin.Z() + ax[2] * tMid;
    double bbox_len = result.boundingBox.dimensions[0] * std::fabs(ax[0]) +
                     result.boundingBox.dimensions[1] * std::fabs(ax[1]) +
                     result.boundingBox.dimensions[2] * std::fabs(ax[2]);
    if (h.depth_mm >= bbox_len * 0.99)
      h.type = "through";
    else
      h.type = "blind";
    result.holes.push_back(h);
    hole_diameters.push_back(h.diameter_mm);
  }

  std::sort(hole_diameters.begin(), hole_diameters.end());
  for (double d : hole_diameters) {
    if (result.holeDiameters_mm.empty() || std::fabs(result.holeDiameters_mm.back() - d) > 1e-4) {
      result.holeDiameters_mm.push_back(d);
    }
  }

  int pocket_count = 0;
  for (int idx = 1; idx <= numFaces; idx++) {
    TopoDS_Face face = TopoDS::Face(faceMap(idx));
    BRepAdaptor_Surface adapt(face, Standard_True);
    if (adapt.GetType() != GeomAbs_Plane)
      continue;
    gp_Pln pln = adapt.Plane();
    gp_Dir n = pln.Axis().Direction();
    gp_Pnt loc = pln.Location();
    double nx = n.X(), ny = n.Y(), nz = n.Z();
    if (std::fabs(nx) < TOL_ANGLE && std::fabs(ny) < TOL_ANGLE && std::fabs(nz) < TOL_ANGLE)
      continue;

    Bnd_Box fbox;
    BRepBndLib::Add(face, fbox, Standard_True);
    if (fbox.IsVoid())
      continue;
    Standard_Real fx1, fy1, fz1, fx2, fy2, fz2;
    fbox.Get(fx1, fy1, fz1, fx2, fy2, fz2);
    double d_plane = loc.X()*nx + loc.Y()*ny + loc.Z()*nz;
    double d_min = fx1*nx + fy1*ny + fz1*nz;
    double d_max = fx2*nx + fy2*ny + fz2*nz;
    double depth = std::fabs((std::max(d_max, d_min) - std::min(d_max, d_min)));
    if (depth < 1e-6)
      continue;

    double cornerRad = 0.0;
    TopExp_Explorer edgeExp(face, TopAbs_EDGE);
    for (; edgeExp.More(); edgeExp.Next()) {
      BRepAdaptor_Curve curve(TopoDS::Edge(edgeExp.Current()));
      if (curve.GetType() == GeomAbs_Circle) {
        double r = curve.Circle().Radius();
        if (r > 1e-9 && (cornerRad < 1e-9 || r < cornerRad))
          cornerRad = r;
      }
    }

    pocket_count++;
    Pocket p;
    std::ostringstream pid;
    pid << "pocket_" << pocket_count;
    p.id = pid.str();
    p.depth_mm = depth;
    p.cornerRadius_mm = cornerRad;
    double dx = std::fabs(fx2 - fx1);
    double dy = std::fabs(fy2 - fy1);
    double dz = std::fabs(fz2 - fz1);
    double dims[3] = {dx, dy, dz};
    std::sort(std::begin(dims), std::end(dims));
    double openingArea = dims[2] * dims[1];
    p.openingArea_mm2 = openingArea;
    p.volume_mm3 = openingArea * depth;
    p.bounds_mm[0] = dx;
    p.bounds_mm[1] = dy;
    p.bounds_mm[2] = dz;
    p.faces.push_back(face_id(idx));
    result.pockets.push_back(p);
  }

  result.success = true;
  result.error_message.clear();
  return result;
}

}  // namespace step_analyzer
