// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "EditorWidgetFunctionLibrary.generated.h"

UCLASS()
class CLOUDRENDERING_API UEditorWidgetFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable)
	static void ExportTextures(const FString& InExportPath);

	UFUNCTION(BlueprintCallable)
	static void ReimportTextures();

private:
	static FString ExportPath;
	static TArray<class UObject*> AssetsToExport;
};
