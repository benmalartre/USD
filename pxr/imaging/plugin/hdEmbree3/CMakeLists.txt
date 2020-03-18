set(PXR_PREFIX pxr/imaging)
set(PXR_PACKAGE hdEmbree3)

message("EMBREE3 INCLUDE DIR : ${EMBREE3_INCLUDE_DIR}")
message("EMBREE3 LIBRARY : ${EMBREE3_LIBRARY}")

if(NOT EMBREE3_FOUND)
    message(WARNING "Not building ${PXR_PACKAGE} because of missing dependency: Embree3")
    return()
endif()
if (NOT ${PXR_ENABLE_GL_SUPPORT})
    message(STATUS
        "Skipping ${PXR_PACKAGE} because PXR_ENABLE_GL_SUPPORT is OFF")
    return()
endif()

pxr_plugin(hdEmbree3
   LIBRARIES
        plug
        tf
        vt
        gf
        work
        hf
        hd
        hdx
        ${TBB_tbb_LIBRARY}
        ${EMBREE3_LIBRARY}

    INCLUDE_DIRS
        ${TBB_INCLUDE_DIRS}
        ${EMBREE3_INCLUDE_DIR}

    PUBLIC_CLASSES
        config
        geometry
        instancer
        mesh
        renderBuffer
        renderer
        rendererPlugin
        renderDelegate
        renderPass
        sampler

    PUBLIC_HEADERS
        context.h
        renderParam.h

    RESOURCE_FILES
        plugInfo.json
)

if (X11_FOUND OR APPLE)
pxr_build_test(testHdEmbree3
    LIBRARIES
        hdEmbree3
        garch
        arch
    CPPFILES
        testenv/testHdEmbree3.cpp
)
endif()
