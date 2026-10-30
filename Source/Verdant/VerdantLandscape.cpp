// Original runtime valley composition for Verdant. All dimensions are centimetres.
#include "VerdantWorld.h"
#include "ProceduralMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace VerdantLand
{
    constexpr float Extent = 20000.f;
    constexpr float Spacing = 250.f;
    constexpr int32 Cells = 160;
    const TCHAR* Sphere = TEXT("/Engine/BasicShapes/Sphere.Sphere");
    const TCHAR* Cylinder = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
    const TCHAR* Cone = TEXT("/Engine/BasicShapes/Cone.Cone");

    float Ease(float A, float B, float X)
    {
        const float T = FMath::Clamp((X - A) / (B - A), 0.f, 1.f);
        return T * T * (3.f - 2.f * T);
    }

    float SegmentDistance(FVector2D P, FVector2D A, FVector2D B)
    {
        const FVector2D AB = B - A;
        const float T = FMath::Clamp(float(FVector2D::DotProduct(P - A, AB) / AB.SizeSquared()), 0.f, 1.f);
        return FVector2D::Distance(P, A + T * AB);
    }

    float RoadDistance(float X, float Y)
    {
        const FVector2D P(X, Y);
        const FVector2D Hub(0, 1000);
        float D = SegmentDistance(P, FVector2D(-1000, -6200), Hub);
        D = FMath::Min(D, SegmentDistance(P, Hub, FVector2D(-4500, -1500)));
        D = FMath::Min(D, SegmentDistance(P, Hub, FVector2D(4200, -1000)));
        D = FMath::Min(D, SegmentDistance(P, Hub, FVector2D(0, 3600)));
        D = FMath::Min(D, SegmentDistance(P, FVector2D(0, 3600), FVector2D(500, 6500)));
        D = FMath::Min(D, SegmentDistance(P, FVector2D(-4500, -1500), FVector2D(-7400, -2100)));
        D = FMath::Min(D, SegmentDistance(P, FVector2D(4200, -1000), FVector2D(7400, -2100)));
        D = FMath::Min(D, SegmentDistance(P, FVector2D(500, 6500), FVector2D(500, 9700)));
        return FMath::Min(D, float(FMath::Abs((P - FVector2D(0, 1600)).Size() - 8200.f)));
    }

    float Landform(float X, float Y)
    {
        const float R = FMath::Sqrt(X * X + Y * Y);
        const float A = FMath::Atan2(Y, X);
        const float Rim = Ease(10500.f, 19000.f, R);
        float H = 65.f + 100.f * FMath::Sin(X / 4900.f) * FMath::Cos(Y / 4200.f);
        H += Rim * (1800.f + 520.f * FMath::Sin(A * 5.f + .6f) + 280.f * FMath::Cos(A * 9.f));
        H += 2700.f * Ease(16500.f, 19800.f, R);
        H += 780.f * FMath::Exp(-FMath::Square((X + 10500.f) / 5200.f) - FMath::Square((Y - 1500.f) / 8500.f));
        H += 1150.f * FMath::Exp(-FMath::Square((X - 4500.f) / 7300.f) - FMath::Square((Y - 13500.f) / 4500.f));
        // A shallow dry drainage folds around the western side of the basin.
        const float Drain = X + 1700.f - 560.f * FMath::Sin(Y / 2800.f);
        H -= 65.f * FMath::Exp(-FMath::Square(Drain / 700.f)) * (1.f - Ease(7500.f, 11500.f, FMath::Abs(Y)));
        return H;
    }
}

float AVerdantWorld::HeightAt(float X, float Y)
{
    using namespace VerdantLand;
    float H = Landform(X, Y);
    const FVector2D Clearings[] = { FVector2D(-4500, -1500), FVector2D(4200, -1000), FVector2D(500, 6500), FVector2D(0, 3600) };
    for (const FVector2D& C : Clearings)
    {
        const float W = .9f * (1.f - Ease(450.f, 1450.f, FVector2D::Distance(FVector2D(X, Y), C)));
        H = FMath::Lerp(H, Landform(C.X, C.Y), W);
    }
    const float Detail = 10.f * FMath::Sin(X / 410.f) * FMath::Cos(Y / 360.f)
        + 7.f * FMath::Sin((X + Y) / 225.f);
    return H + Detail * Ease(400.f, 1000.f, RoadDistance(X, Y));
}

