# FBXJointRenamer
A command line tool to rename joints for retargeting in FBX files

Operation is super simple:

1. Place your input FBX in the Debug folder
2. Run FBXTest.exe filename.fbx (where filename is your fbx file)
3. Edit the jointmap.cfg file. Syntax is one joint per line, with the old and new joint name seperated by an equals sign - e.g. myOldJointName=myNewJointName.
4. Joints without an old to new mapping will be ignored (not renamed)
5. Output will go to output.fbx

To build from source on Mac:

1. Get a copy of the FBX SDK, perferably the version, shipping with the Unreal Engine source (Engine/Source/ThirdParty/FBX/YYYY.v.m/*), if you want to use the tool with the engine
2. Copy it to ThirdParty/FbxSdk, so ThirdParty/FbxSdk/include and ThirdParty/FbxSdk/lib are valid
3. If you want to statically link against libfbxsdk, remove ThirdParty/FbxSdk/lib/clang/release/libfbxsdk.dylib
4. Open the Xcode project
5. Build