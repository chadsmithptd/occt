#include "step_analyzer.h"
#include <STEPControl_Reader.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAdaptor_Curve.hxx>
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
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopExp.hxx>
#include <cmath>
#include <map>
#include <set>
#include <sstream>
#include <algorithm>

namespace step_analyzer {

static const double TOL_ANGLE = 1e-6;
static const double TOL_LENGTH = 1e-6;
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

AnalysisResult analyze_step(const std::string& step_file_path) {
  AnalysisResult result = {};
  result.success = false;
  result.volume_mm3 = 0.0;
  result.surfaceArea_mm2 = 0.0;
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

  std::vector<int> cylindrical_face_indices;
  std::map<int, double> face_radius;
  std::map<int, double> face_axis[3];
  std::map<int, std::pair<double, double>> face_cyl_extent;

  for (int idx = 1; idx <= numFaces; idx++) {
    TopoDS_Face face = TopoDS::Face(faceMap(idx));
    BRepAdaptor_Surface adapt(face, Standard_True);
    if (adapt.GetType() != GeomAbs_Cylinder)
      continue;
    gp_Cylinder cyl = adapt.Cylinder();
    double r = cyl.Radius();
    if (r < 1e-9 || r > 1e9)
      continue;
    gp_Ax1 ax = cyl.Axis();
    gp_Dir d = ax.Direction();
    double axis[3];
    dir_to_array(d, axis);
    cylindrical_face_indices.push_back(idx);
    face_radius[idx] = r;
    face_axis[0][idx] = axis[0];
    face_axis[1][idx] = axis[1];
    face_axis[2][idx] = axis[2];

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
    double bbox_len = result.boundingBox.dimensions[0] * std::fabs(ax[0]) +
                     result.boundingBox.dimensions[1] * std::fabs(ax[1]) +
                     result.boundingBox.dimensions[2] * std::fabs(ax[2]);
    if (h.depth_mm >= bbox_len * 0.99)
      h.type = "through";
    else
      h.type = "blind";
    result.holes.push_back(h);
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
    p.faces.push_back(face_id(idx));
    result.pockets.push_back(p);
  }

  result.success = true;
  result.error_message.clear();
  return result;
}

}  // namespace step_analyzer
