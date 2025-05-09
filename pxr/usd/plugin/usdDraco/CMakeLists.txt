set(PXR_PREFIX pxr/usd)
set(PXR_PACKAGE usdDraco)

if(WIN32 AND NOT CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS)
  set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS 1)
  set(USD_DRACO_SET_WINDOWS_EXPORT 1)
endif()

pxr_plugin(${PXR_PACKAGE}
    LIBRARIES
        tf
        gf
        sdf
        usd
        usdGeom
        ${DRACO_LIBRARY}

    INCLUDE_DIRS
        ${DRACO_INCLUDES}

    CPPFILES
        attributeDescriptor.cpp
	attributeFactory.cpp
        exportTranslator.cpp
        fileFormat.cpp
        importTranslator.cpp
        writer.cpp

    PYMODULE_CPPFILES
        module.cpp
        wrapDraco.cpp

    PYMODULE_FILES
        __init__.py

    RESOURCE_FILES
        plugInfo.json

    DOXYGEN_FILES
        overview.dox
)

if(USD_DRACO_SET_WINDOWS_EXPORT)
  unset(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS)
endif()

