#include "Common/Common.h"
#include "DisplayCommon.h"
#include "DisplayHierarchy.h"
#include "DisplaySkeleton.h"

#include <iostream>
#include <map>
#include <istream>
#include <sstream>
#include <fstream>

// Local function prototypes.
void DisplayContent(FbxScene* pScene);
void DisplayContent(FbxNode* pNode);
void DisplayTarget(FbxNode* pNode);
void DisplayTransformPropagation(FbxNode* pNode);
void DisplayGeometricTransform(FbxNode* pNode);
void DisplayMetaData(FbxScene* pScene);
void ScaleCurves(FbxNode* pNode, FbxAnimLayer* pLayer, FbxVectorTemplate3<double> scale);

static bool gVerbose = true;
static bool removeAnim = false;
std::map<std::string, std::string> jointMap;


int main(int argc, char** argv)
{
	FbxManager* lSdkManager = NULL;
	FbxScene* lScene = NULL;
	bool lResult;


	// Prepare the FBX SDK.
	InitializeSdkObjects(lSdkManager, lScene);
	// Load the scene.

	// The example can take a FBX file as an argument.
	FbxString lFilePath("");
    const char* outpath = "output.fbx";
	for (int i = 1, c = argc; i < c; ++i)
	{
		if (FbxString(argv[i]) == "-test") gVerbose = false;
        else if (FbxString(argv[i]) == "-removeanim") removeAnim = true;
		else if (lFilePath.IsEmpty()) lFilePath = argv[i];
        else if (!lFilePath.IsEmpty()) outpath = argv[i];
	}

	//Read joints file
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
		std::cout << "Read line: " << line << std::endl;

		std::istringstream is_line(line);
		std::string key;
		if (std::getline(is_line, key, '='))
		{
			std::string value;
			if (std::getline(is_line, value))
				jointMap[key] = value;
		}
	}


	if (lFilePath.IsEmpty())
	{
		lResult = false;
		FBXSDK_printf("\n\nUsage: ImportScene <FBX file name>\n\n");
	}
	else
	{
		FBXSDK_printf("\n\nFile: %s\n\n", lFilePath.Buffer());
		lResult = LoadScene(lSdkManager, lScene, lFilePath.Buffer());
	}

	if (lResult == false)
	{
		FBXSDK_printf("\n\nAn error occurred while loading the scene...");
	}
	else
	{
		// Display the scene.
		DisplayMetaData(lScene);		
		DisplayContent(lScene);
		
	}

    // Parse all the nodes to convert the translations and meshes vertices.
    int numAnimStacks = lScene->GetSrcObjectCount(FbxCriteria::ObjectType(FbxAnimStack::ClassId));
    for(int i = 0; i < numAnimStacks; ++i)
    {
        FbxAnimStack* stack = FbxCast<FbxAnimStack>(lScene->GetSrcObject(FbxCriteria::ObjectType(FbxAnimStack::ClassId), i));
        FBXSDK_printf("Scaling Stack %s\n", stack->GetName());

        int nbAnimLayers = stack->GetMemberCount(FbxCriteria::ObjectType(FbxAnimLayer::ClassId));
        for(int j = 0; j < nbAnimLayers; ++j)
        {
            FbxAnimLayer* layer = FbxCast<FbxAnimLayer>(stack->GetMember(FbxCriteria::ObjectType(FbxAnimLayer::ClassId), j));
            FBXSDK_printf("  Scaling Layer %s\n", layer->GetName());
            ScaleCurves(lScene->GetRootNode(), layer, FbxVectorTemplate3<double>(1.0, 1.0, 1.0));
        }
    }

    if (removeAnim)
    {
        for (int i = numAnimStacks - 1; i >= 0; --i)
        {
            FbxAnimStack* stack = FbxCast<FbxAnimStack>(lScene->GetSrcObject(FbxCriteria::ObjectType(FbxAnimStack::ClassId), i));
            FBXSDK_printf("Removing Anim Stack %s\n", stack->GetName());
            lScene->RemoveAnimStack(stack->GetName());
        }
    }

    FbxGlobalSettings& settings = lScene->GetGlobalSettings();
    FbxSystemUnit::cm.ConvertScene(lScene);
    settings.SetSystemUnit(FbxSystemUnit::cm);
    lScene->GetAnimationEvaluator()->Reset();

	lResult = SaveScene(lSdkManager, lScene, outpath);
	if (lResult == false)
	{
		FBXSDK_printf("\n\nAn error occurred while saving the scene...\n");
		DestroySdkObjects(lSdkManager, lResult);
		return 0;
	}



	// Destroy all objects created by the FBX SDK.
	DestroySdkObjects(lSdkManager, lResult);

	return 0;
}

