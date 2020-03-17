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
	// 同时考虑支持*.lua和 *.luac两个lua脚本格式
	static TArray<FString> luaExts = {UTF8_TO_TCHAR(".lua"), UTF8_TO_TCHAR(".luac")};

	for (const auto& filePath : luaFilePaths)
	{
		FString path = FPaths::ProjectContentDir();

		path += filePath;

		path += UTF8_TO_TCHAR(fn);

		for (auto ptr = luaExts.CreateConstIterator(); ptr; ++ptr)
		{
			auto fullPath = path + *ptr;

			// 通过ReadFile方法返回lua脚本的数据
			// 返回的len参数用于确定资源的长度
			// 返回的filepath参数，帮助外部调试器清楚资源的绝对地址，可以在外部调试器里快速查看正确的调用栈信息
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
	 * setLoadFileDelegate 用于设置一个代理函数
	 * 该代理函数在lua doFile，require等操作的时候查找脚本的路劲，并加载
	 * 函数原型typedef uint8* (*LoadFileDelegate) (const char* fn, uint32& len, FString& filepath)
	 */
	state.setLoadFileDelegate(&LoadFile);

	// 加载并执行main.lua文件，注意由于我们上面写的资源load函数里会自动添加扩展名.lua
	/*
	print("hello world")
	return 1024

	xx={}
	function xx.text(v)
	    print("xx.text",v)
	end
	 */
	// 同时1024的返回值保存在 v 里，他是一个LuaVar对象，可以表示任何lua里的值
	slua::LuaVar v = state.doFile("main");

	// 上面的语句的含义是调用xx.text方法，同时传入v作为参数
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
