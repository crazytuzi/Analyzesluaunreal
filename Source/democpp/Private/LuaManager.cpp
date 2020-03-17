// Fill out your copyright notice in the Description page of Project Settings.


#include "Public/LuaManager.h"

// Sets default values
ALuaManager::ALuaManager()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
}

static uint8* ReadFile(IPlatformFile& PlatformFile, FString path, uint32& len)
{
	IFileHandle* FileHandle = PlatformFile.OpenRead(*path);

	if (FileHandle)
	{
		len = static_cast<uint32>(FileHandle->Size());
		uint8* buf = new uint8[len];

		FileHandle->Read(buf, len);
		delete FileHandle;

		return buf;
	}
	return nullptr;
}

uint8* LoadFile(const char* fn, uint32& len, FString& filepath)
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	static TArray<FString> luaFilePaths = {TEXT("/Lua/")};
	// ͬʱ����֧��*.lua�� *.luac����lua�ű���ʽ
	static TArray<FString> luaExts = {UTF8_TO_TCHAR(".lua"), UTF8_TO_TCHAR(".luac")};

	for (const auto& filePath : luaFilePaths)
	{
		FString path = FPaths::ProjectContentDir();

		path += filePath;

		path += UTF8_TO_TCHAR(fn);

		for (auto ptr = luaExts.CreateConstIterator(); ptr; ++ptr)
		{
			auto fullPath = path + *ptr;

			// ͨ��ReadFile��������lua�ű�������
			// ���ص�len��������ȷ����Դ�ĳ���
			// ���ص�filepath�����������ⲿ�����������Դ�ľ��Ե�ַ���������ⲿ����������ٲ鿴��ȷ�ĵ���ջ��Ϣ
			auto buf = ReadFile(PlatformFile, fullPath, len);

			if (buf)
			{
				filepath = fullPath;
				return buf;
			}
		}
	}
	return nullptr;
}

// Called when the game starts or when spawned
void ALuaManager::BeginPlay()
{
	Super::BeginPlay();

	state.init();

	/*
	 * setLoadFileDelegate ��������һ��������
	 * �ô�������lua doFile��require�Ȳ�����ʱ����ҽű���·����������
	 * ����ԭ��typedef uint8* (*LoadFileDelegate) (const char* fn, uint32& len, FString& filepath)
	 */
	state.setLoadFileDelegate(&LoadFile);

	// ���ز�ִ��main.lua�ļ���ע��������������д����Դload��������Զ������չ��.lua
	/*
	print("hello world")
	return 1024

	xx={}
	function xx.text(v)
	    print("xx.text",v)
	end
	 */
	// ͬʱ1024�ķ���ֵ������ v �����һ��LuaVar���󣬿��Ա�ʾ�κ�lua���ֵ
	slua::LuaVar v = state.doFile("main");

	// ��������ĺ����ǵ���xx.text������ͬʱ����v��Ϊ����
	state.call("xx.text", v);
	int64_t start = slua::getTime();

	for (size_t i=0;i< 1000000;++i)
	{
		state.call("xx.dotest");
	}

	int64_t end = slua::getTime();

	int64_t diff = end - start;

	UE_LOG(LogTemp,Log, L"used %lld", diff);
	
}

// Called every frame
void ALuaManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}
