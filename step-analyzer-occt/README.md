# step-analyzer-occt

Minimal production-ready C++ backend that accepts a STEP file via HTTP, analyzes it with OpenCascade (OCCT), and returns manufacturing-relevant geometric data as JSON. Designed for deployment on Render.com and consumption by a web UI (e.g. WeWeb).

## System dependencies (Docker / Ubuntu 22.04)

- **OpenCascade (OCCT)** – geometry kernel (STEP read, BRep, bounding box, volume/surface, face types)
- **CMake** – build system
- **G++** – C++17 compiler
- **Git** – used by CMake FetchContent to download nlohmann/json and cpp-httplib

The Dockerfile installs these via `apt-get`:

- `libocct-data-exchange-dev` – STEP/IGES (STEPControl_Reader)
- `libocct-foundation-dev` – core types
- `libocct-modeling-algorithms-dev` – BRep, BRepGProp, BRepBndLib, BRepAdaptor
- `libocct-modeling-data-dev` – geometry data

## Step-by-step local build (no Docker)

1. **Install OCCT and build tools (Ubuntu/Debian)**

   ```bash
   sudo apt-get update
   sudo apt-get install -y cmake g++ git \
     libocct-data-exchange-dev \
     libocct-foundation-dev \
     libocct-modeling-algorithms-dev \
     libocct-modeling-data-dev
   ```

2. **Clone or enter the project**

   ```bash
   cd step-analyzer-occt
   ```

3. **Configure and build**

   ```bash
   mkdir build
   cd build
   cmake .. -DCMAKE_BUILD_TYPE=Release
   cmake --build . -j$(nproc)
   ```

4. **Run the server**

   ```bash
   ./step-analyzer-occt
   ```

   By default it listens on port 10000. Override with the `PORT` environment variable:

   ```bash
   PORT=8080 ./step-analyzer-occt
   ```

## Step-by-step Render.com deployment

1. **Create a Web Service**

   - In Render Dashboard: **New** → **Web Service**.
   - Connect your repo (or use the repo that contains `step-analyzer-occt`).
   - Set **Root Directory** to `step-analyzer-occt` (if the service lives in a subdirectory).
   - **Environment**: Docker.
   - Render will use the `Dockerfile` in that root.

2. **Build and run**

   - Render runs `docker build` then runs the image.
   - The Dockerfile installs OCCT packages, builds the C++ binary, and sets the default command to `step-analyzer-occt`.
   - Render sets `PORT` (e.g. 10000); the app reads `PORT` and listens on `0.0.0.0:PORT`.

3. **No extra configuration**

   - No UI, no env vars required beyond `PORT` (set by Render).
   - After deploy you get a URL like `https://<service-name>.onrender.com`.

## Test locally with curl

1. Start the server (see above).

2. **Health check**

   ```bash
   curl -s http://localhost:10000/health
   ```

   Expected: `{"status":"ok"}`

3. **Analyze a STEP file**

   ```bash
   curl -X POST http://localhost:10000/analyze \
     -F "file=@/path/to/your/file.stp"
   ```

   Replace `/path/to/your/file.stp` with a real STEP file. The response is JSON (see example below).

## Test once deployed on Render.com

1. Get your service URL, e.g. `https://step-analyzer-occt.onrender.com`.

2. **Health check**

   ```bash
   curl -s https://step-analyzer-occt.onrender.com/health
   ```

3. **Analyze a STEP file**

   ```bash
   curl -X POST https://step-analyzer-occt.onrender.com/analyze \
     -F "file=@/path/to/your/file.stp"
   ```

## API

### POST /analyze

- **Content-Type**: `multipart/form-data`
- **Field name**: `file` (STEP file)
- **Response**: JSON with `metrics` (each metric has the required metadata fields), `features` (holes/pockets), and `meta`.

Example response structure (values shortened):

```json
{
  "metrics": [
    {
      "key": "bounding_box",
      "display_name": "Bounding Box Dimensions",
      "description": "Length × Width × Height and min/max coordinates",
      "unit": "in",
      "category": "Geometry",
      "occt_extraction": "Bnd_Box via BRepBndLib::Add()",
      "feeds": ["stock sizing", "fixturing", "stock volume estimation"],
      "value": {
        "min": [0.0, 0.0, 0.0],
        "max": [4.72, 3.15, 1.18],
        "dimensions": [4.72, 3.15, 1.18]
      }
    }
  ],
  "features": {
    "holes": [
      {
        "id": "hole_1",
        "type": "through",
        "diameter_in": 0.5,
        "depth_in": 1.2,
        "axis": [0, 0, 1],
        "centroid_in": [1.1, 0.7, 0.3],
        "faces": ["face_12", "face_18"]
      }
    ],
    "pockets": [
      {
        "id": "pocket_1",
        "depth_in": 0.4,
        "cornerRadius_in": 0.08,
        "volume_in3": 0.15,
        "openingArea_in2": 0.36,
        "bounds_in": [1.2, 0.8, 0.4],
        "faces": ["face_21"]
      }
    ]
  },
  "meta": {
    "units": "inch",
    "kernel": "OpenCascade",
    "source": "step"
  }
}
```

