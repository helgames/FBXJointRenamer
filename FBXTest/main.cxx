#include "Common/Common.h"

#include <iostream>
#include <map>
#include <set>
#include <vector>
#include <istream>
#include <sstream>
#include <fstream>

// Options
bool removeAnim = false;
bool bakeRootScale = false;
bool removeRootScale = false;
bool removeRootRotation = false;
bool convertAxis = false;
float baseScale = 1.0f;

// Node renaming data
std::map<std::string, std::string> jointMap = {};
std::set<std::string> foundNodes = {};

/*
struct Skeleton
{
    FbxSkeleton* skeleton;
    FbxSkeleton* bones;
    FbxMesh* linkedMeshes;
    std::vector<FbxAnimStack*> animations;

    // Apply to vert, bone and animation translation
    FbxQuaternion rootRotation;
    FbxDouble3 rootScale;
};

std::vector<Skeleton*> skeletons;
std::vector<FbxMesh*> meshes;
*/

void RemoveAnimations(FbxScene* scene)
{
    FBXSDK_printf("Removing Animation Stacks:\n");
    int animStackCount = scene->GetSrcObjectCount(FbxCriteria::ObjectType(FbxAnimStack::ClassId));
    for (int i = animStackCount - 1; i >= 0; --i)
    {
        FbxAnimStack* stack = FbxCast<FbxAnimStack>(scene->GetSrcObject(FbxCriteria::ObjectType(FbxAnimStack::ClassId), i));
        FBXSDK_printf("  %s\n", stack->GetName());
        scene->RemoveAnimStack(stack->GetName());
    }
}

void ApplyComponentScale(FbxNode* node, FbxAnimLayer* layer, FbxDouble3& scale, int component, const char* componentName)
{
    // Apply parent scale first
    FbxAnimCurve* translation = node->LclTranslation.GetCurve(layer, componentName);
    if(translation)
    {
        translation->KeyScaleValueAndTangent(scale[component]);
    }

    // Add local scale for child scaling
    FbxAnimCurve* localScale = node->LclScaling.GetCurve(layer, componentName);
    if (localScale)
    {
        scale[component] *= localScale->GetValue();
        localScale->KeyClear();
    }
}

void ScaleCurves(FbxNode* node, FbxAnimLayer* layer, FbxDouble3 scale)
{
    ApplyComponentScale(node, layer, scale, 0, FBXSDK_CURVENODE_COMPONENT_X);
    ApplyComponentScale(node, layer, scale, 1, FBXSDK_CURVENODE_COMPONENT_Y);
    ApplyComponentScale(node, layer, scale, 2, FBXSDK_CURVENODE_COMPONENT_Z);

    FBXSDK_printf("      %s Scale %.3f, %.3f, %.3f\n", node->GetName(), scale[0], scale[1], scale[2]);

    for (int i = 0; i < node->GetChildCount(); i++)
    {
        ScaleCurves(node->GetChild(i), layer, scale);
    }
}

void ProcessAnimations(FbxScene* scene, FbxDouble3 scale)
{
    FBXSDK_printf("Process Animations\n");
    int animStackCount = scene->GetSrcObjectCount(FbxCriteria::ObjectType(FbxAnimStack::ClassId));
    for(int i = 0; i < animStackCount; ++i)
    {
        FbxAnimStack* stack = FbxCast<FbxAnimStack>(scene->GetSrcObject(FbxCriteria::ObjectType(FbxAnimStack::ClassId), i));
        FBXSDK_printf("  Stack %s\n", stack->GetName());

        int animLayerCount = stack->GetMemberCount(FbxCriteria::ObjectType(FbxAnimLayer::ClassId));
        for(int j = 0; j < animLayerCount; ++j)
        {
            FbxAnimLayer* layer = FbxCast<FbxAnimLayer>(stack->GetMember(FbxCriteria::ObjectType(FbxAnimLayer::ClassId), j));
            FBXSDK_printf("    Layer %s\n", layer->GetName());
            ScaleCurves(scene->GetRootNode(), layer, scale);
        }
    }
}

void RenameBone(FbxNode* node)
{
    FbxString origName = node->GetName();
    FbxString newName = origName;
    bool renamed = false;

    for (int i = 2;! foundNodes.insert(std::string(node->GetName())).second; ++i)
    {
        newName = origName;
        char buffer[4];
        sprintf(buffer, "%2d", i);
        newName.Append(buffer, strlen(buffer));
        node->SetName(newName);
        renamed = true;
    }

    std::string newNameStd(newName);
    if (jointMap.find(newNameStd) != jointMap.end())
    {
        node->SetName(jointMap[newNameStd].c_str());
        renamed = true;
    }

    if (renamed)
    {
        FBXSDK_printf("    Rename Node: %s\n", node->GetName());
    }
}

