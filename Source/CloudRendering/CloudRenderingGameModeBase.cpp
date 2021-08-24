// Copyright Epic Games, Inc. All Rights Reserved.
#include "CloudRenderingGameModeBase.h"
#include "Exporters/Exporter.h"
#include "AssetExportTask.h"
#include "UObject/GCObjectScopeGuard.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/StaticMeshActor.h"
#include "Animation/SkeletalMeshActor.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SplineComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Rendering/Texture2DResource.h"
#include "Engine/SplineMeshActor.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Runtime/ImageWrapper/Public/IImageWrapper.h"
#include "Runtime/ImageWrapper/Public/IImageWrapperModule.h"

ACloudRenderingGameModeBase::ACloudRenderingGameModeBase()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	SplineComponent = CreateDefaultSubobject<USplineComponent>(TEXT("SplineComponent"));
	SplineComponent->SetupAttachment(Root);

	SceneCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("SceneCapture"));
	SceneCapture->SetupAttachment(Root);
	SceneCapture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;
	SceneCapture->bCaptureEveryFrame = false;
	SceneCapture->bCaptureOnMovement = false;

	LandscapeSize = FVector2D(40000.0f, 30000.0f);
	ContinuousSpan = 400.0f;
	ViewPitchRange = FVector2D(-60.0f, -15.0f);

	DataPath = FString::Printf(TEXT("%sData"), *FPaths::ProjectContentDir());
	TaskState = ETaskState::TS_Finish;
}

void ACloudRenderingGameModeBase::Export(const FString& ExportPath)
{
	UAssetExportTask* ExportTask = NewObject<UAssetExportTask>();
	FGCObjectScopeGuard ExportTaskGuard(ExportTask);

	ExportTask->Object = GetWorld();
	ExportTask->Exporter = UExporter::FindExporter(ExportTask->Object, TEXT("FBX"));
	ExportTask->Filename = ExportPath;
	ExportTask->bSelected = false;
	ExportTask->bReplaceIdentical = true;
	ExportTask->bPrompt = false;
	ExportTask->bUseFileArchive = true;
	ExportTask->bWriteEmptyFiles = false;
	ExportTask->bAutomated = true;

	UExporter::RunAssetExportTask(ExportTask);
}

void ACloudRenderingGameModeBase::BeginPlay()
{
	Super::BeginPlay();

	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);
	UWidgetBlueprintLibrary::SetInputMode_GameAndUIEx(PlayerController);
	PlayerController->SetShowMouseCursor(true);

	DiscreteDataTableMap = {
		{ TEXT("tree"), TreeDataTable }, 
		{ TEXT("grass"), GrassDataTable },
		{ TEXT("rock"), RockDataTable },
		{ TEXT("house"), HouseDataTable },
		{ TEXT("mount"), HillDataTable },
	};

	ContinuousDataTableMap = {
		{ TEXT("road"), RoadDataTable },
		{ TEXT("river"), RiverDataTable }
	};

	// 查找已有的所有任务列表
	TArray<FString> DirectoryNames = FindNonEmptyDirectoryNames();
	for (const FString& DirectoryName : DirectoryNames)
	{
		LastTasks.Add(DirectoryName);
	}

	GetWorldTimerManager().SetTimer(PollTimerHandle, this, &ACloudRenderingGameModeBase::Poll, 1.0f, true);
}

void ACloudRenderingGameModeBase::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (TaskState == ETaskState::TS_Finish)
	{
		if (!TaskQueue.IsEmpty())
		{
			TaskQueue.Dequeue(Task);
			TaskState = ETaskState::TS_Brush;

			UE_LOG(LogClass, Log, TEXT("Start task %s"), *Task);
		}
	}
	else if (TaskState == ETaskState::TS_Brush)
	{
		UE_LOG(LogClass, Log, TEXT("[%s]: generating landscape..."), *Task);
		LoadBrush();
		UE_LOG(LogClass, Log, TEXT("[%s]: generating landscape finished!"), *Task);
		TaskState = ETaskState::TS_Snapshot;
	}
	else if (TaskState == ETaskState::TS_Snapshot)
	{
		UE_LOG(LogClass, Log, TEXT("[%s]: generating snapshots..."), *Task);
		GenSnapshots(6);
		UE_LOG(LogClass, Log, TEXT("[%s]: generating snapshots finished!"), *Task);
		TaskState = ETaskState::TS_Finish;
	}
}

void ACloudRenderingGameModeBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	GetWorldTimerManager().ClearTimer(PollTimerHandle);
}

void ACloudRenderingGameModeBase::Poll()
{
	TArray<FString> DirectoryNames = FindNonEmptyDirectoryNames();
	for (const FString& DirectoryName : DirectoryNames)
	{
		if (!LastTasks.Contains(DirectoryName))
		{
			LastTasks.Add(DirectoryName);
			TaskQueue.Enqueue(DirectoryName);
		}
	}
}

void ACloudRenderingGameModeBase::LoadBrush()
{
	// 清除场景中的所有物体
	for (AActor* Actor : Actors)
	{
		Actor->Destroy();
	}
	Actors.Empty();

	TSharedPtr<FJsonObject> RootJsonObject;
	FString JsonFilePath = FString::Printf(TEXT("%s/%s/brush.json"), *DataPath, *Task);
	LoadJsonFile(JsonFilePath, RootJsonObject);

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// 摆放离散型物体
	for (const auto& Iter : DiscreteDataTableMap)
	{
		const FString& Type = Iter.Key;
		UDataTable* DataTable = Iter.Value;

		if (!RootJsonObject->HasField(Type))
		{
			UE_LOG(LogClass, Log, TEXT("Discrete field %s doesn't exists!"), *Type);
			continue;
		}

		// 解析位置信息
		const auto& Instances = RootJsonObject->GetArrayField(Type);
		for (const auto& Instance : Instances)
		{
			const auto& InstanceObject = Instance->AsObject();
			FVector Location = GetParsedPos(InstanceObject->GetStringField(TEXT("pos")));
			FRotator Rotation = FRotator(0.0f, InstanceObject->GetNumberField(TEXT("yaw")), 0.0f);

			// 创建模型蓝图
			FMeshRow* MeshRow = GetRandomMeshRow(DataTable);
			if (MeshRow->StaticMesh)
			{
				AStaticMeshActor* StaticMeshActor = GetWorld()->SpawnActor<AStaticMeshActor>(
					AStaticMeshActor::StaticClass(), Location, Rotation, SpawnParameters);
				StaticMeshActor->SetMobility(EComponentMobility::Movable);
				StaticMeshActor->GetStaticMeshComponent()->SetStaticMesh(MeshRow->StaticMesh);

				Actors.Add(StaticMeshActor);
			}
			else if (MeshRow->SkeletalMesh)
			{
				ASkeletalMeshActor* SkeletalMeshActor = GetWorld()->SpawnActor<ASkeletalMeshActor>(
					ASkeletalMeshActor::StaticClass(), Location, Rotation, SpawnParameters);
				SkeletalMeshActor->GetSkeletalMeshComponent()->SetMobility(EComponentMobility::Movable);
				SkeletalMeshActor->GetSkeletalMeshComponent()->SetSkeletalMesh(MeshRow->SkeletalMesh);

				Actors.Add(SkeletalMeshActor);
			}
		}
	}

	// 摆放连续型物体
	for (const auto& Iter : ContinuousDataTableMap)
	{
		const FString& Type = Iter.Key;
		UDataTable* DataTable = Iter.Value;
		if (!RootJsonObject->HasField(Type))
		{
			UE_LOG(LogClass, Log, TEXT("Continuous field %s doesn't exists!"), *Type);
			continue;
		}

		FMeshRow* MeshRow = GetRandomMeshRow(DataTable);

		// 解析位置信息
		const auto& Instances = RootJsonObject->GetArrayField(Type);
		for (const auto& Instance : Instances)
		{
			const auto& InstanceObject = Instance->AsObject();

			// 读取宽度
			float Width = GetRelativeWidth(InstanceObject->GetNumberField(TEXT("width")));

			// 构造样条曲线
			SplineComponent->ClearSplinePoints();
			const auto& CurveJsonArray = InstanceObject->GetArrayField(TEXT("curve"));
			for (const auto& CurveJsonValue : CurveJsonArray)
			{
				FVector Location = GetParsedPos(CurveJsonValue->AsString());
				SplineComponent->AddSplinePoint(Location, ESplineCoordinateSpace::World);
			}

			// 采样样条曲线，创建曲线模型
			float SplineLength = 0.0f;
			while (true)
			{
				float NextSplineLength = FMath::Min(SplineLength + ContinuousSpan, SplineComponent->GetSplineLength());
				if (SplineComponent->GetSplineLength() - NextSplineLength < ContinuousSpan * 0.1f)
				{
					break;
				}

				FVector StartPos = SplineComponent->GetLocationAtDistanceAlongSpline(SplineLength, ESplineCoordinateSpace::Local);
				FVector StartTangent = (SplineComponent->GetTangentAtDistanceAlongSpline(SplineLength, ESplineCoordinateSpace::Local)).GetSafeNormal() * ContinuousSpan;
				FVector EndPos = SplineComponent->GetLocationAtDistanceAlongSpline(NextSplineLength, ESplineCoordinateSpace::Local);
				FVector EndTangent = (SplineComponent->GetTangentAtDistanceAlongSpline(NextSplineLength, ESplineCoordinateSpace::Local)).GetSafeNormal() * ContinuousSpan;

				ASplineMeshActor* SplineMeshActor = GetWorld()->SpawnActor<ASplineMeshActor>(
					ASplineMeshActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParameters);
				SplineMeshActor->GetSplineMeshComponent()->SetMobility(EComponentMobility::Movable);
				SplineMeshActor->GetSplineMeshComponent()->SetStaticMesh(MeshRow->StaticMesh);
				SplineMeshActor->GetSplineMeshComponent()->SetStartAndEnd(StartPos, StartTangent, EndPos, EndTangent);
				SplineMeshActor->GetSplineMeshComponent()->SetStartScale(FVector2D(Width));
				SplineMeshActor->GetSplineMeshComponent()->SetEndScale(FVector2D(Width));
				Actors.Add(SplineMeshActor);
				SplineLength = NextSplineLength;
			}
		}
	}
	SplineComponent->ConditionalBeginDestroy();

	// 更新场景包围盒
	BoundBox.Init();
	for (AActor* Actor : Actors)
	{
		BoundBox += Actor->GetComponentsBoundingBox();
	}

	if (BoundBox.GetSize().X < 1000.0f || BoundBox.GetSize().Y < 1000.0f)
	{
		BoundBox = BoundBox.ExpandBy(BoundBox.GetSize() * 1.5f);
	}
	BoundBox.Min.Z = FMath::Max(BoundBox.Min.Z, 0.0f);
}

