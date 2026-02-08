# FindOpenCASCADE.cmake - Find OCCT (OpenCASCADE) when using Ubuntu/Debian libocct-* packages.
# Sets OpenCASCADE_INCLUDE_DIR and OpenCASCADE_LIBRARIES for STEP + BRep usage.

find_path(OpenCASCADE_INCLUDE_DIR NAMES Standard_Real.hxx
  PATHS /usr/include/opencascade /usr/include
  PATH_SUFFIXES opencascade
  DOC "OpenCASCADE include directory")

set(_OCCT_LIBS
  TKSTEP TKSTEP209 TKSTEPAttr TKSTEPBase TKXSBase
  TKBRep TKBO TKPrim TKG2d TKG3d TKMath TKernel TKGeomBase
  TKBnd TKG2d TKG3d TKGeomBase TKBO TKPrim
  TKOffset TKHLR TKTopAlgo TKShHealing TKGeomAlgo TKService
)
list(REMOVE_DUPLICATES _OCCT_LIBS)

set(OpenCASCADE_LIBRARIES "")
foreach(_lib ${_OCCT_LIBS})
  find_library(_OCCT_${_lib} NAMES ${_lib}
    PATHS /usr/lib /usr/lib/x86_64-linux-gnu /usr/lib/aarch64-linux-gnu
    DOC "OCCT library ${_lib}")
  if(_OCCT_${_lib})
    list(APPEND OpenCASCADE_LIBRARIES ${_OCCT_${_lib}})
  endif()
endforeach()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(OpenCASCADE
  REQUIRED_VARS OpenCASCADE_INCLUDE_DIR OpenCASCADE_LIBRARIES
  FAIL_MESSAGE "OpenCASCADE (OCCT) not found. Install libocct-*-dev packages.")