void ApplyComponentScale(FbxNode* pNode, FbxAnimLayer* pLayer, FbxVectorTemplate3<double>& scale, int component, const char* componentName)
{
    // Apply parent scale first
    FbxAnimCurve* translation = pNode->LclTranslation.GetCurve(pLayer, componentName);
    if(translation)
    {
        FBXSDK_printf("      Trans %s %s\n", componentName, pNode->GetName());
        translation->KeyScaleValueAndTangent(scale[component]);
    }

    // Add local scale for child scaling
    FbxAnimCurve* lclScale = pNode->LclScaling.GetCurve(pLayer, componentName);
    if (lclScale)
    {
        FBXSDK_printf("      Scale %s %s\n", componentName, pNode->GetName());
        scale[component] *= lclScale->GetValue();
        lclScale->KeyClear();
    }
}

void ScaleCurves(FbxNode* pNode, FbxAnimLayer* pLayer, FbxVectorTemplate3<double> scale)
{
    FBXSDK_printf("    Scaling %s\n", pNode->GetName());
    ApplyComponentScale(pNode, pLayer, scale, 0, FBXSDK_CURVENODE_COMPONENT_X);
    ApplyComponentScale(pNode, pLayer, scale, 1, FBXSDK_CURVENODE_COMPONENT_Y);
    ApplyComponentScale(pNode, pLayer, scale, 2, FBXSDK_CURVENODE_COMPONENT_Z);

    FBXSDK_printf("      New scale %f, %f, %f\n", scale[0], scale[1], scale[2]);

    for (int i = 0; i < pNode->GetChildCount(); i++)
    {
        ScaleCurves(pNode->GetChild(i), pLayer, scale);
    }
}

void DisplayContent(FbxScene* pScene)
{
	int i;
	FbxNode* lNode = pScene->GetRootNode();

	if (lNode)
	{
		for (i = 0; i < lNode->GetChildCount(); i++)
		{
			DisplayContent(lNode->GetChild(i));
		}
	}
}

void DisplayContent(FbxNode* pNode)
{
	FbxNodeAttribute::EType lAttributeType;
	int i;

	if (pNode->GetNodeAttribute() == NULL)
	{
		FBXSDK_printf("NULL Node Attribute\n\n");
	}
	else
	{
		lAttributeType = (pNode->GetNodeAttribute()->GetAttributeType());

		switch (lAttributeType)
		{
		default:
			break;

		case FbxNodeAttribute::eSkeleton:
			DisplaySkeleton(pNode, jointMap);
			break;

		}
	}

	for (i = 0; i < pNode->GetChildCount(); i++)
	{
		DisplayContent(pNode->GetChild(i));
	}
}





void DisplayMetaData(FbxScene* pScene)
{
	FbxDocumentInfo* sceneInfo = pScene->GetSceneInfo();
	if (sceneInfo)
	{
		FBXSDK_printf("\n\n--------------------\nMeta-Data\n--------------------\n\n");
		FBXSDK_printf("    Title: %s\n", sceneInfo->mTitle.Buffer());
		FBXSDK_printf("    Subject: %s\n", sceneInfo->mSubject.Buffer());
		FBXSDK_printf("    Author: %s\n", sceneInfo->mAuthor.Buffer());
		FBXSDK_printf("    Keywords: %s\n", sceneInfo->mKeywords.Buffer());
		FBXSDK_printf("    Revision: %s\n", sceneInfo->mRevision.Buffer());
		FBXSDK_printf("    Comment: %s\n", sceneInfo->mComment.Buffer());

		FbxThumbnail* thumbnail = sceneInfo->GetSceneThumbnail();
		if (thumbnail)
		{
			FBXSDK_printf("    Thumbnail:\n");

			switch (thumbnail->GetDataFormat())
			{
			case FbxThumbnail::eRGB_24:
				FBXSDK_printf("        Format: RGB\n");
				break;
			case FbxThumbnail::eRGBA_32:
				FBXSDK_printf("        Format: RGBA\n");
				break;
			}

			switch (thumbnail->GetSize())
			{
			default:
				break;
			case FbxThumbnail::eNotSet:
				FBXSDK_printf("        Size: no dimensions specified (%ld bytes)\n", thumbnail->GetSizeInBytes());
				break;
			case FbxThumbnail::e64x64:
				FBXSDK_printf("        Size: 64 x 64 pixels (%ld bytes)\n", thumbnail->GetSizeInBytes());
				break;
			case FbxThumbnail::e128x128:
				FBXSDK_printf("        Size: 128 x 128 pixels (%ld bytes)\n", thumbnail->GetSizeInBytes());
			}
		}
	}
}