void ACloudRenderingGameModeBase::GenSnapshots(int Num)
{
	for (int i = 0; i < Num; ++i)
	{
		FVector Location = UKismetMathLibrary::RandomPointInBoundingBox(BoundBox.GetCenter(), BoundBox.GetExtent());
		FVector Target = UKismetMathLibrary::RandomPointInBoundingBox(BoundBox.GetCenter(), BoundBox.GetExtent());
		Target.Z = 0.0f;

		FRotator Rotation = (Target - Location).Rotation();

		SceneCapture->SetWorldLocationAndRotation(Location, Rotation);
		SceneCapture->CaptureScene();

		FString ImageFileName = FString::Printf(TEXT("%s/%s/snapshot_%d.png"), *DataPath, *Task, i);
		ExportRenderTarget(SceneCapture->TextureTarget, ImageFileName);
	}
}

bool ACloudRenderingGameModeBase::LoadJsonFile(const FString& JsonFilePath, TSharedPtr<FJsonObject>& RootJsonObject)
{
	FArchive* JsonFile = IFileManager::Get().CreateFileReader(*JsonFilePath);
	if (!JsonFile)
	{
		UE_LOG(LogClass, Error, TEXT("Can not open json file: %s"), *JsonFilePath);
		return false;
	}

	bool bJsonLoaded = false;
	RootJsonObject = MakeShareable(new FJsonObject());
	TSharedRef<TJsonReader<ANSICHAR>> JsonReader = TJsonReaderFactory<ANSICHAR>::Create(JsonFile);
	bJsonLoaded = FJsonSerializer::Deserialize(JsonReader, RootJsonObject);
	JsonFile->Close();

	return bJsonLoaded;
}

