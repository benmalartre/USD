#!/pxrpythonsubst
#
# Copyright 2018 Pixar
#
# Licensed under the Apache License, Version 2.0 (the "Apache License")
# with the following modification; you may not use this file except in
# compliance with the Apache License and the following modification to it:
# Section 6. Trademarks. is deleted and replaced with:
#
# 6. Trademarks. This License does not grant permission to use the trade
#    names, trademarks, service marks, or product names of the Licensor
#    and its affiliates, except as required to comply with Section 4(c) of
#    the License and to reproduce the content of the NOTICE file.
#
# You may obtain a copy of the Apache License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the Apache License with the above modification is
# distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied. See the Apache License for the specific
# language governing permissions and limitations under the Apache License.

"""
Common utilities that node-based parser plugins can use in their tests.

This is mostly focused on dealing with OSL and Args files. This may need to be
expanded/generalized to accommodate other types in the future.
"""

from __future__ import print_function

from pxr import Ndr
from pxr import Exec
from pxr.Sdf import ValueTypeNames as SdfTypes
from pxr import Tf


def IsNodeOSL(node):
    """
    Determines if the given node has an OSL source type.
    """

    return node.GetSourceType() == "OSL"

# XXX Maybe rename this to GetSdfType (as opposed to the Exec type)
def GetType(property):
    """
    Given a property (ExecProperty), return the SdfValueTypeName type.
    """
    sdfTypeIndicator = property.GetTypeAsSdfType()
    sdfValueTypeName = sdfTypeIndicator[0]
    tfType = sdfValueTypeName.type

    return tfType

def TestBasicProperties(node):
    """
    Test the correctness of the properties on the specified node (only the
    non-shading-specific aspects).
    """

    isOSL = IsNodeOSL(node)

    # Data that varies between OSL/Args
    # --------------------------------------------------------------------------
    metadata = {
        "widget": "number",
        "label": "inputA label",
        "page": "inputs1",
        "help": "inputA help message",
        "uncategorized": "1"
    }

    if not isOSL:
        metadata["name"] = "inputA"
        metadata["default"] = "0.0"
        metadata["type"] = "float"
    # --------------------------------------------------------------------------


    properties = {
        "inputA": node.GetInput("inputA"),
        "inputB": node.GetInput("inputB"),
        "inputC": node.GetInput("inputC"),
        "inputD": node.GetInput("inputD"),
        "inputF2": node.GetInput("inputF2"),
        "inputStrArray": node.GetInput("inputStrArray"),
        "resultF": node.GetOutput("resultF"),
        "resultI": node.GetOutput("resultI"),
    }

    assert properties["inputA"].GetName() == "inputA"
    assert properties["inputA"].GetType() == "float"
    assert properties["inputA"].GetDefaultValue() == 0.0
    assert not properties["inputA"].IsOutput()
    assert not properties["inputA"].IsArray()
    assert not properties["inputA"].IsDynamicArray()
    assert properties["inputA"].GetArraySize() == 0
    assert properties["inputA"].GetInfoString() == "inputA (type: 'float'); input"
    assert properties["inputA"].IsConnectable()
    assert properties["inputA"].CanConnectTo(properties["resultF"])
    assert not properties["inputA"].CanConnectTo(properties["resultI"])
    assert properties["inputA"].GetMetadata() == metadata

    # --------------------------------------------------------------------------
    # Check some array variations
    # --------------------------------------------------------------------------
    assert properties["inputD"].IsDynamicArray()
    assert properties["inputD"].GetArraySize() == 1 if isOSL else -1
    assert properties["inputD"].IsArray()
    assert list(properties["inputD"].GetDefaultValue()) == [1]

    assert not properties["inputF2"].IsDynamicArray()
    assert properties["inputF2"].GetArraySize() == 2
    assert properties["inputF2"].IsArray()
    assert not properties["inputF2"].IsConnectable()
    assert list(properties["inputF2"].GetDefaultValue()) == [1.0, 2.0]

    assert properties["inputStrArray"].GetArraySize() == 4
    assert list(properties["inputStrArray"].GetDefaultValue()) == \
        ["test", "string", "array", "values"]


