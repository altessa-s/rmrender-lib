# Copyright 2026 ALTESSA SOLUTIONS INC.
# Use of this source code is governed by an MIT-style license that can be
# found in the LICENSE file.
# PDFium is supplied explicitly by the consumer; no network downloads at configure time.
find_path(PDFium_INCLUDE_DIR fpdfview.h HINTS ${PDFium_ROOT} ENV PDFium_ROOT PATH_SUFFIXES include)
find_library(PDFium_LIBRARY NAMES pdfium HINTS ${PDFium_ROOT} ENV PDFium_ROOT PATH_SUFFIXES lib lib64)
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(PDFium REQUIRED_VARS PDFium_LIBRARY PDFium_INCLUDE_DIR)
if(PDFium_FOUND AND NOT TARGET PDFium::PDFium)
    if(APPLE AND PDFium_LIBRARY MATCHES "\\.dylib$")
        add_library(PDFium::PDFium SHARED IMPORTED)
        set_target_properties(PDFium::PDFium PROPERTIES IMPORTED_SONAME "@rpath/libpdfium.dylib")
    else()
        add_library(PDFium::PDFium UNKNOWN IMPORTED)
    endif()
    set_target_properties(PDFium::PDFium PROPERTIES IMPORTED_LOCATION "${PDFium_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${PDFium_INCLUDE_DIR}")
endif()
mark_as_advanced(PDFium_INCLUDE_DIR PDFium_LIBRARY)
