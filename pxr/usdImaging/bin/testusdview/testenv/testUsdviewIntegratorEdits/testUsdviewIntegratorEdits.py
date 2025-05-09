#!/pxrpythonsubst
#
# Copyright 2023 Pixar
#
# Licensed under the terms set forth in the LICENSE.txt file available at
# https://openusd.org/license.
#
from pxr import UsdShade

# Remove any unwanted visuals from the view, and enable autoClip
def _modifySettings(appController):
    appController._dataModel.viewSettings.showBBoxes = False
    appController._dataModel.viewSettings.showHUD = False
    appController._dataModel.viewSettings.autoComputeClippingPlanes = True

def _updateIntegratorTarget(integratorPath, appController):
    stage = appController._dataModel.stage
    layer = stage.GetSessionLayer()
    stage.SetEditTarget(layer)

    renderSettings = stage.GetPrimAtPath('/Render/RenderSettings')
    integratorRel = renderSettings.GetRelationship('ri:integrator')
    integratorRel.SetTargets([integratorPath])

def _updateIntegratorParam(integratorPath, attrName, attrValue, appController):
    stage = appController._dataModel.stage
    layer = stage.GetSessionLayer()
    stage.SetEditTarget(layer)

    integrator = stage.GetPrimAtPath(integratorPath)
    integratorParam = integrator.GetAttribute(attrName)
    integratorParam.Set(attrValue)


# Test changing the connected Integrator and Integrator attribute.
def testUsdviewInputFunction(appController):
    _modifySettings(appController)

    pathTracerIntegrator = '/Render/PathTracerIntegrator'
    allowCausticsAttrName = "inputs:ri:allowCaustics"

    appController._takeShot("directLighting.png", waitForConvergence=True)

    _updateIntegratorTarget(pathTracerIntegrator, appController)
    appController._takeShot("pathTracer.png", waitForConvergence=True)

    _updateIntegratorParam(pathTracerIntegrator, allowCausticsAttrName, False, appController)
    appController._takeShot("pathTracer_modified.png", waitForConvergence=True)