def TestShadingProperties(node):
    """
    Test the correctness of the properties on the specified node (only the
    shading-specific aspects).
    """

    isOSL = IsNodeOSL(node)

    properties = {
        "inputA": node.GetExecInput("inputA"),
        "inputB": node.GetExecInput("inputB"),
        "inputC": node.GetExecInput("inputC"),
        "inputD": node.GetExecInput("inputD"),
        "inputF2": node.GetExecInput("inputF2"),
        "inputF3": node.GetExecInput("inputF3"),
        "inputF4": node.GetExecInput("inputF4"),
        "inputF5": node.GetExecInput("inputF5"),
        "inputInterp": node.GetExecInput("inputInterp"),
        "inputOptions": node.GetExecInput("inputOptions"),
        "inputPoint": node.GetExecInput("inputPoint"),
        "inputNormal": node.GetExecInput("inputNormal"),
        "inputStruct": node.GetExecInput("inputStruct"),
        "inputAssetIdentifier": node.GetExecInput("inputAssetIdentifier"),
        "resultF": node.GetExecOutput("resultF"),
        "resultF2": node.GetExecOutput("resultF2"),
        "resultF3": node.GetExecOutput("resultF3"),
        "resultI": node.GetExecOutput("resultI"),
        "vstruct1": node.GetExecOutput("vstruct1"),
        "vstruct1_bump": node.GetExecOutput("vstruct1_bump"),
        "outputPoint": node.GetExecOutput("outputPoint"),
        "outputNormal": node.GetExecOutput("outputNormal"),
        "outputColor": node.GetExecOutput("outputColor"),
        "outputVector": node.GetExecOutput("outputVector"),
    }

    assert properties["inputA"].GetLabel() == "inputA label"
    assert properties["inputA"].GetHelp() == "inputA help message"
    assert properties["inputA"].GetHints() == {
        "uncategorized": "1"
    }
    assert properties["inputA"].GetOptions() == []
    assert properties["inputA"].IsConnectable()
    assert properties["inputA"].GetValidConnectionTypes() == []
    assert properties["inputA"].CanConnectTo(properties["resultF"])
    assert not properties["inputA"].CanConnectTo(properties["resultI"])

    # --------------------------------------------------------------------------
    # Check correct options parsing
    # --------------------------------------------------------------------------
    assert set(properties["inputOptions"].GetOptions()) == {
        ("opt1", "opt1val"),
        ("opt2", "opt2val")
    }

    assert set(properties["inputInterp"].GetOptions()) == {
        ("linear", ""),
        ("catmull-rom", ""),
        ("bspline", ""),
        ("constant", ""),
    }

    # --------------------------------------------------------------------------
    # Check cross-type connection allowances
    # --------------------------------------------------------------------------
    assert properties["inputPoint"].CanConnectTo(properties["outputNormal"])
    assert properties["inputNormal"].CanConnectTo(properties["outputPoint"])
    assert properties["inputNormal"].CanConnectTo(properties["outputColor"])
    assert properties["inputNormal"].CanConnectTo(properties["outputVector"])
    assert properties["inputNormal"].CanConnectTo(properties["resultF3"])
    assert properties["inputF2"].CanConnectTo(properties["resultF2"])
    assert properties["inputD"].CanConnectTo(properties["resultI"])
    assert not properties["inputNormal"].CanConnectTo(properties["resultF2"])
    assert not properties["inputF4"].CanConnectTo(properties["resultF2"])
    assert not properties["inputF2"].CanConnectTo(properties["resultF3"])

    # --------------------------------------------------------------------------
    # Check clean and unclean mappings to Sdf types
    # --------------------------------------------------------------------------
    assert properties["inputB"].GetTypeAsSdfType() == (SdfTypes.Int, "")
    assert properties["inputF2"].GetTypeAsSdfType() == (SdfTypes.Float2, "")
    assert properties["inputF3"].GetTypeAsSdfType() == (SdfTypes.Float3, "")
    assert properties["inputF4"].GetTypeAsSdfType() == (SdfTypes.Float4, "")
    assert properties["inputF5"].GetTypeAsSdfType() == (SdfTypes.FloatArray, "")
    assert properties["inputStruct"].GetTypeAsSdfType() == \
           (SdfTypes.Token, Exec.PropertyTypes.Struct)



