/*###############################################################################
#
# Copyright 2020 NVIDIA Corporation
#
# Permission is hereby granted, free of charge, to any person obtaining a copy of
# this software and associated documentation files (the "Software"), to deal in
# the Software without restriction, including without limitation the rights to
# use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
# the Software, and to permit persons to whom the Software is furnished to do so,
# subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
# FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
# COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
# IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
# CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#
###############################################################################*/

/*###############################################################################
# 
# This "helloworld" sample demonstrates how to:
#  * connect to an Omniverse server
#  * create a USD stage
#  * create a physics scene to define simulation parameters
#  * create a polygonal box and add it to the stage and make it a dynamic rigid
#  * create a cube and add it to the stage and make it a dynamic rigid
#  * create a quad and add it to the stage and make it a collider
#  * upload an MDL material and its textures to an Omniverse server
#  * bind an MDL and USD Preview Surface material to the box
#  * add a light to the stage
#  * move and rotate the box with live updates
#  * disconnect from an Omniverse server
#  * 
#  * optional stuff:
#  *  print verbose Omniverse logs
#  *  open an existing stage and find a mesh to do live edits
#
###############################################################################*/

#include "xformUtils.h"
#include "primUtils.h"
#include "OmniverseUsdLuxLightCompat.h"

#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <cassert>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <fstream>
#ifdef _WIN32
#include <conio.h>
#endif
#include <mutex>
#include <memory>
#include <map>
#include <condition_variable>

#include <OmniClient.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/usd/kind/registry.h>
#include <pxr/usd/usdUtils/pipeline.h>
#include <pxr/usd/usdUtils/sparseValueWriter.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usd/modelAPI.h>
#include <pxr/usd/usd/payloads.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/references.h>
#include <pxr/usd/usdGeom/primvar.h>
#include <pxr/usd/usdShade/input.h>
#include <pxr/usd/usdShade/output.h>
#include <pxr/usd/usdGeom/scope.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdGeom/cube.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/usdLux/distantLight.h>
#include <pxr/usd/usdLux/domeLight.h>
#include <pxr/usd/usdShade/shader.h>

// Physics includes, note that usdPhysics is now part of USD itself, 
// in newer USD versions these includes will be replaced by pxr official usdPhysics schema
#if PXR_VERSION > 2108
    #include <pxr/usd/usdPhysics/scene.h>
    #include <pxr/usd/usdPhysics/rigidBodyAPI.h>
    #include <pxr/usd/usdPhysics/collisionAPI.h>
    #include <pxr/usd/usdPhysics/meshCollisionAPI.h>
#else
    #include <usdPhysics/scene.h>
    #include <usdPhysics/rigidBodyAPI.h>
    #include <usdPhysics/collisionAPI.h>
    #include <usdPhysics/meshCollisionAPI.h>
#endif

#if PXR_VERSION >= 2208
#include <pxr/usd/usdGeom/primvarsAPI.h>
#endif

PXR_NAMESPACE_USING_DIRECTIVE

// Globals for Omniverse Connection and base Stage
static UsdStageRefPtr gStage;

// Omniverse logging is noisy, only enable it if verbose mode (-v)
static bool gOmniverseLoggingEnabled = false;

// Global for making the logging reasonable
static std::mutex gLogMutex;

static GfVec3d gDefaultRotation(0);
static GfVec3i gDefaultRotationOrder(0, 1, 2);
static GfVec3d gDefaultScale(1);

// Multiplatform array size
#define HW_ARRAY_COUNT(array) (sizeof(array) / sizeof(array[0]))

// Private tokens for building up SdfPaths. We recommend
// constructing SdfPaths via tokens, as there is a performance
// cost to constructing them directly via strings (effectively,
// a table lookup per path element). Similarly, any API which
// takes a token as input should use a predefined token
// rather than one created on the fly from a string.
TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    (DistantLight)
    (DomeLight)
    (Looks)
    (World)
    (Shader)
    (st)

    // These tokens will be reworked or replaced by the official MDL schema for USD.
    // https://developer.nvidia.com/usd/MDLschema
    (Material)
    ((_module, "module"))
    (name)
    (out)
    ((shaderId, "mdlMaterial"))
    (mdl)

    // Tokens used for USD Preview Surface
    (diffuseColor)
    (normal)
    (file)
    (result)
    (varname)
    (rgb)
    (sourceColorSpace)
    (raw)
    (scale)
    (bias)
    (surface)
    (PrimST)
    (UsdPreviewSurface)
    ((UsdShaderId, "UsdPreviewSurface"))
    ((PrimStShaderId, "UsdPrimvarReader_float2"))
    (UsdUVTexture)
);

static void OmniClientConnectionStatusCallbackImpl(void* userData, const char* url, OmniClientConnectionStatus status) noexcept
{
    // Let's just print this regardless
    {
        std::unique_lock<std::mutex> lk(gLogMutex);
        std::cout << "Connection Status: " << omniClientGetConnectionStatusString(status) << " [" << url << "]" << std::endl;
    }
    if (status == eOmniClientConnectionStatus_ConnectError)
    {
        // We shouldn't just exit here - we should clean up a bit, but we're going to do it anyway
        std::cout << "[ERROR] Failed connection, exiting." << std::endl;
        exit(-1);
    }
}

static void failNotify(const char *msg, const char *detail = nullptr, ...)
{
    std::unique_lock<std::mutex> lk(gLogMutex);

    fprintf(stderr, "%s\n", msg);
    if (detail != nullptr)
    {
        fprintf(stderr, "%s\n", detail);
    }
}

// Shut down Omniverse connection
static void shutdownOmniverse()
{
    // Calling this prior to shutdown ensures that all pending live updates complete.
    omniClientLiveWaitForPendingUpdates();

    // The stage is a sophisticated object that needs to be destroyed properly.  
    // Since gStage is a smart pointer we can just reset it
    gStage.Reset();

    // This will prevent "Core::unregister callback called after shutdown"
    omniClientSetLogCallback(nullptr);

    omniClientShutdown();
}

// Omniverse Log callback
static void logCallback(const char* threadName, const char* component, OmniClientLogLevel level, const char* message) noexcept
{
    std::unique_lock<std::mutex> lk(gLogMutex);
    if (gOmniverseLoggingEnabled)
    {
        puts(message);
    }
}

