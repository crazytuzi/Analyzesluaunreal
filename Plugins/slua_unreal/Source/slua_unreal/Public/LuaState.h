// Tencent is pleased to support the open source community by making sluaunreal available.

// Copyright (C) 2018 THL A29 Limited, a Tencent company. All rights reserved.
// Licensed under the BSD 3-Clause License (the "License"); 
// you may not use this file except in compliance with the License. You may obtain a copy of the License at

// https://opensource.org/licenses/BSD-3-Clause

// Unless required by applicable law or agreed to in writing, 
// software distributed under the License is distributed on an "AS IS" BASIS, 
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. 
// See the License for the specific language governing permissions and limitations under the License.

#pragma once
#define LUA_LIB
#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "LuaVar.h"
#include <string>
#include <memory>
#include <atomic>
#include "HAL/Runnable.h"
#include "Tickable.h"

#define SLUA_LUACODE "[sluacode]"
#define SLUA_CPPINST "__cppinst"

DECLARE_MULTICAST_DELEGATE(FLuaStateInitEvent);

class ULatentDelegate;

namespace NS_SLUA {

	struct ScriptTimeoutEvent {
		virtual void onTimeout() = 0;
	};

	// 死循环检测
	class FDeadLoopCheck : public FRunnable
	{
	public:
		FDeadLoopCheck();
		~FDeadLoopCheck();

		int scriptEnter(ScriptTimeoutEvent* pEvent);
		int scriptLeave();

	protected:
		uint32 Run() override;
		void Stop() override;
		void onScriptTimeout();
	private:
		std::atomic<ScriptTimeoutEvent*> timeoutEvent;
		FThreadSafeCounter timeoutCounter;
		FThreadSafeCounter stopCounter;
		FThreadSafeCounter frameCounter;
		FRunnableThread* thread;
	};

	// check lua script dead loop
	class LuaScriptCallGuard : public ScriptTimeoutEvent {
	public:
		LuaScriptCallGuard(lua_State* L);
		virtual ~LuaScriptCallGuard();
		void onTimeout() override;
	private:
		lua_State* L;
		static void scriptTimeout(lua_State *L, lua_Debug *ar);
	};

	typedef TMap<UObject*, GenericUserData*> UObjectRefMap;

