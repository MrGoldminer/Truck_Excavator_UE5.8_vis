#include "Vehicles/QuarryBlockoutVisuals.h"

#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "Vehicles/QuarryBlockoutComponent.h"

namespace
{
	template<typename ComponentType>
	ComponentType* CreateVisual(AActor* Owner, FName Name, USceneComponent* Parent, const FLinearColor& Color, bool bRuntime)
	{
		ComponentType* Component = bRuntime
			? NewObject<ComponentType>(Owner, Name)
			: Owner->CreateDefaultSubobject<ComponentType>(Name);
		if (bRuntime)
		{
			Owner->AddInstanceComponent(Component);
		}
		Component->SetupAttachment(Parent);
		if (bRuntime)
		{
			Component->RegisterComponent();
		}
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->Color = Color;
		Component->SetMaterial(0, LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_QuarryBlockout.M_QuarryBlockout")));
		return Component;
	}
}

UQuarryBlockoutComponent* FQuarryBlockoutVisuals::AddCube(
	AActor* Owner, FName Name, USceneComponent* Parent, const FVector& Size,
	const FVector& Location, const FLinearColor& Color, bool bRuntime, const FRotator& Rotation)
{
	UQuarryBlockoutComponent* Component = CreateVisual<UQuarryBlockoutComponent>(Owner, Name, Parent, Color, bRuntime);
	Component->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	Component->SetRelativeLocation(Location);
	Component->SetRelativeRotation(Rotation);
	Component->SetRelativeScale3D(Size / 100.0f);
	return Component;
}

UQuarryBlockoutComponent* FQuarryBlockoutVisuals::AddCylinder(
	AActor* Owner, FName Name, USceneComponent* Parent, float Radius, float Length,
	const FVector& Location, const FRotator& Rotation, const FLinearColor& Color, bool bRuntime)
{
	UQuarryBlockoutComponent* Component = CreateVisual<UQuarryBlockoutComponent>(Owner, Name, Parent, Color, bRuntime);
	Component->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")));
	Component->SetRelativeLocation(Location);
	Component->SetRelativeRotation(Rotation);
	Component->SetRelativeScale3D(FVector(Radius / 50.0f, Radius / 50.0f, Length / 100.0f));
	return Component;
}

UQuarryBlockoutComponent* FQuarryBlockoutVisuals::AddSphere(
	AActor* Owner, FName Name, USceneComponent* Parent, const FVector& Scale,
	const FVector& Location, const FLinearColor& Color, bool bRuntime)
{
	UQuarryBlockoutComponent* Component = CreateVisual<UQuarryBlockoutComponent>(Owner, Name, Parent, Color, bRuntime);
	Component->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")));
	Component->SetRelativeLocation(Location);
	Component->SetRelativeScale3D(Scale);
	return Component;
}