// Startup Omniverse 
static bool startOmniverse()
{
    // Register a function to be called whenever the library wants to print something to a log
    omniClientSetLogCallback(logCallback);

    // The default log level is "Info", set it to "Debug" to see all messages
    omniClientSetLogLevel(eOmniClientLogLevel_Debug);

    // Initialize the library and pass it the version constant defined in OmniClient.h
    // This allows the library to verify it was built with a compatible version. It will
    // return false if there is a version mismatch.
    if (!omniClientInitialize(kOmniClientVersion))
    {
        return false;
    }

    omniClientRegisterConnectionStatusCallback(nullptr, OmniClientConnectionStatusCallbackImpl);

    return true;
}

// Create a new connection for this model in Omniverse, returns the created stage URL
static std::string createOmniverseModel(const std::string& destinationPath, bool doLiveEdit)
{
    std::string stageUrl = destinationPath + "/helloworld" + (doLiveEdit ? ".live" : ".usd");

    // Delete the old version of this file on Omniverse and wait for the operation to complete
    {
        std::unique_lock<std::mutex> lk(gLogMutex);
        std::cout << "Waiting for " << stageUrl << " to delete... " << std::endl;
    }
    omniClientWait(omniClientDelete(stageUrl.c_str(), nullptr, nullptr));
    {
        std::unique_lock<std::mutex> lk(gLogMutex);
        std::cout << "finished" << std::endl;
    }

    // Create this file in Omniverse cleanly
    gStage = UsdStage::CreateNew(stageUrl);
    if (!gStage)
    {
        failNotify("Failure to create model in Omniverse", stageUrl.c_str());
        return std::string();
    }

    {
        std::unique_lock<std::mutex> lk(gLogMutex);
        std::cout << "New stage created: " << stageUrl << std::endl;
    }

    // Keep the model contained inside of "World", only need to do this once per model
    const SdfPath defaultPrimPath = SdfPath::AbsoluteRootPath().AppendChild(_tokens->World);
    UsdGeomXform defaultPrim = UsdGeomXform::Define(gStage, defaultPrimPath);

    // Set the /World prim as the default prim
    gStage->SetDefaultPrim(defaultPrim.GetPrim());

    // Set the default prim as an assembly to support using component references
    pxr::UsdModelAPI(defaultPrim).SetKind(pxr::KindTokens->assembly);

    // Always a good idea to declare your up-ness
    UsdGeomSetStageUpAxis(gStage, UsdGeomTokens->y);

    // For physics its important to set units!
    UsdGeomSetStageMetersPerUnit(gStage, 0.01);

    return stageUrl;
}

// This function will add a commented checkpoint to a file on Nucleus if:
//   The Nucleus server supports checkpoints
static void checkpointFile(const std::string& stageUrl, const char* comment)
{
    bool bCheckpointsSupported = false;
    omniClientWait(omniClientGetServerInfo(stageUrl.c_str(), &bCheckpointsSupported, 
        [](void* UserData, OmniClientResult Result, OmniClientServerInfo const * Info) noexcept
        {
            if (Result == eOmniClientResult_Ok && Info && UserData)
            {
                bool* bCheckpointsSupported = static_cast<bool*>(UserData);
                *bCheckpointsSupported = Info->checkpointsEnabled;
            }
        }));

    if (bCheckpointsSupported)
    {
        const bool bForceCheckpoint = true;
        omniClientWait(omniClientCreateCheckpoint(stageUrl.c_str(), comment, bForceCheckpoint, nullptr, 
        [](void* userData, OmniClientResult result, char const * checkpointQuery) noexcept
        {}));

        std::unique_lock<std::mutex> lk(gLogMutex);
        std::cout << "Adding checkpoint comment <" << comment << "> to stage <" << stageUrl <<">" << std::endl;
    }
}

static std::string getConnectedUsername(const char* stageUrl)
{
    // Get the username for the connection
    std::string userName("_none_");
    omniClientWait(omniClientGetServerInfo(stageUrl, &userName, [](void* userData, OmniClientResult result, struct OmniClientServerInfo const * info) noexcept
        {
            std::string* userName = static_cast<std::string*>(userData);
            if (userData && userName && info && info->username)
            {
                userName->assign(info->username);
            }
        }));

    return userName;
}

// Stage URL really only needs to contain the server in the URL.  eg. omniverse://ov-prod
static void printConnectedUsername(const std::string& stageUrl)
{
    // Get the username for the connection
    std::string userName("_none_");
    omniClientWait(omniClientGetServerInfo(stageUrl.c_str(), &userName, [](void* userData, OmniClientResult result, struct OmniClientServerInfo const * info) noexcept
        {
            std::string* userName = static_cast<std::string*>(userData);
            if (userData && userName && info && info->username)
            {
                userName->assign(info->username);
            }
        }));
    {
        std::unique_lock<std::mutex> lk(gLogMutex);
        std::cout << "Connected username: " << userName << std::endl;
    }
}

static void createPhysicsScene()
{
    const SdfPath& defaultPrimPath = gStage->GetDefaultPrim().GetPath();
    std::string sceneName("physicsScene");
    const SdfPath scenePrimPath = defaultPrimPath.AppendChild(TfToken(TfMakeValidIdentifier(sceneName)));

    // Create physics scene, note that we dont have to specify gravity
    // the default value is derived based on the scene up Axis and meters per unit.
    // Hence in this case the gravity would be (0.0, -981.0, 0.0) since we have
    // defined the Y up-axis and we are having a scene in centimeters.
    UsdPhysicsScene::Define(gStage, scenePrimPath);
}

static void enablePhysics(const UsdPrim& prim, bool dynamic)
{
    if (dynamic)
    {
        // Make the cube a physics rigid body dynamic
        UsdPhysicsRigidBodyAPI::Apply(prim);
    }

    // Add collision
    UsdPhysicsCollisionAPI collisionAPI = UsdPhysicsCollisionAPI::Apply(prim);
    if (!collisionAPI)
    {
        std::cout << "ERROR: Failed to apply UsdPhysics.CollisionAPI, check that the UsdPhysics plugin is located in the USD plugins folders" << std::endl;
    }

    if (prim.IsA<UsdGeomMesh>())
    {
        UsdPhysicsMeshCollisionAPI meshCollisionAPI = UsdPhysicsMeshCollisionAPI::Apply(prim);
        if (dynamic)
        {
            // set mesh approximation to convexHull for dynamic meshes
            meshCollisionAPI.CreateApproximationAttr().Set(UsdPhysicsTokens->convexHull);
        }
        else
        {
            // set mesh approximation to none - triangle mesh as is will be used
            meshCollisionAPI.CreateApproximationAttr().Set(UsdPhysicsTokens->none);
        }
    }
}