    class SLUA_UNREAL_API LuaState 
		: public FUObjectArray::FUObjectDeleteListener
		, public FGCObject
		, public FTickableGameObject
    {
    public:
        LuaState(const char* name=nullptr,UGameInstance* pGI=nullptr);
        virtual ~LuaState();

        /*
         * fn, lua file to load, fn may be a short filename
         * if find fn to load, return file size to len and file full path fo filepath arguments
         * if find fn and load successful, return buf of file content, otherwise return nullptr
         * you must delete[] buf returned by function for free memory.
         */
    	// 加载文件委托原型
		typedef uint8* (*LoadFileDelegate) (const char* fn, uint32& len, FString& filepath);
    	// 错误委托原型
		typedef void (*ErrorDelegate) (const char* err);

    	// 获取State,空参返回mainState
        inline static LuaState* get(lua_State* l=nullptr) {
            // if L is nullptr, return main state
            if(!l) return mainState;
        	// sizeof(void*) 获取一个指针的大小
        	// void** 指向指针的指针
        	// (char *)(l)-(sizeof(void *) 强转到char*,取前8个字节的偏移
			// (LuaState*)*((void**)((void *)((char *)(l)-(sizeof(void *)))));
            // static_cast<LuaState*>(*static_cast<void**>(static_cast<void *>((char *)(l) - (sizeof(void *)))));
            return (LuaState*)*((void**)lua_getextraspace(l));
        }
        // get LuaState from state index
    	// 从stateMapFromIndex取index对应的LuaState
        static LuaState* get(int index);
		// get LuaState from UGameInstance, you should create LuaState with an UGameInstance pointer at first
		// if multi LuaState have same UGameInstance, we will return first one
		// 取UGameInstance对应的LuaState,需要在调用构造函数的时候将UGameInstance* pGI赋值
		static LuaState* get(UGameInstance* pGI);

        // get LuaState from name
		// 取name对应的LuaState,遍历stateMapFromIndex
    	static LuaState* get(const FString& name);

        // return specified index is valid state index
    	// 查看index对应的LuaState是否有效
        inline static bool isValid(int index)  {
            return get(index)!=nullptr;
        }
        
        // return state index
    	// 获取state对应的Index
        int stateIndex() const { return si; }
        
        // init lua state
    	// 初始化
    	// 是否开启多线程gc 默认关闭
        virtual bool init(bool enableMultiThreadGC=false);
		// attach this luaState to UGameInstance
		// this function just store UGameInstance pointer for search future
		// 附加到UGameInstance,用于初始化的时候填入空UGameInstance
		// 未做pGI是否已经赋值的检查
		void attach(UGameInstance* pGI);
        
        // close lua state
    	// 关闭
        virtual void close();

        // execute lua string
    	// 加载并运行指定的字符串,此处的pEnv没有实际用处,工程中已经实现了
        LuaVar doString(const char* str, LuaVar* pEnv = nullptr);
        // execute bytes buffer and named buffer to chunk
        LuaVar doBuffer(const uint8* buf,uint32 len, const char* chunk, LuaVar* pEnv = nullptr);
        // load file and execute it
        // file how to loading depend on load delegation
        // see setLoadFileDelegate function
        // 加载并运行指定的文件,同样pEnv没有实际用处,工程中已经实现了
        LuaVar doFile(const char* fn, LuaVar* pEnv = nullptr);

       
        // call function that specified by key
        // any supported c++ value can be passed as argument to lua
        // 用于C++通过key调用Lua方法,支持任意常数
        template<typename ...ARGS>
        LuaVar call(const char* key,ARGS&& ...args) {
            LuaVar f = get(key);
            return f.call(std::forward<ARGS>(args)...);
        }

        // get field from _G, support "x.x.x.x" to search children field
    	// 从_G查key,支持a.b.c.d
        LuaVar get(const char* key);
		// set field to _G, support "x.x.x.x" to create sub table recursive
    	// 设置_G
		bool set(const char* key, LuaVar v);

        // set load delegation function to load lua code
    	// 设置加载文件的委托
		void setLoadFileDelegate(LoadFileDelegate func);
		// set error delegation function to handle error
    	// 设置错误的委托
		void setErrorDelegate(ErrorDelegate func);

		lua_State* getLuaState() const
		{
			return L;
		}

    	// 隐式转换
		operator lua_State*() const
		{
			return L;
		}

        // create a empty table
    	// 创建一个表
		LuaVar createTable();
		// create named table, support "x.x.x.x", put table to _G
    	// 创建一个全局表
		LuaVar createTable(const char* key);

		const UObjectRefMap& cacheSet() const {
			return objRefs;
		}

    	// 设置Tick函数
		void setTickFunction(LuaVar func);

		// add obj to ref, tell Engine don't collect this obj
    	// 添加到引用,防止被GC
		void addRef(UObject* obj,void* ud,bool ref);
		// unlink UObject, flag Object had been free, and remove from cache and objRefs
    	// 取消引用
		void unlinkUObject(const UObject * Object);

		// if obj be deleted, call this function
    	// 当对象被删除的时候,调用函数 => 继承于FUObjectDeleteListener
		virtual void NotifyUObjectDeleted(const class UObjectBase *Object, int32 Index) override;

		// tell Engine which objs should be referenced
		virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
    	// push失败函数
        static int pushErrorHandler(lua_State* L);
#if (ENGINE_MINOR_VERSION>=23) && (ENGINE_MAJOR_VERSION>=4)
		virtual void OnUObjectArrayShutdown() override;
#endif

		// tickable object methods
		virtual void Tick(float DeltaTime) override;
		virtual TStatId GetStatId() const override;
#if !((ENGINE_MINOR_VERSION>18) && (ENGINE_MAJOR_VERSION>=4))
		virtual bool IsTickable() const override { return true; }
#endif

    	// 开线程
		int addThread(lua_State *thread);
    	// 恢复线程
		void resumeThread(int threadRef);
    	// 查询线程
		int findThread(lua_State *thread);
    	// 清理所有线程
		void cleanupThreads();
		ULatentDelegate* getLatentDelegate() const;

		// call this function on script error
    	// 错误提示函数
		void onError(const char* err);
    protected:
		LoadFileDelegate loadFileDelegate;
		ErrorDelegate errorDelegate;
    	// 加载文件函数,需要先通过 setLoadFileDelegate 设置上
        uint8* loadFile(const char* fn,uint32& len,FString& filepath);
    	// 加载文件函数
		static int loader(lua_State* L);
		static int getStringFromMD5(lua_State* L);

	public:
		FLuaStateInitEvent onInitEvent;

    private:
        friend class LuaObject;
        friend class SluaUtil;
		friend struct LuaEnums;
		friend class LuaScriptCallGuard;
        lua_State* L;
        int cacheObjRef;
		// init enums lua code
        int _pushErrorHandler(lua_State* L);
        static int _atPanic(lua_State* L);
    	// 把prop添加到parent
		void linkProp(void* parent, void* prop);
    	// 将prop从prop的parent移除
		void releaseLink(void* prop);
    	// 释放所有的propLink
		void releaseAllLink();
		// unreal gc will call this funciton
    	// 当unreal的gc发生的时候调用
		void onEngineGC();
		// on world cleanup
    	// 在世界被清理的时候调用
		void onWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);
    	// 释放所有的延迟释放对象
		void freeDeferObject();