def TestBasicNode(node, nodeSourceType, nodeDefinitionURI, nodeImplementationURI):
    """
    Test basic, non-node-specific correctness on the specified node.
    """

    # Implementation notes:
    # ---
    # The source type needs to be passed manually in order to ensure the utils
    # do not have a dependency on the plugin (where the source type resides).
    # The URIs are manually specified because the utils cannot know ahead of
    # time where the node originated.

    isOSL = IsNodeOSL(node)
    nodeContext = "OSL" if isOSL else "pattern"

    # Data that varies between OSL/Args
    # --------------------------------------------------------------------------
    nodeName = "TestNodeOSL" if isOSL else "TestNodeARGS"
    numOutputs = 8 if isOSL else 10
    outputNames = {
        "resultF", "resultF2", "resultF3", "resultI", "outputPoint",
        "outputNormal", "outputColor", "outputVector"
    }
    metadata = {
        "category": "testing",
        "departments": "testDept",
        "help": "This is the test node",
        "label": "TestNodeLabel",
        "primvars": "primvar1|primvar2|primvar3|$primvarNamingProperty|"
                    "$invalidPrimvarNamingProperty",
        "uncategorizedMetadata": "uncategorized"
    }

    if not isOSL:
        metadata.pop("category")
        metadata.pop("label")
        metadata.pop("uncategorizedMetadata")

        outputNames.add("vstruct1")
        outputNames.add("vstruct1_bump")
    # --------------------------------------------------------------------------

    nodeInputs = {propertyName: node.GetExecInput(propertyName)
                  for propertyName in node.GetInputNames()}

    nodeOutputs = {propertyName: node.GetExecOutput(propertyName)
                   for propertyName in node.GetOutputNames()}

    assert node.GetName() == nodeName
    assert node.GetContext() == nodeContext
    assert node.GetSourceType() == nodeSourceType
    assert node.GetFamily() == ""
    assert node.GetResolvedDefinitionURI() == nodeDefinitionURI
    assert node.GetResolvedImplementationURI() == nodeImplementationURI
    assert node.IsValid()
    assert len(nodeInputs) == 17
    assert len(nodeOutputs) == numOutputs
    assert nodeInputs["inputA"] is not None
    assert nodeInputs["inputB"] is not None
    assert nodeInputs["inputC"] is not None
    assert nodeInputs["inputD"] is not None
    assert nodeInputs["inputF2"] is not None
    assert nodeInputs["inputF3"] is not None
    assert nodeInputs["inputF4"] is not None
    assert nodeInputs["inputF5"] is not None
    assert nodeInputs["inputInterp"] is not None
    assert nodeInputs["inputOptions"] is not None
    assert nodeInputs["inputPoint"] is not None
    assert nodeInputs["inputNormal"] is not None
    assert nodeOutputs["resultF2"] is not None
    assert nodeOutputs["resultI"] is not None
    assert nodeOutputs["outputPoint"] is not None
    assert nodeOutputs["outputNormal"] is not None
    assert nodeOutputs["outputColor"] is not None
    assert nodeOutputs["outputVector"] is not None
    print(set(node.GetInputNames()))
    assert set(node.GetInputNames()) == {
        "inputA", "inputB", "inputC", "inputD", "inputF2", "inputF3", "inputF4",
        "inputF5", "inputInterp", "inputOptions", "inputPoint", "inputNormal",
        "inputStruct", "inputAssetIdentifier", "primvarNamingProperty",
        "invalidPrimvarNamingProperty", "inputStrArray"
    }
    assert set(node.GetOutputNames()) == outputNames

    # There may be additional metadata passed in via the NdrNodeDiscoveryResult.
    # So, ensure that the bits we expect to see are there instead of doing 
    # an equality check.
    nodeMetadata = node.GetMetadata()
    for i,j in metadata.items():
        assert i in nodeMetadata
        assert nodeMetadata[i] == metadata[i]

    # Test basic property correctness
    TestBasicProperties(node)


