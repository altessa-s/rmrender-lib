# Copyright 2026 ALTESSA SOLUTIONS INC.
# Use of this source code is governed by an MIT-style license that can be
# found in the LICENSE file.
# PDFium is supplied explicitly by the consumer; no network downloads at configure time.
find_path(PDFium_INCLUDE_DIR fpdfview.h HINTS ${PDFium_ROOT} ENV PDFium_ROOT PATH_SUFFIXES include)
find_library(PDFium_LIBRARY NAMES pdfium HINTS ${PDFium_ROOT} ENV PDFium_ROOT PATH_SUFFIXES lib lib64)
set(_pdfium_arguments REQUIRED_VARS PDFium_LIBRARY PDFium_INCLUDE_DIR)
if(WIN32)
    if(NOT PDFium_LIBRARY)
        find_file(PDFium_LIBRARY NAMES pdfium.dll.lib HINTS ${PDFium_ROOT} ENV PDFium_ROOT
            PATH_SUFFIXES lib)
    endif()
    # A Windows SDK needs both the import library and pdfium.dll; without the
    # DLL the package is reported as not found instead of failing configure.
    find_file(PDFium_RUNTIME NAMES pdfium.dll HINTS ${PDFium_ROOT} ENV PDFium_ROOT
        PATH_SUFFIXES bin lib)
    list(APPEND _pdfium_arguments PDFium_RUNTIME REASON_FAILURE_MESSAGE
        "a Windows PDFium SDK must include pdfium.dll and its import library")
endif()
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(PDFium ${_pdfium_arguments})
if(PDFium_FOUND AND NOT TARGET PDFium::PDFium)
    set(_pdfium_location "${PDFium_LIBRARY}")
    if(WIN32)
        set(_pdfium_location "${PDFium_RUNTIME}")
        add_library(PDFium::PDFium SHARED IMPORTED)
        set_target_properties(PDFium::PDFium PROPERTIES IMPORTED_IMPLIB "${PDFium_LIBRARY}")
    elseif(APPLE AND PDFium_LIBRARY MATCHES "\\.dylib$")
        add_library(PDFium::PDFium SHARED IMPORTED)
        set_target_properties(PDFium::PDFium PROPERTIES IMPORTED_SONAME "@rpath/libpdfium.dylib")
    else()
        add_library(PDFium::PDFium UNKNOWN IMPORTED)
    endif()
    set_target_properties(PDFium::PDFium PROPERTIES IMPORTED_LOCATION "${_pdfium_location}"
        INTERFACE_INCLUDE_DIRECTORIES "${PDFium_INCLUDE_DIR}")
endif()
mark_as_advanced(PDFium_INCLUDE_DIR PDFium_LIBRARY PDFium_RUNTIME)