		TMap<void*, TArray<void*>> propLinks;
        int stackCount;
        int si;
        FString stateName;

		// cache ufunction/uproperty ptr if index by lua
    	// 缓存ufunction/uproperty ptr
		struct ClassCache {
			typedef TMap<FString, TWeakObjectPtr<UFunction>> CacheFuncItem;
			typedef TMap<TWeakObjectPtr<UClass>, CacheFuncItem> CacheFuncMap;

			typedef TMap<FString, TWeakObjectPtr<UProperty>> CachePropItem;
			typedef TMap<TWeakObjectPtr<UClass>, CachePropItem> CachePropMap;
			
			UFunction* findFunc(UClass* uclass, const char* fname);
			UProperty* findProp(UClass* uclass, const char* pname);
			void cacheFunc(UClass* uclass, const char* fname, UFunction* func);
			void cacheProp(UClass* uclass, const char* pname, UProperty* prop);
			void clear() {
				cacheFuncMap.Empty();
				cachePropMap.Empty();
			}

			CacheFuncMap cacheFuncMap;
			CachePropMap cachePropMap;
		} classMap;

		FDeadLoopCheck* deadLoopCheck;

		// hold UObjects pushed to lua
		UObjectRefMap objRefs;
		// hold FGcObject to defer delete
		TArray<FGCObject*> deferDelete;
		// store UGameInstance ptr to search LuaState
		// we don't hold referrence
		UGameInstance* pGI;


		FDelegateHandle pgcHandler;
		FDelegateHandle wcHandler;

		bool enableMultiThreadGC;
		LuaVar stateTickFunc;

        static LuaState* mainState;

        #if WITH_EDITOR
        // used for debug
		TMap<FString, FString> debugStringMap;
        #endif

    	// 双向Map
		TMap<lua_State*, int> threadToRef;                                // coroutine -> ref
		TMap<int, lua_State*> refToThread;                                // coroutine -> ref
		ULatentDelegate* latentDelegate;

    };
}
