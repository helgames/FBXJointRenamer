/****************************************************************************************

   Copyright (C) 2014 Autodesk, Inc.
   All rights reserved.

   Use of this software is subject to the terms of the Autodesk license agreement
   provided at the time of installation or download, or which otherwise accompanies
   this software in either electronic or hard copy form.
 
****************************************************************************************/

#include <fbxsdk.h>
#include <map>
#include "DisplayCommon.h"
#include <string>
#include <set>

void DisplaySkeleton(FbxNode* pNode, std::map<std::string, std::string> jointMap)
{
    static std::set<std::string> foundNodes = {};
    for (int i = 2;! foundNodes.insert(std::string(pNode->GetName())).second; i++) {
        FbxString stringName = pNode->GetName();
        DisplayString("Found duplicate of: " + stringName);

        char buffer[4];
        sprintf(buffer, "%2d", i);
        stringName.Append(buffer, strlen(buffer));

        DisplayString("Renaming to: " + stringName);
        pNode->SetName(stringName);
    }

    FbxSkeleton* lSkeleton = (FbxSkeleton*) pNode->GetNodeAttribute();

    DisplayString("Skeleton Name: ", (char *) pNode->GetName());


	if (jointMap.find(std::string(pNode->GetName())) != jointMap.end()) {
		FbxString stringName = jointMap[std::string(pNode->GetName())].c_str();

		DisplayString("Setting name to: " + stringName);
		pNode->SetName(stringName);
	}
	else {
		DisplayString("No new name for joint: ",  (char*)pNode->GetName());
	}

	



    const char* lSkeletonTypes[] = { "Root", "Limb", "Limb Node", "Effector" };

    DisplayString("    Type: ", lSkeletonTypes[lSkeleton->GetSkeletonType()]);

    if (lSkeleton->GetSkeletonType() == FbxSkeleton::eLimb)
    {
        DisplayDouble("    Limb Length: ", lSkeleton->LimbLength.Get());
    }
    else if (lSkeleton->GetSkeletonType() == FbxSkeleton::eLimbNode)
    {
        DisplayDouble("    Limb Node Size: ", lSkeleton->Size.Get());
    }
    else if (lSkeleton->GetSkeletonType() == FbxSkeleton::eRoot)
    {
        DisplayDouble("    Limb Root Size: ", lSkeleton->Size.Get());
    }

    DisplayColor("    Color: ", lSkeleton->GetLimbNodeColor());
}