static void createDynamicCube(double size)
{
    // Create the geometry under the default prim
    const SdfPath& defaultPrimPath = gStage->GetDefaultPrim().GetPath();
    std::string cubeName("cube");
    const SdfPath cubePrimPath = defaultPrimPath.AppendChild(TfToken(TfMakeValidIdentifier(cubeName)));
    UsdGeomCube cube = UsdGeomCube::Define(gStage, cubePrimPath);

    if (!cube)
        return;

    // Move it up
    cube.AddTranslateOp(UsdGeomXformOp::PrecisionFloat).Set(GfVec3f(65.0f, 300.0f, 65.0f));

    cube.GetSizeAttr().Set(size);
    VtArray<GfVec3f> extent;
    cube.GetExtentAttr().Get(&extent);
    cube.CreateExtentAttr(VtValue(size * 0.5 * extent));

    enablePhysics(cube.GetPrim(), true);

    // Make the kind a component to support the assembly/component selection hierarchy
    pxr::UsdModelAPI(cube.GetPrim()).SetKind(pxr::KindTokens->component);

    // Commit the changes to the USD
    gStage->Save();
    omniClientLiveProcess();
}

// Create a simple quad in USD with normals and add a collider
static void createQuad(double size)
{
    // Create the geometry under the default prim
    const SdfPath& defaultPrimPath = gStage->GetDefaultPrim().GetPath();
    std::string quadName("quad");
    const SdfPath quadPrimPath = defaultPrimPath.AppendChild(TfToken(TfMakeValidIdentifier(quadName)));
    UsdGeomMesh mesh = UsdGeomMesh::Define(gStage, quadPrimPath);

    if (!mesh)
        return;

    // Set orientation
    mesh.CreateOrientationAttr(VtValue(UsdGeomTokens->rightHanded));

    // Add all of the vertices
    VtArray<GfVec3f> points = { 
        GfVec3f(-size, 0.0, -size),
        GfVec3f(-size, 0.0, size),
        GfVec3f(size, 0.0, size), 
        GfVec3f(size, 0.0, -size), 
    };
    mesh.CreatePointsAttr(VtValue(points));
    VtArray<GfVec3f> extent;
    mesh.ComputeExtent(points, &extent);
    mesh.CreateExtentAttr(VtValue(extent));

    // Calculate indices for each triangle
    VtArray<int> vecIndices = { 0, 1, 2, 3 };
    mesh.CreateFaceVertexIndicesAttr(VtValue(vecIndices));

    // Add vertex normals    
    VtArray<GfVec3f> meshNormals = { 
        GfVec3f(0.0, 1.0, 0.0),
        GfVec3f(0.0, 1.0, 0.0),
        GfVec3f(0.0, 1.0, 0.0), 
        GfVec3f(0.0, 1.0, 0.0) 
    };
    mesh.CreateNormalsAttr(VtValue(meshNormals));

    // Add face vertex count
    VtArray<int> faceVertexCounts = { 4 };
    mesh.CreateFaceVertexCountsAttr(VtValue(faceVertexCounts));

    // set is as a static triangle mesh
    enablePhysics(mesh.GetPrim(), false);

    // Make the kind a component to support the assembly/component selection hierarchy
    pxr::UsdModelAPI(mesh.GetPrim()).SetKind(pxr::KindTokens->component);

    // Commit the changes to the USD
    gStage->Save();
    omniClientLiveProcess();
}