void ScaleTranslation(FbxNode* pNode, FbxDouble3 scale)
{
    if (scale[0] == 0 && scale[1] == 0 && scale[2] == 0) return;

    // Convert node's translation.
    FbxDouble3 lcltra = pNode->LclTranslation.Get();
    FBXSDK_printf("    Scale Translation: %.3f, %.3f, %.3f", lcltra[0], lcltra[1], lcltra[2]);
    lcltra[0] *= scale[0];
    lcltra[1] *= scale[1];
    lcltra[2] *= scale[2];
    FBXSDK_printf(" -> %.3f, %.3f, %.3f\n", lcltra[0], lcltra[1], lcltra[2]);
    pNode->LclTranslation.Set(lcltra);
}

FbxAMatrix& ScaleAfineMatrix(FbxAMatrix& matrix, FbxDouble3 scale)
{
    FbxVector4 translation = matrix.GetT();
    translation[0] *= scale[0];
    translation[1] *= scale[1];
    translation[2] *= scale[2];
    matrix.SetT(translation);
    return matrix;
}

void ScaleMesh(FbxMesh* mesh, FbxDouble3 scale)
{
    FBXSDK_printf("    Scale Mesh: %.3f, %.3f, %.3f\n", scale[0], scale[1], scale[2]);
    int vertexCount = mesh->GetControlPointsCount();
    for(unsigned int i = 0; i < vertexCount; ++i)
    {
        FbxVector4 vertex = mesh->GetControlPointAt(i);
        vertex[0] *= scale[0];
        vertex[1] *= scale[1];
        vertex[2] *= scale[2];
        mesh->SetControlPointAt(vertex, i);
    }

    FbxAMatrix transformMatrix;
    int nameLen = strlen(mesh->GetNode()->GetName());

    int deformerCount = mesh->GetDeformerCount();
    for (int i = 0; i < deformerCount; ++i)
    {
        FbxDeformer* deformer = mesh->GetDeformer(i);
        switch (deformer->GetDeformerType())
        {
            case FbxDeformer::eSkin:
            {
                FBXSDK_printf("    Scale Skin: ");

                FbxSkin* skin = FbxCast<FbxSkin>(deformer);
                int clusterCount = skin->GetClusterCount();
                int lineLength = 0;
                for (int i = 0; i < clusterCount; ++i)
                {
                    FbxCluster* cluster = skin->GetCluster(i);
                    if (i > 0) FBXSDK_printf(", ");
                    if (lineLength > 60) { FBXSDK_printf("\n                "); lineLength = 0; }
                    FBXSDK_printf("%s", cluster->GetName()); // + 9 + nameLen);
                    lineLength += strlen(cluster->GetName()) + 2; //  - 7 - nameLen);

                    transformMatrix = cluster->GetTransformMatrix(transformMatrix);
                    cluster->SetTransformMatrix(ScaleAfineMatrix(transformMatrix, scale));

                    transformMatrix = cluster->GetTransformLinkMatrix(transformMatrix);
                    cluster->SetTransformLinkMatrix(ScaleAfineMatrix(transformMatrix, scale));

                    transformMatrix = cluster->GetTransformAssociateModelMatrix(transformMatrix);
                    cluster->SetTransformAssociateModelMatrix(ScaleAfineMatrix(transformMatrix, scale));
                }

                FBXSDK_printf("\n");
                break;
            }
            case FbxDeformer::eBlendShape:
                FBXSDK_printf("      Skip Blend Shape: %s\n", deformer->GetName());
                break;
            case FbxDeformer::eVertexCache:
                FBXSDK_printf("      Skip Vertex Cache: %s\n", deformer->GetName());
                break;
            case FbxDeformer::eUnknown:
            default:
                FBXSDK_printf("      Skip Unknown Deformer: %s\n", deformer->GetName());
                break;
        }
    }
}