def TestExecSpecificNode(node):
    """
    Test exec-specific correctness on the specified node.
    """

    isOSL = IsNodeOSL(node)

    # Data that varies between OSL/Args
    # --------------------------------------------------------------------------
    numOutputs = 8 if isOSL else 10
    label = "TestNodeLabel" if isOSL else ""
    category = "testing" if isOSL else ""
    vstructNames = [] if isOSL else ["vstruct1"]
    pages = {"", "inputs1", "inputs2", "results"} if isOSL else \
            {"", "inputs1", "inputs2", "results"}
    # --------------------------------------------------------------------------


    nodeInputs = {propertyName: node.GetExecInput(propertyName)
                    for propertyName in node.GetInputNames()}

    nodeOutputs = {propertyName: node.GetExecOutput(propertyName)
                     for propertyName in node.GetOutputNames()}

    assert len(nodeInputs) == 17
    assert len(nodeOutputs) == numOutputs
    assert nodeInputs["inputA"] is not None
    assert nodeInputs["inputB"] is not None
    assert nodeInputs["inputC"] is not None
    assert nodeInputs["inputD"] is not None
    assert nodeInputs["inputF2"] is not None
    assert nodeInputs["inputF3"] is not None
    assert nodeInputs["inputF4"] is not None
    assert nodeInputs["inputF5"] is not None
    assert nodeInputs["inputInterp"] is not None
    assert nodeInputs["inputOptions"] is not None
    assert nodeInputs["inputPoint"] is not None
    assert nodeInputs["inputNormal"] is not None
    assert nodeOutputs["resultF"] is not None
    assert nodeOutputs["resultF2"] is not None
    assert nodeOutputs["resultF3"] is not None
    assert nodeOutputs["resultI"] is not None
    assert nodeOutputs["outputPoint"] is not None
    assert nodeOutputs["outputNormal"] is not None
    assert nodeOutputs["outputColor"] is not None
    assert nodeOutputs["outputVector"] is not None
    assert node.GetLabel() == label
    assert node.GetCategory() == category
    assert node.GetHelp() == "This is the test node"
    assert set(node.GetPrimvars()) == {"primvar1", "primvar2", "primvar3"}


    # Test shading-specific property correctness
    TestShadingProperties(node)


