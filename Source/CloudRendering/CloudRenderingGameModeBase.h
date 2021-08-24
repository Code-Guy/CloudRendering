// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "CloudRenderingGameModeBase.generated.h"

// 模型数据表项
USTRUCT(BlueprintType)
struct FMeshRow : public FTableRowBase
{
public:
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	UStaticMesh* StaticMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	USkeletalMesh* SkeletalMesh;
};

// Task步骤
enum class ETaskState
{
	TS_Brush, TS_Snapshot, TS_Finish
};

/**
 * 
 */
UCLASS()
class CLOUDRENDERING_API ACloudRenderingGameModeBase : public AGameModeBase
{
	GENERATED_BODY()

public:
	ACloudRenderingGameModeBase();

	UPROPERTY(EditDefaultsOnly, Category = "Parameter")
	FVector2D LandscapeSize;

	UPROPERTY(EditDefaultsOnly, Category = "Parameter")
	float ContinuousSpan;

	UPROPERTY(EditDefaultsOnly, Category = "DataTable")
	class UDataTable* TreeDataTable;

	UPROPERTY(EditDefaultsOnly, Category = "DataTable")
	class UDataTable* GrassDataTable;

	UPROPERTY(EditDefaultsOnly, Category = "DataTable")
	class UDataTable* RockDataTable;

	UPROPERTY(EditDefaultsOnly, Category = "DataTable")
	class UDataTable* HouseDataTable;

	UPROPERTY(EditDefaultsOnly, Category = "DataTable")
	class UDataTable* HillDataTable;

	UPROPERTY(EditDefaultsOnly, Category = "DataTable")
	class UDataTable* RoadDataTable;

	UPROPERTY(EditDefaultsOnly, Category = "DataTable")
	class UDataTable* RiverDataTable;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void Poll();
	
	void LoadBrush();
	void GenSnapshots(int Num);
	bool LoadJsonFile(const FString& JsonFilePath, TSharedPtr<FJsonObject>& RootJsonObject);
	bool ExportRenderTarget(class UTextureRenderTarget2D* RenderTarget, const FString& FileName);
	void Export(const FString& ExportPath);
	TArray<FString> FindNonEmptyDirectoryNames();

	FMeshRow* GetRandomMeshRow(class UDataTable* DataTable);
	FVector GetParsedPos(const FString& PosStr);
	float GetRelativeWidth(float Width);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"), Category = "CloudRendering")
	class USplineComponent* SplineComponent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"), Category = "CloudRendering")
	class USceneCaptureComponent2D* SceneCapture;

	TMap<FString, class UDataTable*> DiscreteDataTableMap;
	TMap<FString, class UDataTable*> ContinuousDataTableMap;
	TArray<AActor*> Actors;
	FBox BoundBox;

	FString DataPath;
	TArray<FString> LastTasks;
	TQueue<FString> TaskQueue;
	FString Task;
	FTimerHandle PollTimerHandle;

	ETaskState TaskState;
};
