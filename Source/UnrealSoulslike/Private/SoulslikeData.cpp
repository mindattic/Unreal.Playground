#include "SoulslikeData.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogSoulslikeData, Log, All);

namespace
{
	TSharedPtr<FJsonObject> LoadJson(const FString& RelativeToContent)
	{
		const FString Path = FPaths::ProjectContentDir() / RelativeToContent;
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			UE_LOG(LogSoulslikeData, Warning, TEXT("Missing %s"), *Path);
			return nullptr;
		}
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			UE_LOG(LogSoulslikeData, Error, TEXT("Could not parse %s"), *Path);
			return nullptr;
		}
		return Root;
	}

	bool ReadUV(const TArray<TSharedPtr<FJsonValue>>& Pair, FVector2D& Out)
	{
		if (Pair.Num() != 2)
		{
			return false;
		}
		Out = FVector2D(Pair[0]->AsNumber(), Pair[1]->AsNumber());
		return true;
	}

	void ReadPoint(const TSharedPtr<FJsonObject>& Root, const TCHAR* Field, FVector2D& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Pair = nullptr;
		if (Root->TryGetArrayField(Field, Pair))
		{
			ReadUV(*Pair, Out);
		}
	}

	void ReadPoints(const TSharedPtr<FJsonObject>& Root, const TCHAR* Field, TArray<FVector2D>& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
		if (!Root->TryGetArrayField(Field, List))
		{
			return;
		}
		for (const TSharedPtr<FJsonValue>& Item : *List)
		{
			FVector2D UV;
			if (ReadUV(Item->AsArray(), UV))
			{
				Out.Add(UV);
			}
		}
	}

	void ReadSegments(const TSharedPtr<FJsonObject>& Root, const TCHAR* Field, TArray<TPair<FVector2D, FVector2D>>& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
		if (!Root->TryGetArrayField(Field, List))
		{
			return;
		}
		for (const TSharedPtr<FJsonValue>& Item : *List)
		{
			const TArray<TSharedPtr<FJsonValue>>& Ends = Item->AsArray();
			FVector2D A, B;
			if (Ends.Num() == 2 && ReadUV(Ends[0]->AsArray(), A) && ReadUV(Ends[1]->AsArray(), B))
			{
				Out.Add({ A, B });
			}
		}
	}

	FSoulslikeVisual ReadVisual(const TSharedPtr<FJsonObject>& Object)
	{
		FSoulslikeVisual Visual;
		if (!Object.IsValid())
		{
			return Visual;
		}
		Object->TryGetStringField(TEXT("name"), Visual.Name);
		Object->TryGetStringField(TEXT("mesh"), Visual.Mesh);
		const TArray<TSharedPtr<FJsonValue>>* Attachments = nullptr;
		if (Object->TryGetArrayField(TEXT("attachments"), Attachments))
		{
			for (const TSharedPtr<FJsonValue>& Item : *Attachments)
			{
				const TSharedPtr<FJsonObject> A = Item->AsObject();
				FSoulslikeAttachment Attachment;
				FString Socket;
				if (A.IsValid() && A->TryGetStringField(TEXT("socket"), Socket) && A->TryGetStringField(TEXT("mesh"), Attachment.Mesh))
				{
					Attachment.Socket = FName(*Socket);
					Visual.Attachments.Add(Attachment);
				}
			}
		}
		return Visual;
	}
}

const FHebridesTerrain& FHebridesTerrain::Get()
{
	static FHebridesTerrain Instance = []
	{
		FHebridesTerrain Terrain;
		Terrain.Load();
		return Terrain;
	}();
	return Instance;
}

void FHebridesTerrain::Load()
{
	const FString HeightmapPath = FPaths::ProjectContentDir() / TEXT("Terrain/HebridesHeightmap.r16");
	TArray<uint8> Raw;
	if (!FFileHelper::LoadFileToArray(Raw, *HeightmapPath))
	{
		UE_LOG(LogSoulslikeData, Error, TEXT("Missing heightmap %s"), *HeightmapPath);
		return;
	}

	const int32 SampleCount = Raw.Num() / 2;
	const int32 Width = FMath::RoundToInt(FMath::Sqrt(static_cast<float>(SampleCount)));
	if (Width < 2 || Width * Width * 2 != Raw.Num())
	{
		UE_LOG(LogSoulslikeData, Error, TEXT("Heightmap %s must be square raw uint16 (%d bytes)"), *HeightmapPath, Raw.Num());
		return;
	}

	Heights.SetNumUninitialized(SampleCount);
	for (int32 i = 0; i < SampleCount; ++i)
	{
		const uint16 Sample = static_cast<uint16>(Raw[i * 2] | (Raw[i * 2 + 1] << 8));
		Heights[i] = Sample / 65535.f;
	}
	Resolution = Width;

	if (const TSharedPtr<FJsonObject> Root = LoadJson(TEXT("Terrain/HebridesLandmarks.json")))
	{
		Root->TryGetNumberField(TEXT("sea_level"), SeaLevel);
		ReadPoint(Root, TEXT("landing_beach"), LandingBeach);
		ReadPoint(Root, TEXT("broch"), Broch);
		ReadPoint(Root, TEXT("standing_stones"), StandingStones);
		ReadPoint(Root, TEXT("keep"), Keep);
		ReadPoint(Root, TEXT("boss"), Boss);
		ReadPoints(Root, TEXT("player_starts"), PlayerStarts);
		ReadPoints(Root, TEXT("crofts"), Crofts);
		ReadPoints(Root, TEXT("skeleton_spawns"), SkeletonSpawns);
		ReadPoints(Root, TEXT("bridge"), Bridge);
		ReadPoints(Root, TEXT("bridge_guards"), BridgeGuards);
		Root->TryGetNumberField(TEXT("bridge_deck_above_sea"), BridgeDeckAboveSea);
		ReadPoints(Root, TEXT("buried_houses"), BuriedHouses);
		ReadSegments(Root, TEXT("buried_walls"), BuriedWalls);
		ReadSegments(Root, TEXT("field_walls"), FieldWalls);
		const TSharedPtr<FJsonObject>* Dunes = nullptr;
		if (Root->TryGetObjectField(TEXT("dunes"), Dunes))
		{
			ReadPoint(*Dunes, TEXT("centre"), DunesCentre);
			ReadPoint(*Dunes, TEXT("radius"), DunesRadius);
		}
	}
	if (PlayerStarts.IsEmpty())
	{
		PlayerStarts.Add(LandingBeach);
	}
	UE_LOG(LogSoulslikeData, Log, TEXT("Island heightmap %dx%d loaded, %d skeleton spawns"), Resolution, Resolution, SkeletonSpawns.Num());
}