FVector AVerdantWorld::SurfacePoint(FVector P, float Lift) const
{
    // Interpolate the same grid triangles as collision, avoiding floating props
    // and vertical oscillation when NPCs follow the analytic surface.
    using namespace VerdantLand;
    const float GX = FMath::Clamp(float((P.X + Extent) / Spacing), 0.f, float(Cells) - .0001f);
    const float GY = FMath::Clamp(float((P.Y + Extent) / Spacing), 0.f, float(Cells) - .0001f);
    const int32 IX = FMath::FloorToInt(GX), IY = FMath::FloorToInt(GY);
    const float U = GX - IX, V = GY - IY;
    const float X = -Extent + IX * Spacing, Y = -Extent + IY * Spacing;
    const float H00 = HeightAt(X, Y), H10 = HeightAt(X + Spacing, Y);
    const float H01 = HeightAt(X, Y + Spacing), H11 = HeightAt(X + Spacing, Y + Spacing);
    const float Z = U >= V
        ? H00 * (1.f - U) + H10 * (U - V) + H11 * V
        : H00 * (1.f - V) + H11 * U + H01 * (V - U);
    P.Z = Z + Lift;
    return P;
}

UMaterialInterface* AVerdantWorld::ColoredMaterial(FLinearColor Color, float Glow)
{
    static TMap<uint64, TWeakObjectPtr<UMaterialInstanceDynamic>> Cache;
    const uint64 Key = (uint64(Color.ToFColor(false).ToPackedARGB()) << 16)
        | uint64(FMath::Clamp(FMath::RoundToInt(Glow * 100.f), 0, 65535));
    if (const TWeakObjectPtr<UMaterialInstanceDynamic>* Existing = Cache.Find(Key))
        if (Existing->IsValid()) return Existing->Get();
    UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Art/Materials/M_Verdant.M_Verdant"));
    if (!Base) Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    if (!Base) return UMaterial::GetDefaultMaterial(MD_Surface);
    UMaterialInstanceDynamic* MI = UMaterialInstanceDynamic::Create(Base, this);
    MI->SetVectorParameterValue(TEXT("Tint"), Color);
    MI->SetScalarParameterValue(TEXT("Roughness"), .88f);
    MI->SetScalarParameterValue(TEXT("Glow"), Glow);
    Cache.Add(Key, MI);
    return MI;
}

UStaticMeshComponent* AVerdantWorld::AddShape(const FString& Name, const TCHAR* MeshPath, FVector Position, FVector Scale, FLinearColor Color, bool Collision)
{
    UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, MeshPath);
    if (!Mesh) return nullptr;
    UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this, MakeUniqueObjectName(this, UStaticMeshComponent::StaticClass(), FName(*Name)));
    AddInstanceComponent(Part);
    if (GetRootComponent()) Part->SetupAttachment(GetRootComponent());
    Part->SetMobility(EComponentMobility::Movable);
    Part->SetStaticMesh(Mesh);
    Part->SetCollisionEnabled(Collision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
    Part->SetCollisionObjectType(ECC_WorldStatic);
    Part->SetCollisionResponseToAllChannels(ECR_Block);
    Part->SetCanEverAffectNavigation(false);
    // Alpha zero is the internal sentinel for retaining CC0 atlas materials.
    if (Color.A > 0.f)
        for (int32 Slot = 0; Slot < FMath::Max(1, Mesh->GetStaticMaterials().Num()); ++Slot)
            Part->SetMaterial(Slot, ColoredMaterial(Color));
    Part->RegisterComponent();
    Part->SetWorldLocation(Position);
    Part->SetWorldScale3D(Scale);
    return Part;
}