def TestExecPropertiesNode(node):
    """
    Tests property correctness on the specified node, which must be
    one of the following pre-defined nodes:
    * 'TestExecPropertiesNodeOSL'
    * 'TestExecPropertiesNodeARGS'
    * 'TestExecPropertiesNodeUSD'
    These pre-defined nodes have a property of every type that Exec supports.

    Property correctness is defined as:
    * The node property has the expected ExecPropertyType
    * The node property has the expected SdfValueTypeName
    * If the node property has a default value, the default value's type
      matches the node property's type
    """
    # This test should only be run on the following allowed node names
    # --------------------------------------------------------------------------
    allowedNodeNames = ["TestExecPropertiesNodeOSL",
                        "TestExecPropertiesNodeARGS",
                        "TestExecPropertiesNodeUSD"]

    # If this assertion on the name fails, then this test was called with the
    # wrong node.
    assert node.GetName() in allowedNodeNames

    # If we have the correct node name, double check that the source type is
    # also correct
    if node.GetName() == "TestExecPropertiesNodeOSL":
        assert node.GetSourceType() == "OSL"
    elif node.GetName() == "TestExecPropertiesNodeARGS":
        assert node.GetSourceType() == "RmanCpp"
    elif node.GetName() == "TestExecPropertiesNodeUSD":
        assert node.GetSourceType() == "glslfx"

    nodeInputs = {propertyName: node.GetExecInput(propertyName)
                  for propertyName in node.GetInputNames()}

    nodeOutputs = {propertyName: node.GetExecOutput(propertyName)
                  for propertyName in node.GetOutputNames()}

    # For each property, we test that:
    # * The property has the expected ExecPropertyType
    # * The property has the expected TfType (from SdfValueTypeName)
    # * The property's type and default value's type match

    property = nodeInputs["inputInt"]
    assert property.GetType() == Exec.PropertyTypes.Int
    assert GetType(property) == Tf.Type.FindByName("int")
    assert Ndr._ValidateProperty(node, property)

    property = nodeInputs["inputString"]
    assert property.GetType() == Exec.PropertyTypes.String
    assert GetType(property) == Tf.Type.FindByName("string")
    assert Ndr._ValidateProperty(node, property)

    property = nodeInputs["inputFloat"]
    assert property.GetType() == Exec.PropertyTypes.Float
    assert GetType(property) == Tf.Type.FindByName("float")
    assert Ndr._ValidateProperty(node, property)

    property = nodeInputs["inputColor"]
    assert property.GetType() == Exec.PropertyTypes.Color
    assert GetType(property) == Tf.Type.FindByName("GfVec3f")
    assert Ndr._ValidateProperty(node, property)

    property = nodeInputs["inputPoint"]
    assert property.GetType() == Exec.PropertyTypes.Point
    assert GetType(property) == Tf.Type.FindByName("GfVec3f")
    assert Ndr._ValidateProperty(node, property)

    property = nodeInputs["inputNormal"]
    assert property.GetType() == Exec.PropertyTypes.Normal
    assert GetType(property) == Tf.Type.FindByName("GfVec3f")
    assert Ndr._ValidateProperty(node, property)

    property = nodeInputs["inputVector"]
    assert property.GetType() == Exec.PropertyTypes.Vector
    assert GetType(property) == Tf.Type.FindByName("GfVec3f")
    assert Ndr._ValidateProperty(node, property)

    property = nodeInputs["inputMatrix"]
    assert property.GetType() == Exec.PropertyTypes.Matrix
    assert GetType(property) == Tf.Type.FindByName("GfMatrix4d")
    assert Ndr._ValidateProperty(node, property)

    if node.GetName() != "TestExecPropertiesNodeUSD":
        # XXX Note that 'struct' and 'vstruct' types are currently unsupported
        # by the UsdExecNodeDefParserPlugin, which parses nodes defined in
        # usd files. Please see UsdExecNodeDefParserPlugin implementation for
        # details.
        property = nodeInputs["inputStruct"]
        assert property.GetType() == Exec.PropertyTypes.Struct
        assert GetType(property) == Tf.Type.FindByName("TfToken")
        assert Ndr._ValidateProperty(node, property)

    property = nodeInputs["inputIntArray"]
    assert property.GetType() == Exec.PropertyTypes.Int
    assert GetType(property) == Tf.Type.FindByName("VtArray<int>")
    assert Ndr._ValidateProperty(node, property)

    property = nodeInputs["inputStringArray"]
    assert property.GetType() == Exec.PropertyTypes.String
    assert GetType(property) == Tf.Type.FindByName("VtArray<string>")
    assert Ndr._ValidateProperty(node, property)

    property = nodeInputs["inputFloatArray"]
    assert property.GetType() == Exec.PropertyTypes.Float
    assert GetType(property) == Tf.Type.FindByName("VtArray<float>")
    assert Ndr._ValidateProperty(node, property)

    property = nodeInputs["inputColorArray"]
    assert property.GetType() == Exec.PropertyTypes.Color
    assert GetType(property) ==  Tf.Type.FindByName("VtArray<GfVec3f>")
    assert Ndr._ValidateProperty(node, property)

    property = nodeInputs["inputPointArray"]
    assert property.GetType() == Exec.PropertyTypes.Point
    assert GetType(property) == Tf.Type.FindByName("VtArray<GfVec3f>")
    assert Ndr._ValidateProperty(node, property)

    property = nodeInputs["inputNormalArray"]
    assert property.GetType() == Exec.PropertyTypes.Normal
    assert GetType(property) == Tf.Type.FindByName("VtArray<GfVec3f>")
    assert Ndr._ValidateProperty(node, property)

    property = nodeInputs["inputVectorArray"]
    assert property.GetType() == Exec.PropertyTypes.Vector
    assert GetType(property) == Tf.Type.FindByName("VtArray<GfVec3f>")
    assert Ndr._ValidateProperty(node, property)

    property = nodeInputs["inputMatrixArray"]
    assert property.GetType() == Exec.PropertyTypes.Matrix
    assert GetType(property) == Tf.Type.FindByName("VtArray<GfMatrix4d>")
    assert Ndr._ValidateProperty(node, property)

    property = nodeInputs["inputFloat2"]
    assert property.GetType() == Exec.PropertyTypes.Float
    assert GetType(property) == Tf.Type.FindByName("GfVec2f")
    assert Ndr._ValidateProperty(node, property)

    property = nodeInputs["inputFloat3"]
    assert property.GetType() == Exec.PropertyTypes.Float
    assert GetType(property) == Tf.Type.FindByName("GfVec3f")
    assert Ndr._ValidateProperty(node, property)

    property = nodeInputs["inputFloat4"]
    assert property.GetType() == Exec.PropertyTypes.Float
    assert GetType(property) == Tf.Type.FindByName("GfVec4f")
    assert Ndr._ValidateProperty(node, property)

    property = nodeInputs["inputAsset"]
    assert property.GetType() == Exec.PropertyTypes.String
    assert GetType(property) == Tf.Type.FindByName("SdfAssetPath")
    assert Ndr._ValidateProperty(node, property)

    property = nodeInputs["inputAssetArray"]
    assert property.GetType() == Exec.PropertyTypes.String
    assert GetType(property) == Tf.Type.FindByName("VtArray<SdfAssetPath>")
    assert Ndr._ValidateProperty(node, property)

    property = nodeInputs["inputColorRoleNone"]
    assert property.GetType() == Exec.PropertyTypes.Float
    assert GetType(property) == Tf.Type.FindByName("GfVec3f")
    assert Ndr._ValidateProperty(node, property)

    property = nodeInputs["inputPointRoleNone"]
    assert property.GetType() == Exec.PropertyTypes.Float
    assert GetType(property) == Tf.Type.FindByName("GfVec3f")
    assert Ndr._ValidateProperty(node, property)

    property = nodeInputs["inputNormalRoleNone"]
    assert property.GetType() == Exec.PropertyTypes.Float
    assert GetType(property) == Tf.Type.FindByName("GfVec3f")
    assert Ndr._ValidateProperty(node, property)

    property = nodeInputs["inputVectorRoleNone"]
    assert property.GetType() == Exec.PropertyTypes.Float
    assert GetType(property) == Tf.Type.FindByName("GfVec3f")
    assert Ndr._ValidateProperty(node, property)

    property = nodeOutputs["outputSurface"]
    assert property.GetType() == Exec.PropertyTypes.Terminal
    assert GetType(property) == Tf.Type.FindByName("TfToken")
    assert Ndr._ValidateProperty(node, property)