float FHebridesTerrain::GridZ(int32 X, int32 Y) const
{
	if (!IsValid())
	{
		return 0.f;
	}
	X = FMath::Clamp(X, 0, Resolution - 1);
	Y = FMath::Clamp(Y, 0, Resolution - 1);
	return (Heights[Y * Resolution + X] - SeaLevel) * HeightScale;
}

float FHebridesTerrain::SampleZ(float U, float V) const
{
	if (!IsValid())
	{
		return 0.f;
	}
	const float FX = FMath::Clamp(U, 0.f, 1.f) * (Resolution - 1);
	const float FY = FMath::Clamp(V, 0.f, 1.f) * (Resolution - 1);
	const int32 X0 = FMath::FloorToInt(FX);
	const int32 Y0 = FMath::FloorToInt(FY);
	const float TX = FX - X0;
	const float TY = FY - Y0;
	const float Top = FMath::Lerp(GridZ(X0, Y0), GridZ(X0 + 1, Y0), TX);
	const float Bottom = FMath::Lerp(GridZ(X0, Y0 + 1), GridZ(X0 + 1, Y0 + 1), TX);
	return FMath::Lerp(Top, Bottom, TY);
}

FVector FHebridesTerrain::ToWorld(const FVector2D& UV) const
{
	const float Size = WorldSize();
	return FVector((UV.X - 0.5f) * Size, (UV.Y - 0.5f) * Size, SampleZ(UV.X, UV.Y));
}

float FHebridesTerrain::DuneWeight(float U, float V) const
{
	if (DunesRadius.X <= 0.f || DunesRadius.Y <= 0.f)
	{
		return 0.f;
	}
	const float Ellipse = FMath::Sqrt(FMath::Square((U - DunesCentre.X) / DunesRadius.X) + FMath::Square((V - DunesCentre.Y) / DunesRadius.Y));
	return 1.f - FMath::SmoothStep(0.75f, 1.f, Ellipse);
}

FVector2D FHebridesTerrain::ToUV(const FVector& World) const
{
	const float Size = FMath::Max(WorldSize(), 1.f);
	return FVector2D(World.X / Size + 0.5f, World.Y / Size + 0.5f);
}

const FSoulslikeAssets& FSoulslikeAssets::Get()
{
	static FSoulslikeAssets Instance = []
	{
		FSoulslikeAssets Assets;
		Assets.Load();
		return Assets;
	}();
	return Instance;
}

void FSoulslikeAssets::Load()
{
	const TSharedPtr<FJsonObject> Root = LoadJson(TEXT("Characters/CharacterAssets.json"));
	if (!Root.IsValid())
	{
		return;
	}

	const TSharedPtr<FJsonObject>* Object = nullptr;
	if (Root->TryGetObjectField(TEXT("knight"), Object))
	{
		Knight = ReadVisual(*Object);
	}
	if (Root->TryGetObjectField(TEXT("boss"), Object))
	{
		Boss = ReadVisual(*Object);
	}
	const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
	if (Root->TryGetArrayField(TEXT("skeletons"), List))
	{
		for (const TSharedPtr<FJsonValue>& Item : *List)
		{
			FSoulslikeVisual Visual = ReadVisual(Item->AsObject());
			if (Visual.IsSet())
			{
				Skeletons.Add(Visual);
			}
		}
	}
	if (Root->TryGetObjectField(TEXT("anims"), Object))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Object)->Values)
		{
			Anims.Add(FName(*Pair.Key), Pair.Value->AsString());
		}
	}
	UE_LOG(LogSoulslikeData, Log, TEXT("Character assets: knight=%s, %d skeletons, %d anims"), Knight.IsSet() ? TEXT("yes") : TEXT("no"), Skeletons.Num(), Anims.Num());
}

const FEnvironmentAssets& FEnvironmentAssets::Get()
{
	static FEnvironmentAssets Instance = []
	{
		FEnvironmentAssets Assets;
		Assets.Load();
		return Assets;
	}();
	return Instance;
}

void FEnvironmentAssets::Load()
{
	const TSharedPtr<FJsonObject> Root = LoadJson(TEXT("Environment/EnvironmentAssets.json"));
	const TSharedPtr<FJsonObject>* Object = nullptr;
	if (Root.IsValid() && Root->TryGetObjectField(TEXT("pieces"), Object))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Object)->Values)
		{
			Pieces.Add(FName(*Pair.Key), Pair.Value->AsString());
		}
	}
	UE_LOG(LogSoulslikeData, Log, TEXT("Environment kit: %d pieces"), Pieces.Num());
}
