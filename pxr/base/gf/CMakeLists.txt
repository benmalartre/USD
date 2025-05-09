# Don't get the wrong half.h!
set(CMAKE_INCLUDE_CURRENT_DIR OFF)

set(PXR_PREFIX pxr/base)
set(PXR_PACKAGE gf)

pxr_library(gf
    LIBRARIES
        arch
        tf

    PUBLIC_CLASSES
        bbox3d
        camera
        color
        colorSpace
        dualQuatd
        dualQuatf
        dualQuath
        frustum
        gamma
        half
        homogeneous
        ilmbase_half
        interval
        line
        line2d
        lineSeg
        lineSeg2d
        math
        matrixData
        matrix2d
        matrix2f
        matrix3f
        matrix3d
        matrix4f
        matrix4d
        multiInterval
        plane
        quatd
        quatf
        quath
        quaternion
        range1d
        range1f
        range2d
        range2f
        range3d
        range3f
        ray
        rect2i
        rotation
        size2
        size3
        transform
        vec2d
        vec2f
        vec2h
        vec2i
        vec3d
        vec3f
        vec3h
        vec3i
        vec4d
        vec4f
        vec4h
        vec4i

    PYTHON_PUBLIC_CLASSES
        pyBufferUtils

    PUBLIC_HEADERS
        api.h
        declare.h
        ilmbase_halfLimits.h
        limits.h
        numericCast.h
        traits.h

    CPPFILES
        nc/nanocolor.c

    PRIVATE_CLASSES
        ostreamHelpers

    PRIVATE_HEADERS
        ilmbase_eLut.h
        ilmbase_toFloat.h
        nc/nanocolor.h

    PYMODULE_CPPFILES
        module.cpp
        wrapBBox3d.cpp
        wrapCamera.cpp
        wrapColor.cpp
        wrapColorSpace.cpp
        wrapDualQuatd.cpp
        wrapDualQuatf.cpp
        wrapDualQuath.cpp
        wrapFrustum.cpp
        wrapGamma.cpp
        wrapHalf.cpp
        wrapHomogeneous.cpp
        wrapInterval.cpp
        wrapLimits.cpp
        wrapLine.cpp
        wrapLineSeg.cpp
        wrapMath.cpp
        wrapMatrix2d.cpp
        wrapMatrix2f.cpp
        wrapMatrix3f.cpp
        wrapMatrix3d.cpp
        wrapMatrix4f.cpp
        wrapMatrix4d.cpp
        wrapMultiInterval.cpp
        wrapPlane.cpp
        wrapQuaternion.cpp
        wrapQuatd.cpp
        wrapQuatf.cpp
        wrapQuath.cpp
        wrapRange1d.cpp
        wrapRange1f.cpp
        wrapRange2d.cpp
        wrapRange2f.cpp
        wrapRange3d.cpp
        wrapRange3f.cpp
        wrapRay.cpp
        wrapRect2i.cpp
        wrapRotation.cpp
        wrapSize2.cpp
        wrapSize3.cpp
        wrapTransform.cpp
        wrapVec2d.cpp
        wrapVec2f.cpp
        wrapVec2h.cpp
        wrapVec2i.cpp
        wrapVec3d.cpp
        wrapVec3f.cpp
        wrapVec3h.cpp
        wrapVec3i.cpp
        wrapVec4d.cpp
        wrapVec4f.cpp
        wrapVec4h.cpp
        wrapVec4i.cpp

    PYMODULE_FILES
        __init__.py

    DOXYGEN_FILES
        overview.dox
)

# gfGenCode --validate ensures that the generated GfVec, GfMatrix, GfRange, etc.
# source files are up to date with the contents of the templates used to
# generate them. If not, a diff displaying the difference will be printed to
# stdout and the build will fail.
if (NOT ${PXR_VALIDATE_GENERATED_CODE})
    message(STATUS "Skipping validation of gf generated code because PXR_VALIDATE_GENERATED_CODE=OFF")
elseif(NOT JINJA2_FOUND)
    message(STATUS "Skipping validation of gf generated code because of missing dependency: jinja2")
else()
    add_custom_command(
        TARGET gf 
        PRE_BUILD 
        COMMENT "Validating gf generated code..."
        COMMAND
            ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/gfGenCode.py 
                --srcDir ${CMAKE_CURRENT_SOURCE_DIR}
                --validate 
    )
endif()
        
pxr_build_test(testGfHardToReach
    LIBRARIES
        gf
    CPPFILES
        testenv/testGfHardToReach.cpp
)

pxr_build_test(testGfColorCpp
    LIBRARIES
        gf
    CPPFILES
        testenv/testGfColorCpp.cpp
)

pxr_test_scripts(
    testenv/testGfBBox3d.py
    testenv/testGfCamera.py
    testenv/testGfColorPy.py
    testenv/testGfDecomposeRotation.py
    testenv/testGfDualQuaternion.py
    testenv/testGfFrustum.py
    testenv/testGfGamma.py
    testenv/testGfHomogeneous.py
    testenv/testGfInterval.py
    testenv/testGfLine.py
    testenv/testGfLineSeg.py
    testenv/testGfMath.py
    testenv/testGfMatrix.py
    testenv/testGfMultiInterval.py
    testenv/testGfPlane.py
    testenv/testGfQuaternion.py
    testenv/testGfRange.py
    testenv/testGfRay.py
    testenv/testGfRect2i.py
    testenv/testGfRotation.py
    testenv/testGfSize.py
    testenv/testGfTransform.py
    testenv/testGfVec.py
)

pxr_register_test(testGfBBox3d
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testGfBBox3d"
)
pxr_register_test(testGfColorPy
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testGfColorPy"
)
pxr_register_test(testGfColorCpp
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testGfColorCpp"
)
pxr_register_test(testGfDecomposeRotation
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testGfDecomposeRotation"
)
pxr_register_test(testGfDualQuaternion
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testGfDualQuaternion"
)
if (NOT APPLE)
    pxr_register_test(testGfFrustum
        PYTHON
        COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testGfFrustum"
    )
endif()
pxr_register_test(testGfGamma
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testGfGamma"
)
pxr_register_test(testGfHardToReach
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testGfHardToReach"
)
pxr_register_test(testGfHomogeneous
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testGfHomogeneous"
)
pxr_register_test(testGfInterval
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testGfInterval"
)
pxr_register_test(testGfMultiInterval
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testGfMultiInterval"
)
pxr_register_test(testGfLine
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testGfLine"
)
pxr_register_test(testGfLineSeg
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testGfLineSeg"
)
pxr_register_test(testGfMath
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testGfMath"
)
pxr_register_test(testGfMatrix
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testGfMatrix"
)
pxr_register_test(testGfPlane
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testGfPlane"
)
pxr_register_test(testGfQuaternion
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testGfQuaternion"
)
pxr_register_test(testGfRange
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testGfRange"
)
pxr_register_test(testGfRay
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testGfRay"
)
pxr_register_test(testGfRect2i
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testGfRect2i"
)
pxr_register_test(testGfRotation
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testGfRotation"
)
pxr_register_test(testGfSize
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testGfSize"
)
pxr_register_test(testGfTransform
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testGfTransform"
)
pxr_register_test(testGfVec
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testGfVec"
)
pxr_register_test(testGfCamera
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testGfCamera"
)
