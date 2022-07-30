// Copyright (c) 2022 Solar Storm Interactive

using UnrealBuildTool;

public class SectionedUVTools : ModuleRules
{
	public SectionedUVTools(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				// ... add other public dependencies that you statically link with here ...
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"MeshBuilder",
				"MaterialUtilities",
				"RawMesh",
				"MeshUtilities",
				// ... add private dependencies that you statically link with here ...	
			}
			);
		
		
		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"Settings",
				"AssetTools",
				"AssetRegistry",
			}
		);

		DynamicallyLoadedModuleNames.AddRange(
			new string[] {
				"AssetTools",
			}
		);
	}
}
