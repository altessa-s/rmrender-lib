# PDFium is supplied explicitly by the consumer; no network downloads at configure time.
find_path(PDFium_INCLUDE_DIR fpdfview.h HINTS ${PDFium_ROOT} ENV PDFium_ROOT PATH_SUFFIXES include)
find_library(PDFium_LIBRARY NAMES pdfium HINTS ${PDFium_ROOT} ENV PDFium_ROOT PATH_SUFFIXES lib lib64)
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(PDFium REQUIRED_VARS PDFium_LIBRARY PDFium_INCLUDE_DIR)
if(PDFium_FOUND AND NOT TARGET PDFium::PDFium)
    add_library(PDFium::PDFium UNKNOWN IMPORTED)
    set_target_properties(PDFium::PDFium PROPERTIES IMPORTED_LOCATION "${PDFium_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${PDFium_INCLUDE_DIR}")
endif()
mark_as_advanced(PDFium_INCLUDE_DIR PDFium_LIBRARY)
