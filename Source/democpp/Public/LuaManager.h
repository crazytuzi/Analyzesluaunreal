// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LuaState.h"
#include "LuaManager.generated.h"

UCLASS()
class DEMOCPP_API ALuaManager : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	ALuaManager();

protected:
	// Called when the game starts or when spawned
	void BeginPlay() override;

public:
	// Called every frame
	void Tick(float DeltaTime) override;

private:

	NS_SLUA::LuaState state;
};