void ProcessSubScene(FbxNode* node, FbxDouble3 scale, bool inSkeleton = false)
{
    FbxNodeAttribute::EType type = FbxNodeAttribute::eNull;
    if (node->GetNodeAttribute() != NULL)
    {
        type = node->GetNodeAttribute()->GetAttributeType();
    }

    switch (type)
    {
        case FbxNodeAttribute::eSkeleton:
        {
            float factor = scale[0]; //sqrtf(scale[0] * scale[0] + scale[1] * scale[1] + scale[2] * scale[2]);
            float length = 0.0f;
            FbxSkeleton* skeleton = node->GetSkeleton();

            switch (skeleton->GetSkeletonType())
            {
                case FbxSkeleton::eRoot:
                    length = skeleton->Size.Get();
                    FBXSDK_printf("  %s (Skeleton)\n", node->GetName());
                    ScaleTranslation(node, scale);
                    FBXSDK_printf("    Scale Size: %.3f -> %.3f)\n", length, length * factor);
                    skeleton->Size.Set(length * factor);
                    break;
                case FbxSkeleton::eLimb:
                    length = skeleton->LimbLength.Get();
                    FBXSDK_printf("  %s (Limb)\n", node->GetName());
                    ScaleTranslation(node, scale);
                    FBXSDK_printf("    Scale Length: %.3f -> %.3f)\n", length, length * factor);
                    skeleton->LimbLength.Set(length * factor);
                    break;
                case FbxSkeleton::eLimbNode:
                    length = skeleton->Size.Get();
                    FBXSDK_printf("  %s (Limb Node)\n", node->GetName());
                    ScaleTranslation(node, scale);
                    FBXSDK_printf("    Scale Size: %.3f -> %.3f)\n", length, length * factor);
                    skeleton->Size.Set(length * factor);
                    break;
                case FbxSkeleton::eEffector:
                    FBXSDK_printf("  %s (Effector)\n", node->GetName());
                    ScaleTranslation(node, scale);
                    break;
            }

            if (!inSkeleton)
            {
                if (removeRootRotation)
                {
                    FbxDouble3 rotation = node->LclRotation.Get();
                    FBXSDK_printf("    Un-Rotate Root: %.3f, %.3f, %.3f", rotation[0], rotation[1], rotation[2]);
                    node->LclRotation.Set(FbxDouble3(0.0f, 0.0f, 0.0f));
                    rotation = node->LclRotation.Get();
                    FBXSDK_printf(" -> %.3f, %.3f, %.3f\n", rotation[0], rotation[1], rotation[2]);
                }

                if (bakeRootScale)
                {
                    FbxDouble3 nodeScale = node->LclScaling.Get();
                    FBXSDK_printf("    Scale Root: %.3f, %.3f, %.3f", nodeScale[0], nodeScale[1], nodeScale[2]);
                    scale[0] *= nodeScale[0];
                    scale[1] *= nodeScale[1];
                    scale[2] *= nodeScale[2];
                    FBXSDK_printf(" -> %.3f, %.3f, %.3f\n", scale[0], scale[1], scale[2]);
                }

                if (bakeRootScale || removeRootScale)
                {
                    node->LclScaling.Set(FbxDouble3(1.0, 1.0, 1.0));
                }

                inSkeleton = true;
            }

            RenameBone(node);
            break;
        }
        case FbxNodeAttribute::eMesh:
        {
            FBXSDK_printf("  %s (Mesh)\n", node->GetName());
            FbxMesh* mesh = node->GetMesh();
            /*if (mesh->GetDeformerCount() > 0)
            {
                FBXSDK_printf("    Skipping Skinned Mesh\n");
            }
            else*/
            {
                ScaleTranslation(node, scale);
                ScaleMesh(mesh, scale);
            }

            inSkeleton = false;
            break;
        }
        case FbxNodeAttribute::eNull:
        default:
        {
            FBXSDK_printf("  %s (Empty)\n", node->GetName());
            ScaleTranslation(node, scale);
            RenameBone(node);

            if (bakeRootScale)
            {
                FbxDouble3 nodeScale = node->LclScaling.Get();
                FBXSDK_printf("    Scale Empty: %.3f, %.3f, %.3f", nodeScale[0], nodeScale[1], nodeScale[2]);
                scale[0] *= nodeScale[0];
                scale[1] *= nodeScale[1];
                scale[2] *= nodeScale[2];
                FBXSDK_printf(" -> %.3f, %.3f, %.3f\n", scale[0], scale[1], scale[2]);
            }

            if (bakeRootScale || removeRootScale)
            {
                node->LclScaling.Set(FbxDouble3(1.0, 1.0, 1.0));
            }

            inSkeleton = false;
            break;
        }
    }

    int childCount = node->GetChildCount();
    for(int i = 0; i < childCount; ++i)
    {
        ProcessSubScene(node->GetChild(i), scale, inSkeleton);
    }
}

