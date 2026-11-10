// Editor-only numeric lighting authoring. Never creates a world or starts Play.
#include "VerdantWorld.h"

#if WITH_EDITOR
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	constexpr double TerrainExtentCm = 20000.0;
	constexpr double FirstSampleCm = 100.0;
	constexpr double LastSampleCm = 50000.0;
	constexpr double SampleStepCm = 50.0;

	TSharedPtr<FJsonObject> VectorJson(const FVector& Vector)
	{
		auto Object = MakeShared<FJsonObject>();
		Object->SetNumberField(TEXT("x"), Vector.X);
		Object->SetNumberField(TEXT("y"), Vector.Y);
		Object->SetNumberField(TEXT("z"), Vector.Z);
		return Object;
	}

	struct FHorizonSample
	{
		double ElevationDegrees = -90.0;
		double DistanceCm = 0.0;
		FVector Point = FVector::ZeroVector;
		int32 SampleCount = 0;

		TSharedPtr<FJsonObject> ToJson() const
		{
			auto Object = MakeShared<FJsonObject>();
			Object->SetNumberField(TEXT("horizon_degrees"), ElevationDegrees);
			Object->SetNumberField(TEXT("blocking_distance_cm"), DistanceCm);
			Object->SetObjectField(TEXT("blocking_point_cm"), VectorJson(Point));
			Object->SetNumberField(TEXT("sample_count"), SampleCount);
			return Object;
		}
	};

	FHorizonSample InspectHorizon(const FVector& Origin, double AzimuthDegrees)
	{
		FHorizonSample Result;
		const double Angle = FMath::DegreesToRadians(AzimuthDegrees);
		const FVector Direction(FMath::Cos(Angle), FMath::Sin(Angle), 0.0);
		for (double Distance = FirstSampleCm; Distance <= LastSampleCm; Distance += SampleStepCm)
		{
			FVector Point = Origin + Direction * Distance;
			if (FMath::Abs(Point.X) > TerrainExtentCm || FMath::Abs(Point.Y) > TerrainExtentCm)
				break; // A ray from inside this square cannot re-enter it.
			Point.Z = AVerdantWorld::HeightAt(static_cast<float>(Point.X), static_cast<float>(Point.Y));
			const double Elevation = FMath::RadiansToDegrees(FMath::Atan2(Point.Z - Origin.Z, Distance));
			++Result.SampleCount;
			if (Elevation > Result.ElevationDegrees)
			{
				Result.ElevationDegrees = Elevation;
				Result.DistanceCm = Distance;
				Result.Point = Point;
			}
		}
		return Result;
	}

	void InspectSunrise()
	{
		const FVector Arrival(-1000.0, -5800.0,
		                      AVerdantWorld::HeightAt(-1000.f, -5800.f) + 100.0);
		const FVector Camera = Arrival - FRotator(0.0, 78.0, 0.0).Vector() * 280.0;
		auto Report = MakeShared<FJsonObject>();
		Report->SetStringField(
			TEXT("purpose"), TEXT("Terrain-only sunrise authoring without Play or screenshot capture"));
		Report->SetStringField(TEXT("limitations"),
		                       TEXT(
			                       "Approximate initial camera and player eye positions. Uses HeightAt rather than rendered triangle interpolation; excludes trees, props and clouds."));
		Report->SetObjectField(TEXT("player_eye_cm"), VectorJson(Arrival));
		Report->SetObjectField(TEXT("approximate_camera_cm"), VectorJson(Camera));
		Report->SetNumberField(TEXT("terrain_extent_cm"), TerrainExtentCm);
		Report->SetNumberField(TEXT("sample_step_cm"), SampleStepCm);
		Report->SetNumberField(TEXT("first_sample_cm"), FirstSampleCm);
		Report->SetNumberField(TEXT("maximum_sample_distance_cm"), LastSampleCm);
		Report->SetNumberField(TEXT("clearance_degrees"), 1.5);

		TArray<TSharedPtr<FJsonValue>> Directions;
		for (const double Azimuth : {42.0, 48.0, 54.0, 60.0, 66.0, 72.0, 78.0, 84.0, 90.0, 96.0})
		{
			const FHorizonSample CameraHorizon = InspectHorizon(Camera, Azimuth);
			const FHorizonSample PlayerHorizon = InspectHorizon(Arrival, Azimuth);
			const double MaximumHorizon = FMath::Max(CameraHorizon.ElevationDegrees, PlayerHorizon.ElevationDegrees);
			auto Direction = MakeShared<FJsonObject>();
			Direction->SetNumberField(TEXT("azimuth_degrees"), Azimuth);
			Direction->SetObjectField(TEXT("camera"), CameraHorizon.ToJson());
			Direction->SetObjectField(TEXT("player"), PlayerHorizon.ToJson());
			Direction->SetNumberField(TEXT("recommended_elevation_degrees"), FMath::Max(5.0, MaximumHorizon + 1.5));
			Directions.Add(MakeShared<FJsonValueObject>(Direction));
		}
		Report->SetArrayField(TEXT("directions"), Directions);

		FString Json;
		const auto Writer = TJsonWriterFactory<>::Create(&Json);
		if (!FJsonSerializer::Serialize(Report, Writer))
		{
			UE_LOG(LogTemp, Error, TEXT("VERDANT_SUNRISE_AUTHORING could not serialize horizon report"));
			return;
		}
		const FString Directory = FPaths::ProjectSavedDir() / TEXT("Validation");
		IFileManager::Get().MakeDirectory(*Directory, true);
		const FString Filename = Directory / TEXT("sunrise-horizon.json");
		if (!FFileHelper::SaveStringToFile(Json, *Filename, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			UE_LOG(LogTemp, Error, TEXT("VERDANT_SUNRISE_AUTHORING could not save %s"), *Filename);
			return;
		}
		UE_LOG(LogTemp, Display, TEXT("VERDANT_SUNRISE_AUTHORING saved %s; no Play or capture performed"), *Filename);
	}

	FAutoConsoleCommand InspectSunriseCommand(
		TEXT("Verdant.InspectSunrise"),
		TEXT("Write terrain horizons for authoring a visible sunrise; does not start Play."),
		FConsoleCommandDelegate::CreateStatic(&InspectSunrise));
}
#endif