void AVerdantWorld::BuildLandscape()
{
    using namespace VerdantLand;
    if (!Ground)
    {
        Ground = NewObject<UProceduralMeshComponent>(this, TEXT("ValleyGround"));
        AddInstanceComponent(Ground);
        if (GetRootComponent()) Ground->SetupAttachment(GetRootComponent());
        else SetRootComponent(Ground);
        Ground->RegisterComponent();
    }
    Ground->bUseAsyncCooking = false; // Collision must be ready before residents spawn.
    Ground->bUseComplexAsSimpleCollision = true;
    Ground->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Ground->SetCollisionObjectType(ECC_WorldStatic);
    Ground->SetCollisionResponseToAllChannels(ECR_Block);
    Ground->SetCanEverAffectNavigation(false);
    TArray<FVector> Verts, Normals;
    TArray<int32> Indices;
    TArray<FVector2D> UV;
    TArray<FLinearColor> Colors;
    TArray<FProcMeshTangent> Tangents;
    const int32 Row = Cells + 1;
    Verts.Reserve(Row * Row); Normals.Reserve(Row * Row); UV.Reserve(Row * Row);
    Colors.Reserve(Row * Row); Tangents.Reserve(Row * Row); Indices.Reserve(Cells * Cells * 6);
    for (int32 Y = 0; Y <= Cells; ++Y)
        for (int32 X = 0; X <= Cells; ++X)
        {
            const float PX = -Extent + X * Spacing, PY = -Extent + Y * Spacing;
            const float H = HeightAt(PX, PY);
            const float DX = (HeightAt(PX + 30, PY) - HeightAt(PX - 30, PY)) / 60.f;
            const float DY = (HeightAt(PX, PY + 30) - HeightAt(PX, PY - 30)) / 60.f;
            Verts.Emplace(PX, PY, H);
            Normals.Add(FVector(-DX, -DY, 1).GetSafeNormal());
            UV.Emplace(PX / 1800.f, PY / 1800.f);
            Tangents.Emplace(FVector(1, 0, DX).GetSafeNormal(), false);
            const float Variation = .5f + .5f * FMath::PerlinNoise2D(FVector2D(PX / 1700.f + 17, PY / 1700.f - 31));
            FLinearColor C = FMath::Lerp(FLinearColor(.12f, .23f, .075f), FLinearColor(.29f, .37f, .13f), Variation);
            C = FMath::Lerp(C, FLinearColor(.49f, .38f, .23f), 1.f - Ease(220.f, 650.f, RoadDistance(PX, PY)));
            C = FMath::Lerp(C, FLinearColor(.43f, .40f, .30f), Ease(.3f, 1.1f, FVector2D(DX, DY).Size()) * .8f);
            C = FMath::Lerp(C, FLinearColor(.48f, .51f, .43f), Ease(2400.f, 4300.f, H));
            Colors.Add(C);
        }
    for (int32 Y = 0; Y < Cells; ++Y)
        for (int32 X = 0; X < Cells; ++X)
        {
            const int32 A = Y * Row + X, B = A + 1, C = A + Row, D = C + 1;
            Indices.Append({A, D, B, A, C, D});
        }
    Ground->CreateMeshSection_LinearColor(0, Verts, Indices, Normals, UV, Colors, Tangents, true);
    if (UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Art/Materials/M_Terrain.M_Terrain")))
        Ground->SetMaterial(0, Mat);
    UE_LOG(LogTemp, Display, TEXT("VERDANT_LANDSCAPE vertices=%d triangles=%d width_m=400"), Verts.Num(), Indices.Num() / 3);
}

void AVerdantWorld::BuildScenery()
{
    using namespace VerdantLand;
    FRandomStream Random(603107);
    const FLinearColor Bark(.22f, .12f, .065f), Moss(.23f, .38f, .12f), Sand(.52f, .44f, .31f);
    const FLinearColor PreserveAtlas(1, 1, 1, 0);
    auto Native = [&](const FString& Name, const TCHAR* Path, FVector P, float Height, bool Collision) -> UStaticMeshComponent*
    {
        UStaticMesh* Asset = LoadObject<UStaticMesh>(nullptr, Path);
        if (!Asset) return nullptr;
        const FBox Box = Asset->GetBoundingBox();
        const float S = Height / FMath::Max(1.f, float(Box.GetSize().Z));
        P = SurfacePoint(P, -float(Box.Min.Z) * S - 4.f);
        UStaticMeshComponent* Part = AddShape(Name, Path, P, FVector(S), PreserveAtlas, Collision);
        if (Part) Part->SetWorldRotation(FRotator(0, Random.FRandRange(0, 360), 0));
        return Part;
    };
    auto Tree = [&](const FString& Name, FVector P, float Height, int32 Type)
    {
        const TCHAR* Path = Type == 0 ? TEXT("/Game/Art/Nature/SM_Tree01.SM_Tree01") : TEXT("/Game/Art/Nature/SM_Tree02.SM_Tree02");
        if (Native(Name, Path, P, Height, true)) return;
        const FVector Base = SurfacePoint(P);
        AddShape(Name + TEXT("_Stem"), Cylinder, Base + FVector(0, 0, Height * .32f), FVector(Height / 1050.f, Height / 1050.f, Height * .64f / 100.f), Bark, true);
        for (int32 J = 0; J < 3; ++J)
        {
            const float A = J * 2.094f;
            const FVector Offset(FMath::Cos(A) * Height * .12f, FMath::Sin(A) * Height * .12f, Height * (.66f + .06f * J));
            AddShape(Name + TEXT("_Canopy"), Sphere, Base + Offset, FVector(Height * .005f, Height * .0045f, Height * .004f), Moss * (1.f + J * .07f));
        }
    };
    auto Rock = [&](const FString& Name, FVector P, float Height, int32 Type)
    {
        const TCHAR* Paths[] = { TEXT("/Game/Art/Nature/SM_Rock01.SM_Rock01"), TEXT("/Game/Art/Nature/SM_Rock02.SM_Rock02"), TEXT("/Game/Art/Nature/SM_Rock03.SM_Rock03") };
        if (Native(Name, Paths[Type % 3], P, Height, true)) return;
        auto* Part = AddShape(Name, Sphere, SurfacePoint(P, Height * .3f), FVector(Height * .015f, Height * .011f, Height * .009f), Sand, true);
        if (Part) Part->SetWorldRotation(FRotator(Random.FRandRange(-12, 12), Random.FRandRange(0, 360), 0));
    };
    auto ClearOfPlay = [&](FVector P)
    {
        if (RoadDistance(P.X, P.Y) < 850.f || FVector::Dist2D(P, MotherPosition) < 1700.f) return false;
        if (FVector::Dist2D(P, FVector(-1000, -5000, 0)) < 1200.f) return false;
        for (const auto& G : Groves) if (FVector::Dist2D(P, G.Position) < 1250.f) return false;
        return true;
    };
    // Sparse clusters leave long sightlines across the common basin.
    int32 Placed = 0;
    for (int32 Attempt = 0; Attempt < 800 && Placed < 95; ++Attempt)
    {
        const FVector P(Random.FRandRange(-14500, 14500), Random.FRandRange(-14500, 14500), 0);
        if (P.Size2D() > 15500.f || !ClearOfPlay(P)) continue;
        if (FMath::PerlinNoise2D(FVector2D(P.X / 3400.f + 29, P.Y / 3400.f)) < -.18f) continue;
        Tree(FString::Printf(TEXT("Woodland_%d"), Placed), P, Random.FRandRange(550, 1150), Placed % 2);
        ++Placed;
    }
    for (int32 I = 0; I < 58; ++I)
    {
        const FVector P(Random.FRandRange(-13500, 13500), Random.FRandRange(-13500, 13500), 0);
        if (ClearOfPlay(P)) Rock(FString::Printf(TEXT("Stone_%d"), I), P, Random.FRandRange(90, 310), I);
    }
    // A remembered rock pair is a stable scale reference near the starting path.
    Rock(TEXT("FirstStoneLeft"), FVector(-1750, -4300, 0), 220, 0);
    Rock(TEXT("FirstStoneRight"), FVector(-350, -4250, 0), 155, 1);
    Rock(TEXT("LookoutStone"), FVector(2100, 6600, 0), 360, 2);
    for (int32 I = 0; I < 140; ++I)
    {
        const FVector P(Random.FRandRange(-10800, 10800), Random.FRandRange(-10500, 11500), 0);
        if (RoadDistance(P.X, P.Y) < 420.f || FVector::Dist2D(P, MotherPosition) < 1100.f) continue;
        if (!Native(FString::Printf(TEXT("Grass_%d"), I), TEXT("/Game/Art/Nature/SM_Grass01.SM_Grass01"), P, Random.FRandRange(45, 100), false))
            AddShape(TEXT("MeadowSprout"), Cone, SurfacePoint(P, 22), FVector(.45f, .45f, .6f), Moss);
    }
    MotherPosition = SurfacePoint(MotherPosition);
    AddShape(TEXT("MotherTrunk"), Cylinder, MotherPosition + FVector(0, 0, 690), FVector(2.7f, 2.7f, 13.8f), Bark, true);
    for (int32 I = 0; I < 5; ++I)
    {
        const float A = I * 2.f * PI / 5.f;
        const FVector Side(FMath::Cos(A), FMath::Sin(A), 0);
        auto* Branch = AddShape(TEXT("MotherBranch"), Cylinder, MotherPosition + Side * 250.f + FVector(0, 0, 1150), FVector(.95f, .95f, 8), Bark);
        if (Branch) Branch->SetWorldRotation(FRotator(38, FMath::RadiansToDegrees(A), 0));
        AddShape(TEXT("MotherOuterCrown"), Sphere, MotherPosition + Side * 520.f + FVector(0, 0, 1600 + (I % 2) * 100), FVector(9, 8, 6), FLinearColor(.35f, .43f, .16f));
        auto* Root = AddShape(TEXT("MotherRoot"), Cone, MotherPosition + Side * 195.f + FVector(0, 0, 50), FVector(2, 2, 7), Bark);
        if (Root) Root->SetWorldRotation(FRotator(77, FMath::RadiansToDegrees(A), 0));
    }
    MotherCrown = AddShape(TEXT("MotherHeart"), Sphere, MotherPosition + FVector(0, 0, 1760), FVector(9, 9, 7), FLinearColor(.33f, .36f, .16f));
    // A raised golden heart makes the shared objective legible from the basin.
    auto* Heart = AddShape(TEXT("MotherSap"), Sphere, MotherPosition + FVector(0, -150, 420), FVector(1.05f), FLinearColor(1.f, .58f, .12f));
    if (Heart) Heart->SetMaterial(0, ColoredMaterial(FLinearColor(1.f, .58f, .12f), .4f));
    for (int32 Index = 0; Index < Groves.Num(); ++Index)
    {
        auto& G = Groves[Index];
        G.Position = SurfacePoint(G.Position);
        G.Marker = AddShape(FString::Printf(TEXT("Grove_%d_Marker"), Index), Cylinder, G.Position + FVector(0, 0, 9), FVector(7.5f, 7.5f, .12f), G.Color);
        G.FruitVisuals.Empty();
        for (int32 J = 0; J < 12; ++J)
        {
            const float A = J * 2.f * PI / 12.f;
            const float R = 640.f + (J % 3) * 90.f;
            const FVector P = SurfacePoint(G.Position + FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, 0));
            AddShape(TEXT("FruitStem"), Cylinder, P + FVector(0, 0, 48), FVector(.12f, .12f, .96f), Bark);
            AddShape(TEXT("FruitLeaves"), Sphere, P + FVector(0, 0, 62), FVector(1.45f, 1.1f, .45f), Moss);
            auto* Fruit = AddShape(TEXT("GroveFruit"), Sphere, P + FVector(0, 0, 116), FVector(.85f, .85f, .95f), G.Color);
            if (Fruit) G.FruitVisuals.Add(Fruit);
        }
        Tree(FString::Printf(TEXT("Grove_%d_Shade"), Index), G.Position + FVector(1050, 800, 0), 1050, Index % 2);
    }
    // Fallback lighting also makes an Entry-map launch usable before map creation.
    if (!TActorIterator<ASkyAtmosphere>(GetWorld()))
    {
        auto* Sky = GetWorld()->SpawnActor<ASkyAtmosphere>();
        if (Sky) Sky->GetComponent()->SetRayleighScatteringScale(.75f);
    }
    if (!TActorIterator<ADirectionalLight>(GetWorld()))
    {
        auto* Sun = GetWorld()->SpawnActor<ADirectionalLight>(FVector(0, 0, 10000), FRotator(-38, -38, 0));
        if (Sun) if (auto* Light = Sun->FindComponentByClass<UDirectionalLightComponent>())
        {
            Light->SetMobility(EComponentMobility::Movable);
            Light->SetIntensity(4.2f);
            Light->SetLightColor(FLinearColor(1.f, .88f, .67f));
            Light->SetAtmosphereSunLight(true);
        }
    }
    if (!TActorIterator<ASkyLight>(GetWorld()))
    {
        auto* Sky = GetWorld()->SpawnActor<ASkyLight>();
        if (Sky)
        {
            auto* Light = Sky->GetLightComponent();
            Light->SetMobility(EComponentMobility::Movable);
            Light->SetIntensity(1.1f);
            Light->SetRealTimeCapture(true);
        }
    }
    if (!TActorIterator<AExponentialHeightFog>(GetWorld()))
    {
        auto* Fog = GetWorld()->SpawnActor<AExponentialHeightFog>();
        if (Fog)
        {
            Fog->GetComponent()->SetFogDensity(.009f);
            Fog->GetComponent()->SetFogHeightFalloff(.15f);
            Fog->GetComponent()->SetFogInscatteringColor(FLinearColor(.57f, .68f, .72f));
        }
    }
    UpdateGroveVisuals();
}