// Create a simple box in USD with normals and UV information
double h = 50.0;
int gBoxVertexIndices[] = { 0, 1, 2, 1, 3, 2, 4, 5, 6, 4, 6, 7, 8, 9, 10, 8, 10, 11, 12, 13, 14, 12, 14, 15, 16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23 };
double gBoxNormals[][3] = { {0, 0, -1}, {0, 0, -1}, {0, 0, -1}, {0, 0, -1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, -1, 0}, {0, -1, 0}, {0, -1, 0}, {0, -1, 0}, {1, 0, 0}, {1, 0, 0}, {1, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {-1, 0, 0}, {-1, 0, 0}, {-1, 0, 0}, {-1, 0, 0} };
double gBoxPoints[][3] = { {h, -h, -h}, {-h, -h, -h}, {h, h, -h}, {-h, h, -h}, {h, h, h}, {-h, h, h}, {-h, -h, h}, {h, -h, h}, {h, -h, h}, {-h, -h, h}, {-h, -h, -h}, {h, -h, -h}, {h, h, h}, {h, -h, h}, {h, -h, -h}, {h, h, -h}, {-h, h, h}, {h, h, h}, {h, h, -h}, {-h, h, -h}, {-h, -h, h}, {-h, h, h}, {-h, h, -h}, {-h, -h, -h} };
float gBoxUV[][2] = { {0, 0}, {0, 1}, {1, 1}, {1, 0}, {0, 0}, {0, 1}, {1, 1}, {1, 0}, {0, 0}, {0, 1}, {1, 1}, {1, 0}, {0, 0}, {0, 1}, {1, 1}, {1, 0}, {0, 0}, {0, 1}, {1, 1}, {1, 0}, {0, 0}, {0, 1}, {1, 1}, {1, 0} };

static UsdGeomMesh createBox(int boxNumber=0)
{
    // Create the geometry under the default prim
    const SdfPath& defaultPrimPath = gStage->GetDefaultPrim().GetPath();
    std::string boxName("box-"); // Note that TfMakeValidIdentifier will change the hyphen to an underscore
    boxName.append(std::to_string(boxNumber));
    const SdfPath boxPrimPath = defaultPrimPath.AppendChild(TfToken(TfMakeValidIdentifier(boxName)));
    UsdGeomMesh mesh = UsdGeomMesh::Define(gStage, boxPrimPath);

    if (!mesh)
        return mesh;

    // Set orientation
    mesh.CreateOrientationAttr(VtValue(UsdGeomTokens->rightHanded));

    // Add all of the vertices
    int num_vertices = HW_ARRAY_COUNT(gBoxPoints);
    VtArray<GfVec3f> points;
    points.resize(num_vertices);
    for (int i = 0; i < num_vertices; i++)
    {
        points[i] = GfVec3f(gBoxPoints[i][0], gBoxPoints[i][1], gBoxPoints[i][2]);
    }
    mesh.CreatePointsAttr(VtValue(points));
    VtArray<GfVec3f> extent;
    mesh.ComputeExtent(points, &extent);
    mesh.CreateExtentAttr(VtValue(extent));

    // Calculate indices for each triangle
    int num_indices = HW_ARRAY_COUNT(gBoxVertexIndices); // 2 Triangles per face * 3 Vertices per Triangle * 6 Faces
    VtArray<int> vecIndices;
    vecIndices.resize(num_indices);
    for (int i = 0; i < num_indices; i++)
    {
        vecIndices[i] = gBoxVertexIndices[i];
    }
    mesh.CreateFaceVertexIndicesAttr(VtValue(vecIndices));

    // Add vertex normals
    int num_normals = HW_ARRAY_COUNT(gBoxNormals);
    VtArray<GfVec3f> meshNormals;
    meshNormals.resize(num_vertices);
    for (int i = 0; i < num_vertices; i++)
    {
        meshNormals[i] = GfVec3f((float)gBoxNormals[i][0], (float)gBoxNormals[i][1], (float)gBoxNormals[i][2]);
    }
    mesh.CreateNormalsAttr(VtValue(meshNormals));

    // Add face vertex count
    VtArray<int> faceVertexCounts;
    faceVertexCounts.resize(12); // 2 Triangles per face * 6 faces
    std::fill(faceVertexCounts.begin(), faceVertexCounts.end(), 3);
    mesh.CreateFaceVertexCountsAttr(VtValue(faceVertexCounts));

    // Set the color on the mesh
    UsdPrim meshPrim = mesh.GetPrim();
    UsdAttribute displayColorAttr = mesh.CreateDisplayColorAttr();
    {
        VtVec3fArray valueArray;
        GfVec3f rgbFace(0.463f, 0.725f, 0.0f);
        valueArray.push_back(rgbFace);
        displayColorAttr.Set(valueArray);
    }

    // Set the UV (st) values for this mesh
#if PXR_VERSION >= 2208
    pxr::UsdGeomPrimvarsAPI PrimvarsAPI(mesh.GetPrim());
    UsdGeomPrimvar attr2 = PrimvarsAPI.CreatePrimvar(_tokens->st, SdfValueTypeNames->TexCoord2fArray);
#else
    UsdGeomPrimvar attr2 = mesh.CreatePrimvar(_tokens->st, SdfValueTypeNames->TexCoord2fArray);
#endif
    {
        int uv_count = HW_ARRAY_COUNT(gBoxUV);
        VtVec2fArray valueArray;
        valueArray.resize(uv_count);
        for (int i = 0; i < uv_count; ++i)
        {
            valueArray[i].Set(gBoxUV[i]);
        }

        bool status = attr2.Set(valueArray);
    }
    attr2.SetInterpolation(UsdGeomTokens->vertex);

    // Move it up and rotate
    xformUtils::setLocalTransformSRT( meshPrim, /* translation */ GfVec3d(0.0f, 100.0f, 0.0f), /* rotation */ GfVec3d(20.0, 0.0, 20.0), gDefaultRotationOrder, gDefaultScale );

    // Make the cube a physics rigid body dynamic
    enablePhysics(mesh.GetPrim(), true);

    // Make the kind a component to support the assembly/component selection hierarchy
    pxr::UsdModelAPI(mesh.GetPrim()).SetKind(pxr::KindTokens->component);

    // Commit the changes to the USD
    gStage->Save();
    omniClientLiveProcess();

    return mesh;
}

// Opens an existing stage and finds the first UsdGeomMesh
static UsdGeomMesh findGeomMesh(const std::string& existingStage)
{
    // Open this file from Omniverse
    gStage = UsdStage::Open(existingStage);
    if (!gStage)
    {
        failNotify("Failure to open stage in Omniverse:", existingStage.c_str());
        return UsdGeomMesh();
    }

    {
        std::unique_lock<std::mutex> lk(gLogMutex);
        std::cout << "Existing stage opened: " << existingStage << std::endl;
    }

    if (UsdGeomTokens->y != UsdGeomGetStageUpAxis(gStage))
    {
        std::unique_lock<std::mutex> lk(gLogMutex);
        std::cout << "Stage is not Y-up so live xform edits will be incorrect.  Stage is " << UsdGeomGetStageUpAxis(gStage) << "-up" << std::endl;
    }

    // Traverse the stage and return the first UsdGeomMesh we find
    auto range = gStage->Traverse();
    for (const auto& node : range)
    {
        if (node.IsA<UsdGeomMesh>())
        {
            {
                std::unique_lock<std::mutex> lk(gLogMutex);
                std::cout << "Found UsdGeomMesh: " << node.GetName() << std::endl;
            }
            return UsdGeomMesh(node);
        }
    }

    // No UsdGeomMesh found in stage (what kind of stage is this anyway!?)
    std::cout << "ERROR: No UsdGeomMesh found in stage: " << existingStage << std::endl;
    return UsdGeomMesh();
}

// Upload a material and its textures to the Omniverse Server
static void uploadReferences(const std::string& destinationPath)
{
    std::string matPath = destinationPath + "/Materials";
    std::string propPath = destinationPath + "/Props";

    // Delete the old version of this folder on Omniverse and wait for the operation to complete, then upload
    omniClientWait(omniClientDelete(matPath.c_str(), nullptr, nullptr));
    omniClientWait(omniClientCopy("resources/Materials", matPath.c_str(), nullptr, nullptr));
    // Referenced Props
    omniClientWait(omniClientDelete(propPath.c_str(), nullptr, nullptr));
    omniClientWait(omniClientCopy("resources/Props", propPath.c_str(), nullptr, nullptr));
}

// Bind a material to this geometry
static void createMaterial(UsdGeomMesh meshIn)
{
    std::string materialName = "Fieldstone";
    const SdfPath& defaultPrimPath = gStage->GetDefaultPrim().GetPath();

    // Create a material instance for this in USD
    TfToken materialNameToken(TfMakeValidIdentifier(materialName));
    // Make path for "/Looks" scope under the default prim
    const SdfPath looksScopePath = defaultPrimPath.AppendChild(_tokens->Looks);
    UsdGeomScope::Define(gStage, looksScopePath);

    // Make path for "/Looks/Fieldstone" under the default prim
    SdfPath matPath = defaultPrimPath.AppendChild(_tokens->Looks).AppendChild(materialNameToken);
    UsdShadeMaterial newMat = UsdShadeMaterial::Define(gStage, matPath);

    // MDL Shader
    {
        // Create the MDL shader to bind to the material
        SdfAssetPath mdlShaderModule = SdfAssetPath("./Materials/Fieldstone.mdl");
        const SdfPath shaderPath = matPath.AppendChild(materialNameToken);
        UsdShadeShader mdlShader = UsdShadeShader::Define(gStage, shaderPath);
        mdlShader.CreateIdAttr(VtValue(_tokens->shaderId));

        // These attributes will be reworked or replaced in the official MDL schema for USD.
        // https://developer.nvidia.com/usd/MDLschema
        mdlShader.SetSourceAsset(mdlShaderModule, _tokens->mdl);
        mdlShader.GetPrim().CreateAttribute(TfToken("info:mdl:sourceAsset:subIdentifier"), SdfValueTypeNames->Token, false, SdfVariabilityUniform).Set(materialNameToken);

        // Set the linkage between material and MDL shader
        UsdShadeOutput mdlOutput = newMat.CreateSurfaceOutput(_tokens->mdl);
#if PXR_VERSION > 2102
        mdlOutput.ConnectToSource(mdlShader.ConnectableAPI(), _tokens->out);
#else
        mdlOutput.ConnectToSource(mdlShader, _tokens->out);
#endif
    }

    
    // USD Preview Surface Shaders
    {
        // Create the "USD Primvar reader for float2" shader
        SdfPath shaderPath = matPath.AppendChild(_tokens->PrimST);
        UsdShadeShader primStShader = UsdShadeShader::Define(gStage, shaderPath);
        primStShader.CreateIdAttr(VtValue(_tokens->PrimStShaderId));
        primStShader.CreateOutput(_tokens->result, SdfValueTypeNames->Float2);
        primStShader.CreateInput(_tokens->varname, SdfValueTypeNames->Token).Set(_tokens->st);

        // Create the "Diffuse Color Tex" shader
        std::string diffuseColorShaderName = materialName + "DiffuseColorTex";
        std::string diffuseFilePath = "./Materials/Fieldstone/Fieldstone_BaseColor.png";
        shaderPath = matPath.AppendChild(TfToken(diffuseColorShaderName));
        UsdShadeShader diffuseColorShader = UsdShadeShader::Define(gStage, shaderPath);
        diffuseColorShader.CreateIdAttr(VtValue(_tokens->UsdUVTexture));
        UsdShadeInput texInput = diffuseColorShader.CreateInput(_tokens->file, SdfValueTypeNames->Asset);
        texInput.Set(SdfAssetPath(diffuseFilePath));
        diffuseColorShader.CreateInput(_tokens->sourceColorSpace, SdfValueTypeNames->Token).Set(TfToken("auto"));
        diffuseColorShader.CreateInput(_tokens->st, SdfValueTypeNames->Float2).ConnectToSource(primStShader.CreateOutput(_tokens->result, SdfValueTypeNames->Float2));
        UsdShadeOutput diffuseColorShaderOutput = diffuseColorShader.CreateOutput(_tokens->rgb, SdfValueTypeNames->Float3);

        // Create the "Normal Tex" shader
        std::string normalShaderName = materialName + "NormalTex";
        std::string normalFilePath = "./Materials/Fieldstone/Fieldstone_N.png";
        shaderPath = matPath.AppendChild(TfToken(normalShaderName));
        UsdShadeShader normalShader = UsdShadeShader::Define(gStage, shaderPath);
        normalShader.CreateIdAttr(VtValue(_tokens->UsdUVTexture));
        UsdShadeInput normalTexInput = normalShader.CreateInput(_tokens->file, SdfValueTypeNames->Asset);
        normalTexInput.Set(SdfAssetPath(normalFilePath));
        normalShader.CreateInput(_tokens->scale, SdfValueTypeNames->Float4).Set(GfVec4f(2, 2, 2, 1));
        normalShader.CreateInput(_tokens->bias, SdfValueTypeNames->Float4).Set(GfVec4f(-1, -1, -1, 0));
        normalShader.CreateInput(_tokens->sourceColorSpace, SdfValueTypeNames->Token).Set(_tokens->raw);
        normalShader.CreateInput(_tokens->st, SdfValueTypeNames->Float2).ConnectToSource(primStShader.CreateOutput(_tokens->result, SdfValueTypeNames->Float2));
        UsdShadeOutput normalShaderOutput = normalShader.CreateOutput(_tokens->rgb, SdfValueTypeNames->Float3);

        // Create the USD Preview Surface shader
        std::string usdPreviewSurfaceShaderName = materialName + "PreviewSurface";
        shaderPath = matPath.AppendChild(TfToken(usdPreviewSurfaceShaderName));
        UsdShadeShader usdPreviewSurfaceShader = UsdShadeShader::Define(gStage, shaderPath);
        usdPreviewSurfaceShader.CreateIdAttr(VtValue(_tokens->UsdPreviewSurface));
        UsdShadeInput diffuseColorInput = usdPreviewSurfaceShader.CreateInput(_tokens->diffuseColor, SdfValueTypeNames->Color3f);
        diffuseColorInput.ConnectToSource(diffuseColorShaderOutput);
        UsdShadeInput normalInput = usdPreviewSurfaceShader.CreateInput(_tokens->normal, SdfValueTypeNames->Normal3f);
        normalInput.ConnectToSource(normalShaderOutput);

        // Set the linkage between material and USD Preview surface shader
        UsdShadeOutput usdPreviewSurfaceOutput = newMat.CreateSurfaceOutput();
#if PXR_VERSION > 2102
        usdPreviewSurfaceOutput.ConnectToSource(usdPreviewSurfaceShader.ConnectableAPI(), _tokens->surface);
#else
        usdPreviewSurfaceOutput.ConnectToSource(usdPreviewSurfaceShader, _tokens->surface);
#endif

    }

    // Final step, associate the material with the face
    UsdShadeMaterialBindingAPI::Apply(meshIn.GetPrim());
    UsdShadeMaterialBindingAPI usdMaterialBinding(meshIn);
    usdMaterialBinding.Bind(newMat);

    // Commit the changes to the USD
    gStage->Save();
    omniClientLiveProcess();
}

// Create references and modify an OmniPBR material
void createReferenceAndPayload()
{
    // The referenced prop is in /Props/Coaster/Coaster_Hexagon.usd
    const SdfPath& defaultPrimPath = gStage->GetDefaultPrim().GetPath();

    // Create a reference, enable physics and give it some angular velocity
    UsdGeomXform xformPrim = UsdGeomXform::Define(gStage, defaultPrimPath.AppendChild(TfToken("Coaster_Hexagon_Reference")));
    xformPrim.GetPrim().GetReferences().AddReference("./Props/Coaster/Coaster_Hexagon.usda");
    UsdPrim meshPrim = gStage->GetPrimAtPath(xformPrim.GetPrim().GetPath().AppendChild(TfToken("Coaster_Hexagon")));
    enablePhysics(meshPrim, true);
    UsdPhysicsRigidBodyAPI(meshPrim).CreateAngularVelocityAttr(VtValue(GfVec3f(0, 1000, 0)));
    xformUtils::setLocalTransformSRT(xformPrim.GetPrim(), GfVec3d(200, 100, -200), /* rotation */ GfVec3d(3, 0, 8), gDefaultRotationOrder, GfVec3d(10));

    // Create a payload, enable physics and give it some angular velocity
    xformPrim = UsdGeomXform::Define(gStage, defaultPrimPath.AppendChild(TfToken("Coaster_Hexagon_Payload")));
    xformPrim.GetPrim().GetPayloads().AddPayload("./Props/Coaster/Coaster_Hexagon.usda");
    meshPrim = gStage->GetPrimAtPath(xformPrim.GetPrim().GetPath().AppendChild(TfToken("Coaster_Hexagon")));
    enablePhysics(meshPrim, true);
    UsdPhysicsRigidBodyAPI(meshPrim).CreateAngularVelocityAttr(VtValue(GfVec3f(-1000, 0, 0)));
    xformUtils::setLocalTransformSRT(xformPrim.GetPrim(), GfVec3d(-200, 180, 200), /* rotation */ GfVec3d(-4, 90, 8), gDefaultRotationOrder, GfVec3d(10));

    // Modify the payload's material in Coaster_Hexagon/Looks/M_Coaster_Hexagon
    SdfPath materialPrimPath = defaultPrimPath.AppendChild(TfToken("Coaster_Hexagon_Payload"))
        .AppendChild(_tokens->Looks)
        .AppendChild(TfToken("M_Coaster_Hexagon"));
    UsdPrim materialPrim = gStage->GetPrimAtPath(materialPrimPath);
    primUtils::createMaterialInput(materialPrim, "diffuse_tint", VtValue(GfVec3f(1, 0.1, 0)), SdfValueTypeNames->Color3f);

    // This save will emit a warning about "Upgrading crate file", this is OK
    // Commit the changes to the USD
    gStage->Save();
}

// Create a light source in the scene.
static void createDistantLight()
{
    // Construct path for the light under the default prim
    const SdfPath& defaultPrimPath = gStage->GetDefaultPrim().GetPath();
    const SdfPath lightPath = defaultPrimPath.AppendChild(_tokens->DistantLight);
    UsdLuxDistantLight newLight = UsdLuxDistantLight::Define(gStage, lightPath);

    // Set the UsdLuxLight attributes.  Note the use of the compatibility class.
    // This class generates both the old and new UsdLuxLight schema values (new prepended with "inputs:")
    OmniverseUsdLuxLightCompat::CreateAngleAttr(newLight.GetPrim(), VtValue(0.53f));

    GfVec3f color(1.0f, 1.0f, 0.745f);
    OmniverseUsdLuxLightCompat::CreateColorAttr(newLight.GetPrim(), VtValue(color));
    OmniverseUsdLuxLightCompat::CreateIntensityAttr(newLight.GetPrim(), VtValue(500.0f));

    // Rotate the distant light
    xformUtils::setLocalTransformSRT(newLight.GetPrim(), GfVec3d(0.0f), /* rotation */ GfVec3d(139, 44, 190), gDefaultRotationOrder, gDefaultScale);

    // Make the kind a component to support the assembly/component selection hierarchy
    pxr::UsdModelAPI(newLight.GetPrim()).SetKind(pxr::KindTokens->component);

    // Commit the changes to the USD
    gStage->Save();
    omniClientLiveProcess();
}

// Create a light source in the scene.
static void createDomeLight(const std::string& texturePath)
{
    // Construct path for the light under the default prim
    const SdfPath& defaultPrimPath = gStage->GetDefaultPrim().GetPath();
    const SdfPath lightPath = defaultPrimPath.AppendChild(_tokens->DomeLight);
    UsdLuxDomeLight newLight = UsdLuxDomeLight::Define(gStage, lightPath);

    // Set the UsdLuxLight attributes.  Note the use of the compatibility class.
    // This class generates both the old and new UsdLuxLight schema values (new prepended with "inputs:")
    OmniverseUsdLuxLightCompat::CreateIntensityAttr(newLight.GetPrim(), VtValue(900.0f));
    OmniverseUsdLuxLightCompat::CreateTextureFileAttr(newLight.GetPrim(), VtValue(SdfAssetPath(texturePath)));
    OmniverseUsdLuxLightCompat::CreateTextureFormatAttr(newLight.GetPrim(), VtValue(UsdLuxTokens->latlong));

    // Set rotation on domelight
    UsdGeomXformable xForm = newLight;
    UsdGeomXformOp rotateOp;
    GfVec3d rotXYZ(270, 270, 0);
    rotateOp = xForm.AddXformOp(UsdGeomXformOp::TypeRotateXYZ, UsdGeomXformOp::Precision::PrecisionDouble);
    rotateOp.Set(rotXYZ);

    // Make the kind a component to support the assembly/component selection hierarchy
    pxr::UsdModelAPI(newLight.GetPrim()).SetKind(pxr::KindTokens->component);

    // Commit the changes to the USD
    gStage->Save();
    omniClientLiveProcess();
}

// Create an empty folder, just as an example.
static void createEmptyFolder(const std::string& emptyFolderPath)
{
    {
        std::unique_lock<std::mutex> lk(gLogMutex);
        std::cout << "Waiting to create a new folder: " << emptyFolderPath << " ... ";
    }

    OmniClientResult localResult;
    localResult = Count_eOmniClientResult;

    omniClientWait(omniClientCreateFolder(emptyFolderPath.c_str(), &localResult, [](void* userData, OmniClientResult result) noexcept
        {
            auto returnResult = static_cast<OmniClientResult*>(userData);
            *returnResult = result;
        }));

    {
        std::unique_lock<std::mutex> lk(gLogMutex);
        std::cout << "finished [" << omniClientGetResultString(localResult) << "]" << std::endl;
    }
}

// Perform a live edit on the box
static void liveEdit(UsdGeomMesh &meshIn, std::string stageUrl)
{
    double angle = 0;

    // Process any updates that may have happened to the stage from another client
    omniClientLiveProcess();
    {
        std::unique_lock<std::mutex> lk(gLogMutex);
        std::cout << "Begin Live Edit on " << meshIn.GetPath() << " - " << std::endl;
        std::cout << "Enter 't' to transform, 'm' to send a channel message, 'l' to leave the channel, or 'q' to quit.\n";
    }

    // Join a message channel to communicate text messages between clients
    OmniClientRequestId joinRequestId = omniClientJoinChannel((stageUrl + ".__omni_channel__").c_str(), 
        nullptr, 
        [](void* userData, OmniClientResult result, OmniClientChannelEvent eventType, const char* from, OmniClientContent* content) noexcept
        {
            std::string eventTypeStr;
            switch (eventType)
            {
            case eOmniClientChannelEvent_Error:        eventTypeStr = "ERROR";        break;
            case eOmniClientChannelEvent_Message:    eventTypeStr = "MESSAGE";    break;
            case eOmniClientChannelEvent_Hello:        eventTypeStr = "HELLO";        break;
            case eOmniClientChannelEvent_Join:        eventTypeStr = "JOIN";        break;
            case eOmniClientChannelEvent_Left:        eventTypeStr = "LEFT";        break;
            case eOmniClientChannelEvent_Deleted:    eventTypeStr = "DELETED";    break;
            }
            std::cout << "Channel event: " << eventTypeStr << std::endl;
            if (eventType == eOmniClientChannelEvent_Message)
            {
                // Assume that this is an ASCII message from another client
                std::string messageText((char*)content->buffer, content->size);
                std::cout << "Channel message received: " << messageText << std::endl;
            }
    }
    );

    bool wait = true;
    while (wait)
    {
#ifdef _WIN32
        char nextCommand = _getch();
#else
        char nextCommand = getchar();
#endif
        // Process any updates that may have happened to the stage from another client
        omniClientLiveProcess();

        switch (nextCommand) {
        case 't':
        {
            // Increase the angle
            angle += 15;
            if (angle >= 360)
                angle = 0;

            double radians = angle * 3.1415926 / 180.0;
            double x = sin(radians) * 100;
            double y = cos(radians) * 100;

            GfVec3d position(0);
            GfVec3d rotXYZ(0);
            GfVec3d scale(1);
            if( !xformUtils::getLocalTransformSRT( meshIn.GetPrim(), position, rotXYZ, gDefaultRotationOrder, scale ) )
            {
                std::cerr << "WARNING: Unable to read transformation on \"" << meshIn.GetPath() << "\"" << std::endl;
                continue;
            }

            // Move/Rotate the existing position/rotation - this works for Y-up stages
            position += GfVec3d(x, 0, y);
            rotXYZ = GfVec3d(rotXYZ[0], angle, rotXYZ[2]);

            xformUtils::setLocalTransformSRT( meshIn.GetPrim(), position, rotXYZ, gDefaultRotationOrder, scale );
            {
                std::unique_lock<std::mutex> lk(gLogMutex);
                std::cout << "Setting pos: " << position << " and rot: " << rotXYZ << std::endl;
            }

            // Commit the change to USD
            omniClientLiveProcess();
            break;
        }

        case 'm':
        {
            if (kInvalidRequestId != joinRequestId)
            {
                std::cout << "Enter a channel message: " << std::endl;
#ifndef _WIN32
                // On Linux we seem to require an extra `get` in order to pause for user input,
                // whereas running the same on Windows would "steal" the first character of the
                // input message...
                std::cin.get();
#endif
                std::string message;
                std::getline(std::cin, message);

                size_t msgLen = message.length() + 1;
                // omniClientSendMessage will call the OmniClientContent::free() function when finished with the buffer
                char* messageBuffer = (char*)malloc(msgLen);
                std::strncpy(messageBuffer, message.c_str(), msgLen);
                // sanitize for static analysis
                messageBuffer[message.length()] = '\0';

                OmniClientContent content = {
                    /* buffer */ (void*)messageBuffer,
                    /* length */ msgLen,
                    /* free function*/ [](void* buf) noexcept { free(buf); } };

                omniClientSendMessage(joinRequestId, &content, nullptr, [](void* userData, OmniClientResult result) noexcept {});
            }
            else
            {
                std::cout << "The message channel is disconnected." << std::endl;
            }

            break;
        }
        case 'l':
        {
            std::cout << "Leaving message channel" << std::endl;
            omniClientStop(joinRequestId);
            joinRequestId = kInvalidRequestId;
            break;
        }
        //escape or 'q'
        case 27:
        case 'q':
            wait = false;
            std::cout << "Live Edit complete\n";
            break;
        default:
            std::cout << "Enter 't' to transform, 'm' to send a channel message, 'l' to leave the channel, or 'q' to quit." << std::endl;
        }
    }
}

// Returns true if the provided maybeURL contains a host and path
static bool isValidOmniURL(const std::string& maybeURL)
{
    bool isValidURL = false;
    OmniClientUrl* url = omniClientBreakUrl(maybeURL.c_str());
    if (url->host && url->path && 
        (std::string(url->scheme) == std::string("omniverse") ||
         std::string(url->scheme) == std::string("omni")))
    {
        isValidURL = true;
    }
    omniClientFreeUrl(url);
    return isValidURL;
}

// Print the command line arguments help
static void printCmdLineArgHelp()
{
    std::cout << "Usage: samples [options]" << std::endl;
    std::cout << "  options:" << std::endl;
    std::cout << "    -h, --help                    Print this help" << std::endl;
    std::cout << "    -l, --live                    Allow the user to continue modifying the stage live after creating (with the 't' key)" << std::endl;
    std::cout << "    -p, --path dest_stage_folder  Alternate destination stage path folder [default: omniverse://localhost/Users/test]" << std::endl;
    std::cout << "    -e, --existing path_to_stage  Open an existing stage and perform live transform edits (full omniverse URL)" << std::endl;
    std::cout << "    -v, --verbose                 Show the verbose Omniverse logging" << std::endl;
    std::cout << "\n\nExamples:\n";
    std::cout << " * create a stage on the ov-prod server at /Projects/HelloWorld/helloworld.usd" << std::endl;
    std::cout << "    > samples -p omniverse://ov-prod/Projects/HelloWorld" << std::endl;
    std::cout << "\n * live edit a stage on the ov-prod server at /Projects/LiveEdit/livestage.usd" << std::endl;
    std::cout << "    > samples -e omniverse://ov-prod/Projects/LiveEdit/livestage.usd" << std::endl;
}


// Main Application 
int main(int argc, char*argv[])
{
    bool doLiveEdit = false;
    std::string existingStage;
    std::string destinationPath = "";
    UsdGeomMesh boxMesh;

    // Process the arguments, if any
    for (int x = 1; x < argc; x++)
    {
        if (strcmp(argv[x], "-h") == 0 || strcmp(argv[x], "--help") == 0)
        {
            printCmdLineArgHelp();
            return 0;
        }
        else if (strcmp(argv[x], "-l") == 0 || strcmp(argv[x], "--live") == 0)
        {
            doLiveEdit = true;
        }
        else if ((strcmp(argv[x], "-p") == 0 || strcmp(argv[x], "--path") == 0) && argc > x)
        {
            if (x == argc-1)
            {
                std::cout << "ERROR: Missing an Omniverse folder URL to create the stage.\n" << std::endl;
                printCmdLineArgHelp();
                return -1;
            }
            destinationPath = std::string(argv[++x]);
            if (!isValidOmniURL(destinationPath))
            {
                std::cout << "This is not an Omniverse Nucleus URL: " << destinationPath << std::endl;
                std::cout << "Correct Omniverse URL format is: omniverse://server_name/Path/To/Example/Folder" << std::endl;
                std::cout << "Allowing program to continue because file paths may be provided in the form: C:/Path/To/Stage" << std::endl;
            }
        }
        else if (strcmp(argv[x], "-v") == 0 || strcmp(argv[x], "--verbose") == 0)
        {
            gOmniverseLoggingEnabled = true;
        }
        else if (strcmp(argv[x], "-e") == 0 || strcmp(argv[x], "--existing") == 0)
        {
            doLiveEdit = true;
            if (x == argc-1)
            {
                std::cout << "ERROR: Missing an Omniverse URL to the stage to edit.\n" << std::endl;
                printCmdLineArgHelp();
                return -1;
            }
            existingStage = std::string(argv[++x]);
            if (!isValidOmniURL(existingStage))
            {
                std::cout << "This is not an Omniverse Nucleus URL: " << destinationPath << std::endl;
                std::cout << "Correct Omniverse URL format is: omniverse://server_name/Path/To/Example/Folder" << std::endl;
                std::cout << "Allowing program to continue because file paths may be provided in the form: C:/Path/To/Stage" << std::endl;
            }
        }
        else
        {
            std::cout << "Unrecognized option: " << argv[x] << std::endl;
        }
    }

    // Startup Omniverse with the default login
    if (!startOmniverse())
        exit(1);

    // Find the correct user folder on Nucleus (if the path is was specified)
    if (existingStage.empty() && destinationPath.empty())
    {
        std::cout << "No output path specified, so checking the localhost Users folder for a valid username" << std::endl;
        std::string userFolder = "omniverse://localhost/Users";
        std::string username = getConnectedUsername(userFolder.c_str());
        destinationPath = userFolder + "/" + username;
    }

    if (existingStage.empty())
    {
        // Create the USD model in Omniverse
        const std::string stageUrl = createOmniverseModel(destinationPath, doLiveEdit);
        existingStage = stageUrl;

        // Print the username for the server
        printConnectedUsername(stageUrl);

        // Create physics scene
        createPhysicsScene();

        // Create box geometry in the model
        boxMesh = createBox();

        // Create dynamic cube
        createDynamicCube(100.0);

        // Create quad - static tri mesh collision so that the box collides with it
        createQuad(500.0);

        checkpointFile(stageUrl, "Add box and nothing else");

        // Create lights in the scene
        createDistantLight();
        createDomeLight("./Materials/kloofendal_48d_partly_cloudy.hdr");

        // Add a Nucleus Checkpoint to the stage
        checkpointFile(stageUrl, "Add lights to stage");

        // Upload a material and textures to the Omniverse server
        uploadReferences(destinationPath);

        // Add a material to the box
        createMaterial(boxMesh);

        // Add a Nucleus Checkpoint to the stage
        checkpointFile(stageUrl, "Add material to the box");

        // Add a reference and payload, modify OmniPBR parameter
        createReferenceAndPayload();
        checkpointFile(stageUrl, "Add Reference, Payload, and modified OmniPBR");

        // Create an empty folder, just as an example
        createEmptyFolder(destinationPath + "/EmptyFolder");
    }
    else
    {
        // Find a UsdGeomMesh in the existing stage
        boxMesh = findGeomMesh(existingStage);
    }

    // Do a live edit session moving the box around, changing a material
    if (doLiveEdit && boxMesh && existingStage.find(".live") != std::string::npos)
    {
        liveEdit(boxMesh, existingStage);
    }

    // All done, shut down our connection to Omniverse
    shutdownOmniverse();

    return 0;
}
