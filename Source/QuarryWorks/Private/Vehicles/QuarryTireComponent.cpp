#include "Vehicles/QuarryTireComponent.h"

#include "Materials/MaterialInstanceDynamic.h"

void UQuarryTireComponent::BeginPlay()
{
	Super::BeginPlay();
	BuildMesh();
}

void UQuarryTireComponent::BuildMesh()
{
	constexpr int32 Segments = 48;
	constexpr int32 RingCount = 4;
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UV0;
	TArray<FLinearColor> Colors;

	for (int32 Ring = 0; Ring < RingCount; ++Ring)
	{
		const float Z = Ring == 0 ? -Width * 0.5f : Ring == 1 ? -Width * 0.36f : Ring == 2 ? Width * 0.36f : Width * 0.5f;
		for (int32 Segment = 0; Segment <= Segments; ++Segment)
		{
			const float Angle = 2.0f * PI * Segment / Segments;
			const float EdgeScale = Ring == 0 || Ring == RingCount - 1 ? 0.74f : 1.0f;
			const float TreadScale = (Ring == 1 || Ring == 2) && Segment % 2 ? 0.965f : 1.0f;
			const FVector Radial(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f);
			Vertices.Add(Radial * (Radius * EdgeScale * TreadScale) + FVector(0.0, 0.0, Z));
			Normals.Add(Radial);
			UV0.Add(FVector2D(static_cast<float>(Segment) / Segments, static_cast<float>(Ring) / (RingCount - 1)));
			Colors.Add(Color);
		}
	}

	for (int32 Ring = 0; Ring < RingCount - 1; ++Ring)
	{
		for (int32 Segment = 0; Segment < Segments; ++Segment)
		{
			const int32 A = Ring * (Segments + 1) + Segment;
			const int32 B = A + Segments + 1;
			Triangles.Append({A, A + 1, B, A + 1, B + 1, B});
		}
	}

	for (int32 Side = 0; Side < 2; ++Side)
	{
		const int32 Center = Vertices.Add(FVector(0.0, 0.0, Side == 0 ? -Width * 0.5f : Width * 0.5f));
		Normals.Add(FVector(0.0, 0.0, Side == 0 ? -1.0f : 1.0f));
		UV0.Add(FVector2D(0.5f, 0.5f));
		Colors.Add(Color * 0.8f);
		const int32 Ring = Side == 0 ? 0 : RingCount - 1;
		for (int32 Segment = 0; Segment < Segments; ++Segment)
		{
			const int32 A = Ring * (Segments + 1) + Segment;
			if (Side == 0) Triangles.Append({Center, A + 1, A});
			else Triangles.Append({Center, A, A + 1});
		}
	}

	CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UV0, Colors, TArray<FProcMeshTangent>(), false);
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (UMaterialInstanceDynamic* Material = CreateAndSetMaterialInstanceDynamic(0))
	{
		Material->SetVectorParameterValue(TEXT("Color"), FLinearColor::White);
	}
}
