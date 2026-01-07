#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "ElaWidgetTools" for configuration "Debug"
set_property(TARGET ElaWidgetTools APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(ElaWidgetTools PROPERTIES
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/ElaWidgetTools/lib/libElaWidgetTools.so"
  IMPORTED_SONAME_DEBUG "libElaWidgetTools.so"
  )

list(APPEND _IMPORT_CHECK_TARGETS ElaWidgetTools )
list(APPEND _IMPORT_CHECK_FILES_FOR_ElaWidgetTools "${_IMPORT_PREFIX}/ElaWidgetTools/lib/libElaWidgetTools.so" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