bool ACloudRenderingGameModeBase::ExportRenderTarget(class UTextureRenderTarget2D* RenderTarget, const FString& FileName)
{
	TArray<FColor> Data;
	Data.AddUninitialized(RenderTarget->GetSurfaceWidth() * RenderTarget->GetSurfaceHeight());
	RenderTarget->GameThread_GetRenderTargetResource()->ReadPixels(Data, FReadSurfaceDataFlags(RCM_UNorm));
	for (FColor& color : Data)
	{
		color.A = 255;
	}

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::JPEG);
	if (ImageWrapper->SetRaw(&Data[0], Data.Num() * sizeof(FColor), RenderTarget->GetSurfaceWidth(), RenderTarget->GetSurfaceHeight(), ERGBFormat::BGRA, 8))
	{
		const TArray64<uint8>& CompressedData = ImageWrapper->GetCompressed();
		return FFileHelper::SaveArrayToFile(CompressedData, *FileName);
	}
	return false;
}

TArray<FString> ACloudRenderingGameModeBase::FindNonEmptyDirectoryNames()
{
	TArray<FString> FoundNonEmptyDirectoryNames;
	TArray<FString> FoundDirectoryNames;
	IFileManager::Get().FindFiles(FoundDirectoryNames, *FPaths::Combine(DataPath, TEXT("*")), false, true);
	for (const FString& FoundDirectoryName : FoundDirectoryNames)
	{
		FString BrushFileName = FString::Printf(TEXT("%s/%s/brush.json"), *DataPath, *FoundDirectoryName);
		if (FPaths::FileExists(BrushFileName))
		{
			FoundNonEmptyDirectoryNames.Add(FoundDirectoryName);
		}
	}

	return FoundNonEmptyDirectoryNames;
}

FMeshRow* ACloudRenderingGameModeBase::GetRandomMeshRow(class UDataTable* DataTable)
{
	FString ContextString;
	TArray<FMeshRow*> MeshRows;
	DataTable->GetAllRows(ContextString, MeshRows);
	
	return MeshRows[UKismetMathLibrary::RandomIntegerInRange(0, MeshRows.Num() - 1)];
}

FVector ACloudRenderingGameModeBase::GetParsedPos(const FString& PosStr)
{
	FString XStr, YStr;
	PosStr.Split(TEXT(","), &XStr, &YStr);
	float X = FCString::Atof(*XStr);
	float Y = FCString::Atof(*YStr);

	return FVector(UKismetMathLibrary::MapRangeClamped(X, 0.0f, 1.0f, -LandscapeSize.X * 0.5f, LandscapeSize.X * 0.5f),
		UKismetMathLibrary::MapRangeClamped(Y, 0.0f, 1.0f, -LandscapeSize.Y * 0.5f, LandscapeSize.Y * 0.5f), 0.0f);
}

float ACloudRenderingGameModeBase::GetRelativeWidth(float Width)
{
	return Width * LandscapeSize.Size() / ContinuousSpan;
}
