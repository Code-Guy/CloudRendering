// Fill out your copyright notice in the Description page of Project Settings.
#include "EditorWidgetFunctionLibrary.h"
#include "AutoReimport/AutoReimportManager.h"
#include "EditorReimportHandler.h"
#include "Modules/ModuleManager.h"
#include "AssetData.h"
#include "AssetRegistryModule.h"
#include "IAssetTools.h"
#include "AssetToolsModule.h"
#include "Engine/Texture.h"

FString UEditorWidgetFunctionLibrary::ExportPath;
TArray<UObject*> UEditorWidgetFunctionLibrary::AssetsToExport;

void UEditorWidgetFunctionLibrary::ExportTextures(const FString& InExportPath)
{
	UE_LOG(LogClass, Log, TEXT("OnExportButtonClicked"));

	ExportPath = InExportPath;

	TArray<FAssetData> AssetDatas;
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	FARFilter ARFilter;
	ARFilter.PackagePaths.Add(FName(TEXT("/Game")));
	ARFilter.ClassNames.Add(FName(TEXT("Texture2D")));
	ARFilter.bRecursiveClasses = false;
	ARFilter.bRecursivePaths = true;
	AssetRegistryModule.Get().GetAssets(ARFilter, AssetDatas);

	AssetsToExport.Empty();
	FAssetToolsModule& AssetToolsModule = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools");
	for (const FAssetData& AssetData : AssetDatas)
	{
		AssetsToExport.Add(AssetData.GetAsset());
	}
	AssetToolsModule.Get().ExportAssets(AssetsToExport, ExportPath);
}

void UEditorWidgetFunctionLibrary::ReimportTextures()
{
	UE_LOG(LogClass, Log, TEXT("OnReimportButtonClicked"));

	for (UObject* Asset : AssetsToExport)
	{
		FString PathName = Asset->GetPathName();
		FString Left, Right;
		PathName.Split(TEXT("."), &Left, &Right);
		
		FString ImportFileName = FString::Printf(TEXT("%s%s.TGA"), *ExportPath, *Left);
		FReimportManager::Instance()->UpdateReimportPaths(Asset, { ImportFileName });
		UE_LOG(LogClass, Log, TEXT("Update %s's reimport path with %s"), *PathName, *ImportFileName);
	}
}