void ProcessScene(FbxScene* scene, FbxDouble3 scale)
{
    FBXSDK_printf("Processing Scene\n");
    ProcessSubScene(scene->GetRootNode(), scale);
}

void ReadConfig()
{
    std::ifstream jfile("jointmap.cfg");
    std::stringstream is_file;
    if (jfile)
    {
        is_file << jfile.rdbuf();
        jfile.close();
    }

    std::string line;
    while (std::getline(is_file, line))
    {
        std::istringstream is_line(line);
        std::string key;
        if (std::getline(is_line, key, '='))
        {
            std::string value;
            if (std::getline(is_line, value))
                jointMap[key] = value;
        }
    }
}

#define DEBUG 0
#if DEBUG
    #define PARAM_OPTION(name, var, value) if (FbxString(argv[i]) == "-" # name) { var = value; FBXSDK_printf("-" # name "\n"); continue; }
    #define PARAM_VALUE(name) if (name.IsEmpty()) { name = argv[i]; FBXSDK_printf("" # name "=%s\n", argv[i]); continue; }
#else
    #define PARAM_OPTION(name, var, value) if (FbxString(argv[i]) == "-" # name) { var = value; continue; }
    #define PARAM_VALUE(name) if (name.IsEmpty()) { name = argv[i]; continue; }
#endif

int main(int argc, char** argv)
{
    FBXSDK_printf("%s: " __DATE__ " " __TIME__ "\n", argv[0]);

    // Parse arguments
    FbxString sourcePath("");
    FbxString destPath("");
    for (int i = 1, c = argc; i < c; ++i)
    {
        PARAM_OPTION(removeanim, removeAnim, true);
        PARAM_OPTION(bakerootscale, bakeRootScale, true);
        PARAM_OPTION(removerootscale, removeRootScale, true);
        PARAM_OPTION(removerootrotation, removeRootRotation, true);
        PARAM_OPTION(convertaxis, convertAxis, true);
        PARAM_OPTION(scale10, baseScale, 10.0f);
        PARAM_OPTION(scale100, baseScale, 100.0f);
        PARAM_VALUE(sourcePath);
        PARAM_VALUE(destPath);
    }

    if (sourcePath.IsEmpty())
    {
        FBXSDK_printf("Usage: %s [-scale10[0]] [-removeanim] <source> <dest>\n", argv[0]);
        return 1;
    }

    if (destPath.IsEmpty())
    {
        destPath = "output.fbx";
    }

    FbxDouble3 scale(baseScale, baseScale, baseScale);
    FbxManager* sdkManager = NULL;
    FbxScene* scene = NULL;

    InitializeSdkObjects(sdkManager, scene);

    // Read data
    ReadConfig();
    if (!LoadScene(sdkManager, scene, sourcePath))
    {
        FBXSDK_printf("An error occurred while loading the scene...\n");
        DestroySdkObjects(sdkManager, false);
        return 2;
    }

    if (convertAxis)
    {
        FBXSDK_printf("Convert Scene\n");
        FbxAxisSystem::ECoordSystem CoordSystem = FbxAxisSystem::eRightHanded;
        FbxAxisSystem::EUpVector UpVector = FbxAxisSystem::eZAxis;
        FbxAxisSystem::EFrontVector FrontVector = (FbxAxisSystem::EFrontVector) - FbxAxisSystem::eParityOdd;
        FbxAxisSystem(UpVector, FrontVector, CoordSystem).ConvertScene(scene);
    }

    // Process animations
    if (removeAnim)
    {
        RemoveAnimations(scene);
    }
    else
    {
        ProcessAnimations(scene, scale);
    }

    // Process scene
    ProcessScene(scene, scale);

    // Make sure to set the scene scale
    FbxGlobalSettings& settings = scene->GetGlobalSettings();
    settings.SetSystemUnit(FbxSystemUnit::cm);
    scene->GetAnimationEvaluator()->Reset();

    // Save and exit
    if (!SaveScene(sdkManager, scene, destPath))
    {
        FBXSDK_printf("An error occurred while saving the scene...\n");
        DestroySdkObjects(sdkManager, false);
        return 3;
    }
    
    DestroySdkObjects(sdkManager, true);
    return 0;
}