void AVerdantWorld::UpdateGroveVisuals()
{
    for (auto& G : Groves)
    {
        const float Life = FMath::Clamp(G.Fertility, 0.f, 1.f);
        const FLinearColor Soil = FMath::Lerp(FLinearColor(.31f, .23f, .14f), G.Color * .6f, Life);
        if (G.Marker)
        {
            G.Marker->SetMaterial(0, ColoredMaterial(Soil, G.bOpen ? .09f : 0.f));
            G.Marker->SetWorldScale3D(FVector(7.5f, 7.5f, G.bOpen ? .12f : .32f));
        }
        const int32 VisibleFruit = FMath::Clamp(FMath::CeilToInt(G.Food / 4.f), 0, G.FruitVisuals.Num());
        for (int32 I = 0; I < G.FruitVisuals.Num(); ++I)
            if (G.FruitVisuals[I])
            {
                G.FruitVisuals[I]->SetVisibility(I < VisibleFruit);
                G.FruitVisuals[I]->SetMaterial(0, ColoredMaterial(FMath::Lerp(FLinearColor(.36f, .23f, .09f), G.Color, Life), .22f));
            }
    }
    if (MotherCrown)
    {
        const float Progress = FMath::Clamp(TreeNutrition / FMath::Max(1.f, TreeGoal), 0.f, 1.f);
        const FLinearColor Crown = FMath::Lerp(FLinearColor(.33f, .36f, .16f), FLinearColor(.75f, .95f, .38f), Progress);
        MotherCrown->SetMaterial(0, ColoredMaterial(Crown, .05f + Progress * .7f));
        MotherCrown->SetWorldScale3D(FVector(9, 9, 7) * (1.f + Progress * .22f));
    }
}