### GET /health

- **Response**: `{"status":"ok"}`

---

## Where OCCT is used

- **STEPControl_Reader** – read STEP file and transfer roots to get a single `TopoDS_Shape`.
- **BRepBndLib::Add** – add the shape to a `Bnd_Box` to get min/max/dimensions.
- **BRepGProp::VolumeProperties** – compute volume (reported as `volume_mm3`).
- **BRepGProp::SurfaceProperties** – compute surface area (reported as `surfaceArea_mm2`).
- **BRepAdaptor_Surface** – per-face adaptor to query surface type.
- **GeomAbs_Cylinder** – cylindrical faces are used to detect holes (axis, radius, extent).
- **GeomAbs_Plane** – planar faces are used to detect pocket floors (depth from face extent).
- **BRepAdaptor_Curve** – on pocket floor faces, edges are checked for **GeomAbs_Circle** to report corner radius (fillet).
- **TopExp::MapShapes** – face/edge/vertex counts for complexity metrics.
- **BRepLProp_SLProps** – curvature statistics sampled at mid-UV of each face.
- **Plane distance** – thin-wall thickness from parallel planar faces.
- Tessellation is only used where OCCT uses it internally (e.g. bounding box with `useTriangulation=true`); feature detection is based on exact BRep (cylinders, planes, circles).

Tolerances (angle, length, radius) are set for typical manufacturing geometry (e.g. 1e-6). Face and feature IDs are stable: `face_1`, `face_2`, … by exploration order; `hole_1`, `pocket_1`, … by detection order.

---

## How holes are detected

1. All faces are explored with `TopExp_Explorer` / `TopExp::MapShapes(..., TopAbs_FACE, ...)`.
2. For each face, `BRepAdaptor_Surface` is used; if `GetType() == GeomAbs_Cylinder`, the face is a cylinder.
3. Cylinders with a reasonable radius (excluding degenerate or huge) are collected. From `gp_Cylinder` we get axis (direction) and radius. The face’s bounding box is projected onto the axis to get an extent (min/max parameter along the axis).
4. Cylindrical faces are **grouped** by same axis (parallel or anti-parallel) and same radius; each group is one hole. The hole’s depth is the union of extents along the axis; if that depth is close to the part’s extent along the same axis, the hole is classified as **through**, otherwise **blind**. (Counterbore/countersink can be added later by inspecting adjacent faces.)
5. Each hole gets a stable ID (`hole_1`, …) and a list of contributing face IDs (`faces`).

---

## How pockets are detected

1. Again, all faces are explored.
2. If `BRepAdaptor_Surface::GetType() == GeomAbs_Plane`, the face is a planar “floor” candidate.
3. The face’s bounding box is used to compute a depth (extent along the plane normal). If that extent is above a small threshold, the face is reported as a pocket with that depth.
4. **Corner radius**: for each such planar face, edges are iterated with `TopExp_Explorer(..., TopAbs_EDGE)`. For each edge, `BRepAdaptor_Curve` is used; if `GetType() == GeomAbs_Circle`, the circle’s radius is taken. The minimum such radius is reported as `cornerRadius_mm` (0 if no circular edges).
5. Each pocket gets a stable ID (`pocket_1`, …) and its face ID(s) in `faces`.

---

## Why this architecture works for web + WeWeb

- **REST over HTTP**: Any client (browser, WeWeb, Postman, curl) can POST a STEP file and get JSON. No custom protocol.
- **Stateless**: Each request is independent; no server-side session. Easy to scale and run behind a load balancer.
- **Docker on Render**: Single container, no UI, listens on `PORT`. Render runs the container and routes traffic to it.
- **Stable IDs**: `face_*` and `hole_*` / `pocket_*` IDs are deterministic for the same STEP file, so a WeWeb UI can map clicks or selections to the same features across reloads or different views.
- **Units and meta**: All lengths in inches; `meta.kernel` and `meta.source` tell the client what engine and format were used.

This service is intended as the backend for a web-based STEP viewer and feature-interaction system: the UI can display the same IDs and show properties (e.g. hole diameter, pocket depth) from this JSON.
