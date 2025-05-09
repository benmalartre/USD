set(PXR_PREFIX pxr/usdImaging)
set(PXR_PACKAGE usdAppUtils)

if (NOT ${PXR_BUILD_GPU_SUPPORT})
    message(STATUS
        "Skipping ${PXR_PACKAGE} because PXR_BUILD_GPU_SUPPORT is OFF")
    return()
endif()

pxr_library(usdAppUtils
    LIBRARIES
        garch
        gf
        hio
        sdf
        tf
        usd
        usdGeom
        usdImagingGL

    PUBLIC_HEADERS
        api.h

    PUBLIC_CLASSES
        camera
        frameRecorder

    PYMODULE_CPPFILES
        module.cpp
        wrapCamera.cpp
        wrapFrameRecorder.cpp

    PYMODULE_FILES
        __init__.py
        cameraArgs.py
        colorArgs.py
        complexityArgs.py
        framesArgs.py
        rendererArgs.py

    DOXYGEN_FILES
        overview.dox
)

pxr_test_scripts(
    testenv/testUsdAppUtilsCamera.py
    testenv/testUsdAppUtilsCmdlineArgs.py
    testenv/testUsdAppUtilsComplexity.py
    testenv/testUsdAppUtilsFrameRecorder.py
)

pxr_install_test_dir(
    SRC testenv/CameraTest
    DEST testUsdAppUtilsCamera
)
pxr_register_test(testUsdAppUtilsCamera
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdAppUtilsCamera"
    EXPECTED_RETURN_CODE 0
)


pxr_install_test_dir(
    SRC testenv/CmdlineArgsTest
    DEST testUsdAppUtilsCmdlineArgs
)
pxr_register_test(testUsdAppUtilsCmdlineArgs
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdAppUtilsCmdlineArgs"
    EXPECTED_RETURN_CODE 0
)

pxr_register_test(testUsdAppUtilsComplexity
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdAppUtilsComplexity"
    EXPECTED_RETURN_CODE 0
)

pxr_install_test_dir(
    SRC testenv/FrameRecorderTest
    DEST testUsdAppUtilsFrameRecorder
)
if (NOT PXR_HEADLESS_TEST_MODE AND NOT WIN32 AND NOT APPLE)
    pxr_register_test(testUsdAppUtilsFrameRecorder
        PYTHON
        COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdAppUtilsFrameRecorder"
        EXPECTED_RETURN_CODE 0
    )
endif()
