#include "Common/Common.h"

#include <iostream>
#include <map>
#include <set>
#include <istream>
#include <sstream>
#include <fstream>

// Options
bool removeAnim = false;
bool bakeRootScale = false;
float baseScale = 1.0f;

// Node renaming data
std::map<std::string, std::string> jointMap = {};
std::set<std::string> foundNodes = {};

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

    FBXSDK_printf("      New scale %f, %f, %f\n", scale[0], scale[1], scale[2]);

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
            FBXSDK_printf("  Layer %s\n", layer->GetName());
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
    // Convert node's translation.
    FbxDouble3 lcltra = pNode->LclTranslation.Get();
    FBXSDK_printf("    Scale Translation: %f, %f, %f", lcltra[0], lcltra[1], lcltra[2]);
    lcltra[0] *= scale[0];
    lcltra[1] *= scale[1];
    lcltra[2] *= scale[2];
    FBXSDK_printf(" -> %f, %f, %f\n", lcltra[0], lcltra[1], lcltra[2]);
    pNode->LclTranslation.Set(lcltra);
}

void ScaleMesh(FbxMesh* mesh, FbxDouble3 scale)
{
    FBXSDK_printf("    Scale Mesh: %f, %f, %f\n", scale[0], scale[1], scale[2]);
    unsigned int const numVertices = mesh->GetControlPointsCount();
    for(unsigned int i = 0; i < numVertices; ++i)
    {
        FbxVector4 vertex = mesh->GetControlPointAt(i);
        vertex[0] *= scale[0];
        vertex[1] *= scale[1];
        vertex[2] *= scale[2];
        mesh->SetControlPointAt(vertex, i);
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
                    FBXSDK_printf("  %s (Skeleton)\n    Scale Size: %.3f -> %.3f)\n", node->GetName(), length, length * factor);
                    skeleton->Size.Set(length * factor);
                    break;
                case FbxSkeleton::eLimb:
                    length = skeleton->LimbLength.Get();
                    FBXSDK_printf("  %s (Limb)\n    Scale Length: %.3f -> %.3f)\n", node->GetName(), length, length * factor);
                    skeleton->LimbLength.Set(length * factor);
                    break;
                case FbxSkeleton::eLimbNode:
                    length = skeleton->Size.Get();
                    FBXSDK_printf("  %s (Limb Node)\n    Scale Size: %.3f -> %.3f)\n", node->GetName(), length, length * factor);
                    skeleton->Size.Set(length * factor);
                    break;
                case FbxSkeleton::eEffector:
                    FBXSDK_printf("  %s (Effector)\n", node->GetName());
                    break;
            }

            if (bakeRootScale && (!inSkeleton))
            {
                FbxDouble3 nodeScale = node->LclScaling.Get();
                FBXSDK_printf("    Scale Root: %f, %f, %f", nodeScale[0], nodeScale[1], nodeScale[2]);
                scale[0] *= nodeScale[0];
                scale[1] *= nodeScale[1];
                scale[2] *= nodeScale[2];
                FBXSDK_printf(" -> %f, %f, %f\n", scale[0], scale[1], scale[2]);
                node->LclScaling.Set(FbxVectorTemplate3<double>(1.0, 1.0, 1.0));
                inSkeleton = true;
            }

            RenameBone(node);
            ScaleTranslation(node, scale);
            break;
        }
        case FbxNodeAttribute::eMesh:
            FBXSDK_printf("  %s (Mesh)\n", node->GetName());
            //ScaleMesh(node->GetMesh(), scale);
            ScaleTranslation(node, scale);
            inSkeleton = false;
            break;
        case FbxNodeAttribute::eNull:
        default:
            FBXSDK_printf("  %s (Empty)\n", node->GetName());
            ScaleTranslation(node, scale);
            inSkeleton = false;
            break;
    }

    int const childCount = node->GetChildCount();
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

int main(int argc, char** argv)
{
    // Parse arguments
    FbxString sourcePath("");
    const char* destPath = "output.fbx";
    for (int i = 1, c = argc; i < c; ++i)
    {
        if (FbxString(argv[i]) == "-removeanim") removeAnim = true;
        else if (FbxString(argv[i]) == "-bakerootscale") bakeRootScale = true;
        else if (FbxString(argv[i]) == "-scale10") baseScale = 10.0f;
        else if (FbxString(argv[i]) == "-scale100") baseScale = 100.0f;
        else if (sourcePath.IsEmpty()) sourcePath = argv[i];
        else if (!sourcePath.IsEmpty()) destPath = argv[i];
    }

    if (sourcePath.IsEmpty())
    {
        FBXSDK_printf("Usage: %s [-scale100] [-removeanim] <source> <dest>\n", argv[0]);
        return 1;
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
