// Tencent is pleased to support the open source community by making sluaunreal available.

// Copyright (C) 2018 THL A29 Limited, a Tencent company. All rights reserved.
// Licensed under the BSD 3-Clause License (the "License"); 
// you may not use this file except in compliance with the License. You may obtain a copy of the License at

// https://opensource.org/licenses/BSD-3-Clause

// Unless required by applicable law or agreed to in writing, 
// software distributed under the License is distributed on an "AS IS" BASIS, 
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. 
// See the License for the specific language governing permissions and limitations under the License.


#include "LuaState.h"
#include "LuaObject.h"
#include "SluaLib.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"
#include "Blueprint/UserWidget.h"
#include "Misc/AssertionMacros.h"
#include "Misc/SecureHash.h"
#include "Log.h"
#include "lua/lua.hpp"
#include <map>
#include "LuaWrapper.h"
#include "LuaArray.h"
#include "LuaMap.h"
#include "LuaSocketWrap.h"
#include "LuaMemoryProfile.h"
#include "HAL/RunnableThread.h"
#include "GameDelegates.h"
#include "LatentDelegate.h"
#include "LuaActor.h"
#include "LuaProfiler.h"
#include "Stats.h"

namespace NS_SLUA {

	// ���ִ��ʱ��
	const int MaxLuaExecTime = 5; // in second

    int import(lua_State *L) {
        const char* name = LuaObject::checkValue<const char*>(L,1);
        if(name) {
        	// ���β�ѯUClass,UScriptStruct��UEnum
            UClass* uclass = FindObject<UClass>(ANY_PACKAGE, UTF8_TO_TCHAR(name));
            if(uclass) return LuaObject::pushClass(L,uclass);
            
			UScriptStruct* ustruct = FindObject<UScriptStruct>(ANY_PACKAGE, UTF8_TO_TCHAR(name));
            if(ustruct) return LuaObject::pushStruct(L,ustruct);

			UEnum* uenum = FindObject<UEnum>(ANY_PACKAGE, UTF8_TO_TCHAR(name));
			if (uenum) return LuaObject::pushEnum(L, uenum);
            
            luaL_error(L,"Can't find class named %s",name);
        }
        return 0;
    }
    
    int print(lua_State *L) {
        FString str;
    	// �����ĸ���
        int top = lua_gettop(L);
        for(int n=1;n<=top;n++) {
            size_t len;
            const char* s = luaL_tolstring(L, n, &len);
            str+="\t";
            if(s) str+=UTF8_TO_TCHAR(s);
        }
        Log::Log("%s",TCHAR_TO_UTF8(*str));
        return 0;
    }

	// Lua����
	int dofile(lua_State *L) {

    	/*
    	 * const char *luaL_checkstring (lua_State *L, int arg)
    	 * ��麯���ĵ� arg �������Ƿ���һ�� �ַ�������������ַ���
    	 * �������ʹ�� lua_tolstring ����ȡ���
    	 * ���Ըú����п���������ת����ͬ����Ч
    	 */
    	
		auto fn = luaL_checkstring(L, 1);
		auto ls = LuaState::get(L);
		ensure(ls);
		auto var = ls->doFile(fn);
		if (var.isValid()) {
			return var.push(L);
		}
		return 0;
	}

    int error(lua_State* L) {
    	
    	/*
    	 * const char *lua_tostring (lua_State *L, int index)
    	 * �ȼ��ڵ��� lua_tolstring,����� len Ϊ NULL
    	 */

    	/*
    	 * const char *lua_tolstring (lua_State *L, int index, size_t *len)
    	 * �Ѹ����������� Lua ֵת��Ϊһ�� C �ַ���
    	 * ��� len ��Ϊ NULL,�������ַ��������赽 *len ��
    	 * ��� Lua ֵ������һ���ַ�������һ������,���򷵻ط��� NULL
    	 * ���ֵ��һ������,lua_tolstring ����Ѷ�ջ�е��Ǹ�ֵ��ʵ������ת��Ϊһ���ַ���
    	 * ������һ�ű��ʱ��,���� lua_tolstring �����ڼ��ϣ� ���ת���п��ܵ��� lua_next Ū��
    	 * lua_tolstring ����һ���Ѷ���ָ��ָ�� Lua ״̬���е��ַ���
    	 * ����ַ������ܱ�֤�� C Ҫ��ģ����һ���ַ�Ϊ�� ('\0'),�������������ַ����ڰ��������������
    	 * ��Ϊ Lua �п��ܷ��������ռ�,���Բ���֤ lua_tolstring ���ص�ָ��,�ڶ�Ӧ��ֵ�Ӷ�ջ���Ƴ�����Ȼ��Ч
    	 */
    	
        const char* err = lua_tostring(L,1);

    	/*
    	 * void luaL_traceback (lua_State *L, lua_State *L1, const char *msg, int level)
    	 * ��ջ L1 ��ջ������Ϣѹջ
    	 * ��� msg ��Ϊ NULL �����ḽ�ӵ�ջ������Ϣ֮ǰ
    	 * level ����ָ���ӵڼ��㿪ʼ��ջ����
    	 */
    	
        luaL_traceback(L,L,err,1);
        err = lua_tostring(L,2);
        lua_pop(L,1);
		auto ls = LuaState::get(L);
		ls->onError(err);
        return 0;
    }

	void LuaState::onError(const char* err) {
    	// ������Զ������
		if (errorDelegate) errorDelegate(err);
    	// û�о����Log
		else Log::Error("%s", err);
	}

     #if WITH_EDITOR
     // used for debug
	int LuaState::getStringFromMD5(lua_State* L) {
		const char* md5String = lua_tostring(L, 1);
		LuaState* state = LuaState::get(L);
		FString md5FString = UTF8_TO_TCHAR(md5String);
		bool hasValue = state->debugStringMap.Contains(md5FString);
		if (hasValue) {
			auto value = state->debugStringMap[md5FString];
			lua_pushstring(L, TCHAR_TO_UTF8(*value));
		}
		else {
			lua_pushstring(L, "");
		}
		return 1;
	}
    #endif

    int LuaState::loader(lua_State* L) {
        LuaState* state = LuaState::get(L);
        const char* fn = lua_tostring(L,1);
        uint32 len;
        FString filepath;
    	// ��������
        if(uint8* buf = state->loadFile(fn,len,filepath)) {
            AutoDeleteArray<uint8> defer(buf);

            char chunk[256];
            snprintf(chunk,256,"@%s",TCHAR_TO_UTF8(*filepath));

			/*
			 * int luaL_loadbuffer (lua_State *L,const char *buff,size_t sz,const char *name)
			 * �ȼ��� luaL_loadbufferx,�� mode �������� NULL
			 */

        	/*
        	 * int luaL_loadbufferx (lua_State *L,const char *buff,size_t sz,const char *name,const char *mode)
        	 * ��һ�λ������Ϊһ�� Lua �����
        	 * �������ʹ�� lua_load ������ buff ָ��ĳ���Ϊ sz ���ڴ���
        	 * ��������� lua_load ����ֵ��ͬ��
        	 * name ��Ϊ���������֣����ڵ�����Ϣ�ʹ�����Ϣ
        	 * mode �ַ���������ͬ���� lua_load
        	 */

        	/*
        	 * int lua_load (lua_State *L,lua_Reader reader,void *data,const char *chunkname,const char *mode)
        	 * ����һ�� Lua ����飬����������
        	 * ���û�д���,lua_load ��һ������õĴ������Ϊһ�� Lua ����ѹ��ջ��
        	 * ����ѹ�������Ϣ
        	 * lua_load �ķ���ֵ�����ǣ�
        	 * LUA_OK: û�д���
        	 * LUA_ERRSYNTAX: ��Ԥ����ʱ�����﷨����
        	 * LUA_ERRMEM: �ڴ�������
        	 * LUA_ERRGCMM: ������ __gc Ԫ����ʱ�����ˡ����������ʹ������ع����޹أ������������ռ��������ġ���
        	 * lua_load ����ʹ��һ���û��ṩ�� reader ��������ȡ�����
        	 * data �����ᱻ���� reader ����
        	 * chunkname ����������Ը�������һ������,������ֱ����ڳ�����Ϣ�͵�����Ϣ��
        	 * lua_load ���Զ�����������ı��Ļ��Ƕ����Ƶģ�Ȼ������Ӧ�ļ��ز���
        	 * �ַ��� mode �����úͺ��� load һ�¡� ���������� NULL �ȼ����ַ��� "bt"��
        	 * lua_load ���ڲ���ʹ��ջ�� ��� reader ����������Զ��ÿ�η���ʱ����ջ��ԭ����
        	 * ������صĺ�������ֵ�� ��һ����ֵ�ᱻ����Ϊ ������ע��� LUA_RIDX_GLOBALS ��������ȫ�ֻ���
        	 * �ڼ����������ʱ�������ֵ�� _ENV ����
        	 * ������ֵ������ʼ��Ϊ nil��
        	 */
        	
            if(luaL_loadbuffer(L,(const char*)buf,len,chunk)==0) {
                return 1;
            }
            else {
                const char* err = lua_tostring(L,-1);
                Log::Error("%s",err);
                lua_pop(L,1);
            }
        }
        else
            Log::Error("Can't load file %s",fn);
        return 0;
    }

	// �����ļ�,�����Զ���ί��
    uint8* LuaState::loadFile(const char* fn,uint32& len,FString& filepath) {
        if(loadFileDelegate) return loadFileDelegate(fn,len,filepath);
        return nullptr;
    }

    LuaState* LuaState::mainState = nullptr;
    TMap<int,LuaState*> stateMapFromIndex;
    static int StateIndex = 0;

	LuaState::LuaState(const char* name, UGameInstance* gameInstance)
		: loadFileDelegate(nullptr)
		, errorDelegate(nullptr)
		, L(nullptr)
		, cacheObjRef(LUA_NOREF)
		, stackCount(0)
		, si(0)
		, deadLoopCheck(nullptr)
    {
        if(name) stateName=UTF8_TO_TCHAR(name);
		this->pGI = gameInstance;
    }

    LuaState::~LuaState()
    {
        close();
    }

    LuaState* LuaState::get(int index) {
        auto it = stateMapFromIndex.Find(index);
        if(it) return *it;
        return nullptr;
    }

    LuaState* LuaState::get(const FString& name) {
        for(auto& pair:stateMapFromIndex) {
            auto state = pair.Value;
            if(state->stateName==name)
                return state;
        }
        return nullptr;
    }

	LuaState* LuaState::get(UGameInstance* pGI) {
		for (auto& pair : stateMapFromIndex) {
			auto state = pair.Value;
			if (state->pGI && state->pGI == pGI)
				return state;
		}
		return nullptr;
	}

    // check lua top , this function can omit
	// һֱȥ���ջ��
	void LuaState::Tick(float dtime) {
		ensure(IsInGameThread());
		if (!L) return;

		int top = lua_gettop(L);
		if (top != stackCount) {
			stackCount = top;
			Log::Error("Error: lua stack count should be zero , now is %d", top);
		}

#ifdef ENABLE_PROFILER
		LuaProfiler::tick(L);
#endif

		// NS_SLUA::LuaProfiler w1(__FUNCTION__)
		PROFILER_WATCHER(w1);
		if (stateTickFunc.isFunction())
		{
			// NS_SLUA::LuaProfiler w2("TickFunc")
			PROFILER_WATCHER_X(w2,"TickFunc");
			stateTickFunc.call(dtime);
		}

		// try lua gc
		// ����һ��GC
		PROFILER_WATCHER_X(w3, "LuaGC");
		if (!enableMultiThreadGC) lua_gc(L, LUA_GCSTEP, 128);
    }

    void LuaState::close() {
        if(mainState==this) mainState = nullptr;

		latentDelegate = nullptr;

		freeDeferObject();

		releaseAllLink();

		cleanupThreads();
        
        if(L) {

        	/*
        	 * void lua_close (lua_State *L)
        	 * ����ָ�� Lua ״̬���е����ж���
        	 * ����������ռ���ص�Ԫ�����Ļ������������
        	 * �����ͷ�״̬����ʹ�õ����ж�̬�ڴ�
        	 * ��һЩƽ̨�ϣ�����Բ��ص������������ ��Ϊ���������������ʱ�����е���Դ����Ȼ���ͷŵ���
        	 * ��һ���棬�������еĳ��򣬱���һ����̨�������һ����վ������
        	 * �ᴴ������� Lua ״̬��,��ô��Ӧ���ڲ���Ҫʱ�Ͻ��ر����� 
        	 */
        	
            lua_close(L);
			GUObjectArray.RemoveUObjectDeleteListener(this);
			FCoreUObjectDelegates::GetPostGarbageCollect().Remove(pgcHandler);
			FWorldDelegates::OnWorldCleanup.Remove(wcHandler);
            stateMapFromIndex.Remove(si);
            L=nullptr;
        }

		freeDeferObject();
		objRefs.Empty();
		SafeDelete(deadLoopCheck);
    }


    bool LuaState::init(bool gcFlag) {

		// ��ѭ�����,��һ�ε��ûᱻ����
        if(deadLoopCheck)
            return false;

		// ����init�ļ�Ϊ��State
        if(!mainState) 
            mainState = this;

		enableMultiThreadGC = gcFlag;
		// ��GC�ص�
		pgcHandler = FCoreUObjectDelegates::GetPostGarbageCollect().AddRaw(this, &LuaState::onEngineGC);
		wcHandler = FWorldDelegates::OnWorldCleanup.AddRaw(this, &LuaState::onWorldCleanup);
		// �̳���FUObjectDeleteListener,��ɾ����ʱ������NotifyUObjectDeleted
		GUObjectArray.AddUObjectDeleteListener(this);

		latentDelegate = NewObject<ULatentDelegate>((UObject*)GetTransientPackage(), ULatentDelegate::StaticClass());
		latentDelegate->bindLuaState(this);

        stackCount = 0;
		// mainState λ��ջ�ĵ�һ��
        si = ++StateIndex;

		// ���
		propLinks.Empty();
		classMap.clear();
		objRefs.Empty();

#if WITH_EDITOR
		// used for debug
		debugStringMap.Empty();
#endif

		deadLoopCheck = new FDeadLoopCheck();

        // use custom memory alloc func to profile memory footprint
		// ����һ���µ�lua_State
		// ����һ���������µĶ�����״̬���е��̡߳�
		// ����޷������̻߳�״̬���������ڴ����ޣ��򷵻� NULL��
		// ���� f ��һ�������������� Lua ��ͨ�����������״̬�������е��ڴ���������
		// �ڶ������� ud �����ָ�뽫��ÿ�ε��÷�����ʱ��ת�롣
        L = lua_newstate(LuaMemoryProfile::alloc,this);
		// ����һ���µ� panic ������������֮ǰ���õ��Ǹ���
		// panic �����Դ�����Ϣ�������ķ�ʽ����
        lua_atpanic(L,_atPanic);
        // bind this to L
        *((void**)lua_getextraspace(L)) = this;
        stateMapFromIndex.Add(si,this);

        // init obj cache table
		
        /*
         * void lua_newtable (lua_State *L)
         * ����һ�ſձ�������ѹջ
         * �ȼ��� lua_createtable(L, 0, 0) 
         */
		
        lua_newtable(L);
        lua_newtable(L);
		
		/*
		 * const char *lua_pushstring (lua_State *L, const char *s)
		 * ��ָ�� s ָ������β���ַ���ѹջ
		 * ��� s �����ڴ��ں������غ󣬿����ͷŵ���������������������;
		 * �����ڲ�������ָ��
		 * ��� s Ϊ NULL���� nil ѹջ������ NULL
		 */
		
        lua_pushstring(L,"kv");
		
		/*
		 * void lua_setfield (lua_State *L, int index, const char *k)
		 * ��һ���ȼ��� t[k] = v �Ĳ���,���� t �Ǹ�������������ֵ,�� v ��ջ�����Ǹ�ֵ
		 * ��������������ֵ����ջ
		 * ���� Lua ��һ��������������ܴ���һ�� "newindex" �¼���Ԫ����
		 */

		/*
		 * __mode
		 * һ�ű��Ԫ���е� __mode ����������ű��������
		 * �� __mode ����һ�������ַ� 'k' ���ַ���ʱ�����ű�����м���Ϊ������
		 * �� __mode ����һ�������ַ� 'v' ���ַ���ʱ�����ű������ֵ��Ϊ������
		 */
		
        lua_setfield(L,-2,"__mode");

		/*
		 * void lua_setmetatable (lua_State *L, int index)
		 * ��һ�ű���ջ����������Ϊ������������ֵ��Ԫ��
		 */
		
        lua_setmetatable(L,-2);
        // register it
		
		/*
		 * int luaL_ref (lua_State *L, int t)
		 * ���ջ���Ķ��󣬴���������һ�������� t ָ��ı��е����ã����ᵯ��ջ������
		 * ��������һ��Ψһ���������� ֻҪ�㲻��� t �ֹ����������,luaL_ref ���Ա�֤�����صļ���Ψһ��
		 * ����ͨ������ lua_rawgeti(L, t, r) ���һ��� r ���õĶ��� ���� luaL_unref �����ͷ�һ�����ù����Ķ���
		 * ���ջ���Ķ����� nil,luaL_ref �����س��� LUA_REFNIL
		 * ���� LUA_NOREF ���Ա�֤�� luaL_ref �ܷ��ص���������ֵ��ͬ��
		 */
		
        cacheObjRef = luaL_ref(L,LUA_REGISTRYINDEX);

		/*
		 * int lua_gettop (lua_State *L)
		 * ����ջ��Ԫ�ص�����
		 * ��Ϊ�����Ǵ� 1 ��ʼ��ŵ�,��������������ջ�ϵ�Ԫ�ظ���
		 * �ر�ָ����0 ��ʾջΪ��
		 */

        ensure(lua_gettop(L)==0);

		/*
		 * ����ջ���̷���
		 * ջ��================================>ջ��
		 * tableA,tableB								// new������table
		 * tableA,tableB,"kv"							// pushһ���ַ���,������������
		 *												// lua_setfield(L,-2,"__mode")
		 *												// => ����-2��,��tableB��__modeΪջ��,��"kv"
		 *												// => tableB.__mode = "kv"
		 * tableA,tableB								// => ����ջ������
		 *												// lua_setmetatable(L,-2)
		 *												// => ��һ�ű���ջ,����ջ�ϵı�ΪtableB
		 * tableA										// => ����Ϊ-2��,��tableA��Ԫ��
		 *												// luaL_ref(L,LUA_REGISTRYINDEX)
		 *												// => ���ջ���Ķ��󣬼�tableA
		 *												// => ����������һ�������� t ָ��ı��е�����
		 *												// => ����ջ������
		 * ջ��
		 */

		/*
		 * void luaL_openlibs (lua_State *L)
		 * ��ָ��״̬���е����� Lua ��׼��
		 */
        luaL_openlibs(L);

		/*
		 * void lua_pushcfunction (lua_State *L, lua_CFunction f)
		 * ��һ�� C ����ѹջ�� �����������һ�� C ����ָ��,����һ������Ϊ function �� Lua ֵѹջ
		 * �����ջ����ֵ������ʱ����������Ӧ�� C ����
		 * ע�ᵽ Lua �е��κκ�����������ѭ��ȷ��Э�������ղ����ͷ���ֵ
		 */

		/*
		 * void lua_setglobal (lua_State *L, const char *name)
		 * �Ӷ�ջ�ϵ���һ��ֵ����������Ϊȫ�ֱ��� name ����ֵ
		 */

		// UClass,UScriptStruct��UEnum
        lua_pushcfunction(L,import);
        lua_setglobal(L, "import");

		// �ڲ�����UE_LOG
        lua_pushcfunction(L,print);
        lua_setglobal(L, "print");

		lua_pushcfunction(L, dofile);
		lua_setglobal(L, "dofile");

        #if WITH_EDITOR
        // used for debug
		lua_pushcfunction(L, getStringFromMD5);
		lua_setglobal(L, "getStringFromMD5");
        #endif
		
        lua_pushcfunction(L,loader);
		// ��ʱջ����Ϊloader����
		// ջ��ʱΪ loader
        int loaderFunc = lua_gettop(L);

		/*
		 * int lua_getglobal (lua_State *L, const char *name)
		 * ��ȫ�ֱ��� name ���ֵѹջ�����ظ�ֵ������
		 */

		/*
		 * int lua_getfield (lua_State *L, int index, const char *k)
		 * �� t[k] ��ֵѹջ,����� t ������ָ���ֵ
		 * �� Lua �У�����������ܴ�����Ӧ "index" �¼���Ӧ��Ԫ����
		 * ����������ѹ��ֵ�����͡�
		 */

		 // ջ��ʱΪ loader,package��Ӧ��ֵ
        lua_getglobal(L,"package");
		// ��-1,��package��Ӧ��ֵ,package.searchersѹջ
		// ջ��ʱΪ loader,package��Ӧ��ֵ,package.searchers
        lua_getfield(L,-1,"searchers");

		// ����loaderTableΪpackage.searchers
        int loaderTable = lua_gettop(L);

		/*
		 * size_t lua_rawlen (lua_State *L, int index)
		 * ���ظ���������ֵ�Ĺ��С����ȡ�
		 * �����ַ���,��ָ�ַ����ĳ���
		 * ���ڱ�,��ָ������Ԫ�����������ȡ���Ȳ�����'#'��Ӧ�õ���ֵ
		 * �����û����ݣ���ָΪ���û����ݷ�����ڴ��Ĵ�С
		 * ��������ֵ����Ϊ 0 
		 */

		/*
		 * int lua_rawgeti (lua_State *L, int index, lua_Integer n)
		 * �� t[n] ��ֵѹջ,����� t ��ָ�����������ı�
		 * ����һ��ֱ�ӷ��ʣ�����˵�������ᴥ��Ԫ����
		 * ������ջֵ������
		 */

		/*
		 * void lua_rawseti (lua_State *L, int index, lua_Integer i)
		 * �ȼ��� t[i] = v ,����� t ��ָ�����������ı�,�� v ��ջ����ֵ
		 * ��������Ὣֵ����ջ�� ��ֵ��ֱ�ӵģ������ᴥ��Ԫ����
		 */

		// ����˳��һλ
		// The first searcher simply looks for a loader in the package.preload table

		/*
		 * package.preload
		 * A table to store loaders for specific modules (see require)
		 * This variable is only a reference to the real table
		 * assignments to this variable do not change the table used by require
		 */
		
		for (int i = lua_rawlen(L, loaderTable) + 1; i > 2; i--) {
			lua_rawgeti(L, loaderTable, i - 1);
			lua_rawseti(L, loaderTable, i);
		}
		
		/*
		 * void lua_pushvalue (lua_State *L, int index)
		 * ��ջ�ϸ�����������Ԫ����һ������ѹջ
		 */

		/*
		 * void lua_settop (lua_State *L, int index)
		 * �����������κ������Լ� 0
		 * �����Ѷ�ջ��ջ����Ϊ�������
		 * ����µ�ջ����ԭ���Ĵ�,�������ֵ���Ԫ�ؽ�����Ϊ nil
		 * ��� index Ϊ 0 ,��ջ������Ԫ���Ƴ�
		 */
		
		 // ջ��ʱΪ loader,package��Ӧ��ֵ,package.searchers,loader
        lua_pushvalue(L,loaderFunc);
		// ��loaderTable����Ӧ,package.searchers[2]=loader
        lua_rawseti(L,loaderTable,2);
		lua_settop(L, 0);

		// ע�ᵽslua����
		LuaSocket::init(L);
        LuaObject::init(L);
        SluaUtil::openLib(L);
        LuaClass::reg(L);
        LuaArray::reg(L);
        LuaMap::reg(L);
#ifdef ENABLE_PROFILER
		LuaProfiler::init(L);
#endif

		// �㲥һ�³�ʼ����Ϣ
		onInitEvent.Broadcast();

		/*
		 * int lua_gc (lua_State *L, int what, int data)
		 * ���������ռ���
		 * ���������������� what �����ֲ�ͬ������
		 * LUA_GCSTOP: ֹͣ�����ռ���
		 * LUA_GCRESTART: ���������ռ���
		 * LUA_GCCOLLECT: ����һ�������������ռ�ѭ��
		 * LUA_GCCOUNT: ���� Lua ʹ�õ��ڴ��������� K �ֽ�Ϊ��λ��
		 * LUA_GCCOUNTB: ���ص�ǰ�ڴ�ʹ�������� 1024 ������
		 * LUA_GCSTEP: ����һ�����������ռ�
		 * LUA_GCSETPAUSE: �� data ��Ϊ �����ռ�����Ъ�ʣ�������֮ǰ���õ�ֵ
		 * LUA_GCSETSTEPMUL: �� data ��Ϊ �����ռ����������ʣ�������֮ǰ���õ�ֵ
		 * LUA_GCISRUNNING: �����ռ����Ƿ������У���û��ֹͣ��
		 */

		// disable gc in main thread
		if (enableMultiThreadGC) lua_gc(L, LUA_GCSTOP, 0);

        lua_settop(L,0);

        return true;
    }

	void LuaState::attach(UGameInstance* GI) {
		this->pGI = GI;
	}

    int LuaState::_atPanic(lua_State* L) {
        const char* err = lua_tostring(L,-1);
        Log::Error("Fatal error: %s",err);
        return 0;
    }

	void LuaState::setLoadFileDelegate(LoadFileDelegate func) {
		loadFileDelegate = func;
	}

	void LuaState::setErrorDelegate(ErrorDelegate func) {
		errorDelegate = func;
	}

	// �ҵ����ڵ�
	static void* findParent(GenericUserData* parent) {
		auto pp = parent;
		while(true) {
			if (!pp->parent)
				break;
			pp = reinterpret_cast<GenericUserData*>(pp->parent);
		}
		return pp;
	}

	void LuaState::linkProp(void* parent, void* prop) {
		auto parentud = findParent(reinterpret_cast<GenericUserData*>(parent));
		auto propud = reinterpret_cast<GenericUserData*>(prop);
		propud->parent = parentud;
		auto& propList = propLinks.FindOrAdd(parentud);
		propList.Add(propud);
	}

	void LuaState::releaseLink(void* prop) {
		auto propud = reinterpret_cast<GenericUserData*>(prop);
		// Luaά��GC
		if (propud->flag & UD_AUTOGC) {
			auto propListPtr = propLinks.Find(propud);
			if (propListPtr) 
				for (auto& cprop : *propListPtr)
					// ��flagλ��Ϊ�Ѿ����ͷ�
					reinterpret_cast<GenericUserData*>(cprop)->flag |= UD_HADFREE;
		} else {
			propud->flag |= UD_HADFREE;
			auto propListPtr = propLinks.Find(propud->parent);
			if (propListPtr) 
				propListPtr->Remove(propud);
		}
	}

	void LuaState::releaseAllLink() {
		for (auto& pair : propLinks) 
			for (auto& prop : pair.Value) 
				reinterpret_cast<GenericUserData*>(prop)->flag |= UD_HADFREE;
		propLinks.Empty();
	}

	// engine will call this function on post gc
	// ����GC�ص�
	void LuaState::onEngineGC()
	{
		PROFILER_WATCHER(w1);
		// find freed uclass
		for (ClassCache::CacheFuncMap::TIterator it(classMap.cacheFuncMap); it; ++it)
			if (!it.Key().IsValid())
				it.RemoveCurrent();
		
		for (ClassCache::CachePropMap::TIterator it(classMap.cachePropMap); it; ++it)
			if (!it.Key().IsValid())
				it.RemoveCurrent();		
		
		freeDeferObject();

		// LUA_GCCOUNT: ���� Lua ʹ�õ��ڴ��������� K �ֽ�Ϊ��λ��
		Log::Log("Unreal engine GC, lua used %d KB",lua_gc(L, LUA_GCCOUNT, 0));
	}

	void LuaState::onWorldCleanup(UWorld * World, bool bSessionEnded, bool bCleanupResources)
	{
		PROFILER_WATCHER(w1);
		unlinkUObject(World);
	}

	// �ӳ��ͷ�
	// �Ƚ�Object��ӵ����TArray
	// ��GC�ص�����delete
	void LuaState::freeDeferObject()
	{
		// really delete FGCObject
		for (auto ptr : deferDelete)
			delete ptr;
		deferDelete.Empty();
	}

	LuaVar LuaState::doBuffer(const uint8* buf, uint32 len, const char* chunk, LuaVar* pEnv) {
		// �Զ��ָ�ջ,ԭ��ʵ��
        AutoStack g(L);

		// ��Error�ص�ѹջ
        int errfunc = pushErrorHandler(L);

		// LUA_OK: û�д��� =>0
        if(luaL_loadbuffer(L, (const char *)buf, len, chunk)) {
        	// ���������ѹ�������Ϣ
        	// ����ȥȡ������Ϣ
            const char* err = lua_tostring(L,-1);
            Log::Error("DoBuffer failed: %s",err);
            return LuaVar();
        }

		// ��һ������õĴ������Ϊһ�� Lua ����ѹ��ջ��
		// ����ȡ-1,����ȡ�������õĴ����
		LuaVar f(L, -1);
		return f.call();
    }

    LuaVar LuaState::doString(const char* str, LuaVar* pEnv) {
        // fix #31 & #30 issue, 
        // vc compile optimize code cause cl.exe dead loop in release mode(no WITH_EDITOR)
        // if turn optimze flag on
        // so just write complex code to bypass link optimize
        // like this, WTF!
        uint32 len = strlen(str);
        #if WITH_EDITOR
		FString md5FString = FMD5::HashAnsiString(UTF8_TO_TCHAR(str));
		debugStringMap.Add(md5FString, UTF8_TO_TCHAR(str));
        return doBuffer((const uint8*)str,len,TCHAR_TO_UTF8(*md5FString),pEnv);
        #else
        return doBuffer((const uint8*)str,len,str,pEnv);
        #endif
    }


	// ��ȡ�ļ�����,�ٵ���doBuffer
    LuaVar LuaState::doFile(const char* fn, LuaVar* pEnv) {
        uint32 len;
        FString filepath;
        if(uint8* buf=loadFile(fn,len,filepath)) {
            char chunk[256];
            snprintf(chunk,256,"@%s",TCHAR_TO_UTF8(*filepath));

            LuaVar r = doBuffer( buf,len,chunk,pEnv );
            delete[] buf;
            return r;
        }
        return LuaVar();
    }

	void LuaState::NotifyUObjectDeleted(const UObjectBase * Object, int32 Index)
	{
		PROFILER_WATCHER(w1);
		unlinkUObject((const UObject*)Object);
	}

	// ����������ͷŵ�
	void LuaState::unlinkUObject(const UObject * Object)
	{
		// find Object from objRefs, maybe nothing
		auto udptr = objRefs.Find(Object);
		// maybe Object not push to lua
		if (!udptr) {
			return;
		}

		GenericUserData* ud = *udptr;

		// remove should put here avoid ud is invalid
		// remove ref, Object must be an UObject in slua
		objRefs.Remove(const_cast<UObject*>(Object));

		// maybe ud is nullptr or had been freed
		if (!ud) {
			// remove should put here avoid ud is invalid
			objRefs.Remove(const_cast<UObject*>(Object));
			return;
		}
		else if (ud->flag & UD_HADFREE)
			return;

		// indicate ud had be free
		// ����flagλ
		ud->flag |= UD_HADFREE;
		// �Ƴ�cache
		// remove cache
		ensure(ud->ud == Object);
		LuaObject::removeFromCache(L, ud->ud);
	}

	// �ѳ��еĶ����������ռ�����
	void LuaState::AddReferencedObjects(FReferenceCollector & Collector)
	{
		for (UObjectRefMap::TIterator it(objRefs); it; ++it)
		{
			UObject* item = it.Key();
			GenericUserData* userData = it.Value();
			if (userData && !(userData->flag & UD_REFERENCE))
			{
				continue;
			}
			Collector.AddReferencedObject(item);
		}
		// do more gc step in collecting thread
		// lua_gc can be call async in bg thread in some isolate position
		// but this position equivalent to main thread
		// we just try and find some proper async position
		if (enableMultiThreadGC && L) lua_gc(L, LUA_GCSTEP, 128);
	}
#if (ENGINE_MINOR_VERSION>=23) && (ENGINE_MAJOR_VERSION>=4)
	void LuaState::OnUObjectArrayShutdown() {
		// nothing todo, we don't add any listener to FUObjectDeleteListener
	}
#endif

	int LuaState::pushErrorHandler(lua_State* L) {
        auto ls = get(L);
        ensure(ls!=nullptr);
        return ls->_pushErrorHandler(L);
    }
	
	TStatId LuaState::GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(LuaState, STATGROUP_Game);
	}

	int LuaState::addThread(lua_State *thread)
	{
		/*
		 * int lua_pushthread (lua_State *L)
		 * �� L ��ʾ���߳�ѹջ�� �������߳��ǵ�ǰ״̬�������̵߳Ļ������� 1
		 */
		int isMainThread = lua_pushthread(thread);
		if (isMainThread == 1)
		{
			lua_pop(thread, 1);

			luaL_error(thread, "Can't call latent action in main lua thread!");
			return LUA_REFNIL;
		}

		/*
		 * void lua_xmove (lua_State *from, lua_State *to, int n)
		 * ����ͬһ��״̬���²�ͬ�߳��е�ֵ
		 * ���������� from ��ջ�ϵ��� n ��ֵ�� Ȼ�������ѹ�� to ��ջ��
		 */

		
		lua_xmove(thread, L, 1);
		lua_pop(thread, 1);

		/*
		 * int lua_isthread (lua_State *L, int index)
		 * ������������ֵ��һ���߳�ʱ������ 1 �����򷵻� 0
		 */

		 // ��ʱLջ��Ϊthread
		ensure(lua_isthread(L, -1));

		int threadRef = luaL_ref(L, LUA_REGISTRYINDEX);
		threadToRef.Add(thread, threadRef);
		refToThread.Add(threadRef, thread);

		return threadRef;
	}

	void LuaState::resumeThread(int threadRef)
	{
		QUICK_SCOPE_CYCLE_COUNTER(Lua_LatentCallback);

		lua_State **threadPtr = refToThread.Find(threadRef);
		if (threadPtr)
		{
			lua_State *thread = *threadPtr;
			bool threadIsDead = false;

			/*
			 * int lua_status (lua_State *L)
			 * �����߳� L ��״̬
			 * �������߳�״̬�� 0 ��LUA_OK��
			 * ���߳��� lua_resume ִ����ϲ��׳���һ������ʱ�� ״ֵ̬�Ǵ�����
			 * ����̱߳�����״̬Ϊ LUA_YIELD
			 * ��ֻ����״̬Ϊ LUA_OK ���߳��е��ú���
			 * ���������һ��״̬Ϊ LUA_OK ���߳� �����ڿ�ʼ��Э�̣�
			 * ����״̬Ϊ LUA_YIELD ���߳� ����������Э�̣�
			 */
			if (lua_status(thread) == LUA_OK && lua_gettop(thread) == 0)
			{
				Log::Error("cannot resume dead coroutine");
				threadIsDead = true;
			}
			else
			{
				/*
				 * int lua_resume (lua_State *L, lua_State *from, int nargs)
				 * �ڸ����߳�������������һ��Э��
				 * Ҫ����һ��Э�̵Ļ��� ����Ҫ���������Լ�����Ҫ�Ĳ���ѹ���߳�ջ
				 * Ȼ����� lua_resume �� �� nargs ��Ϊ�����ĸ���
				 * ��ε��û���Э�̹���ʱ���ǽ������к󷵻�
				 * ����������ʱ����ջ�л��д��� lua_yield ������ֵ�� ���������������з���ֵ
				 * ��Э���ó��� lua_resume ���� LUA_YIELD �� ��Э�̽���������û���κδ���ʱ������ 0
				 * ����д��򷵻ش������
				 * �ڷ�������������,��ջû��չ��,��������ʹ�õ��� API ��������
				 * ������Ϣ����ջ��
				 * Ҫ����һ��Э�̣� ����Ҫ����ϴ� lua_yield �����µ����н��
				 * �����Ҫ���� yield �������ֵѹջ�� Ȼ����� lua_resume
				 * ���� from ��ʾЭ�̴��ĸ�Э���������� L ��
				 * �������������һ��Э�̣�������������� NULL
				 */
				
				int status = lua_resume(thread, L, 0);
				if (status == LUA_OK || status == LUA_YIELD)
				{
					int nres = lua_gettop(thread);

					/*
					 * int lua_checkstack (lua_State *L, int n)
					 * ȷ����ջ�������� n �������λ
					 * ������ܰѶ�ջ��չ����Ӧ�ĳߴ磬�������ؼ�
					 * ʧ�ܵ�ԭ���������ջ��չ���ȹ̶����ߴ绹�� �������Ǽ�ǧ��Ԫ�أ�������ڴ�ʧ��
					 * ���������Զ������С��ջ
					 * �����ջ�Ѿ�����Ҫ�Ĵ��ˣ���ô�ͱ���ԭ��
					 */
					if (!lua_checkstack(L, nres + 1))
					{
						lua_pop(thread, nres);  /* remove results anyway */
						Log::Error("too many results to resume");
						threadIsDead = true;
					}
					else
					{
						// �ƶ�����
						lua_xmove(thread, L, nres);  /* move yielded values */

						if (status == LUA_OK)
						{
							// ��־Ϊ�Ѿ��ر�
							threadIsDead = true;
						}
					}
				}
				else
				{
					// �Ѵ����ƶ�����ǰ�߳�
					lua_xmove(thread, L, 1);  /* move error message */
					const char* err = lua_tostring(L, -1);
					luaL_traceback(L, thread, err, 0);
					err = lua_tostring(L, -1);
					Log::Error("%s", err);
					lua_pop(L, 1);

					threadIsDead = true;
				}
			}

			if (threadIsDead)
			{
				threadToRef.Remove(thread);
				refToThread.Remove(threadRef);
				luaL_unref(L, LUA_REGISTRYINDEX, threadRef);
			}
		}
	}

	int LuaState::findThread(lua_State *thread)
	{
		int32 *threadRefPtr = threadToRef.Find(thread);
		return threadRefPtr ? *threadRefPtr : LUA_REFNIL;
	}

	void LuaState::cleanupThreads()
	{
		for (TMap<lua_State*, int32>::TIterator It(threadToRef); It; ++It)
		{
			lua_State *thread = It.Key();
			int32 threadRef = It.Value();
			if (threadRef != LUA_REFNIL)
			{
				luaL_unref(L, LUA_REGISTRYINDEX, threadRef);
			}
		}
		threadToRef.Empty();
		refToThread.Empty();
	}

	ULatentDelegate* LuaState::getLatentDelegate() const
	{
		return latentDelegate;
	}

	int LuaState::_pushErrorHandler(lua_State* state) {
        lua_pushcfunction(state,error);
        return lua_gettop(state);
    }

    LuaVar LuaState::get(const char* key) {
        // push global table
        lua_pushglobaltable(L);

        FString path(key);
        FString left,right;
        LuaVar rt;
		// a.b.c.d
		// ��һ�ε�ʱ��,a���ȫ�ֱ�����ȡ
        while(strSplit(path,".",&left,&right)) {
            if(lua_type(L,-1)!=LUA_TTABLE) break;
            lua_pushstring(L,TCHAR_TO_UTF8(*left));
            lua_gettable(L,-2);
            rt.set(L,-1);
        	// ����һ�εı��Ƴ�,����ջ��Ϊ��ȡ�������±�
            lua_remove(L,-2);
            if(rt.isNil()) break;
            path = right;
        }
        lua_pop(L,1);
        return rt;
    }

	bool LuaState::set(const char * key, LuaVar v)
	{
		// push global table
		AutoStack as(L);
		lua_pushglobaltable(L);

		FString path(key);
		FString left, right;
		LuaVar rt;
		while (strSplit(path, ".", &left, &right)) {
			if (lua_type(L, -1) != LUA_TTABLE) return false;
			lua_pushstring(L, TCHAR_TO_UTF8(*left));
			lua_gettable(L, -2);
			if (lua_isnil(L, -1))
			{
				// pop nil
				lua_pop(L, 1);
				if (right.IsEmpty()) {
					// a.b bΪ��,b����ȫ��
					lua_pushstring(L, TCHAR_TO_UTF8(*left));
					v.push(L);
					lua_rawset(L, -3);
					return true;
				}
				else {
					// a.b a��Ϊ��,bΪ��,�Ƚ�a����ȫ��,�ٰ�b��ӵ�a
					lua_newtable(L);
					lua_pushstring(L, TCHAR_TO_UTF8(*left));
					// push table again
					lua_pushvalue(L, -2);
					lua_rawset(L, -4);
					// stack leave table for next get
				}
			}
			else
			{
				// if sub field isn't table, set failed
				// a.b ���a��Ϊ��,b����һ����,����
				if (!right.IsEmpty() && !lua_istable(L, -1))
					return false;

				if (lua_istable(L, -1) && right.IsEmpty()) {
					lua_pushstring(L, TCHAR_TO_UTF8(*left));
					v.push(L);
					lua_rawset(L, -3);
					return true;
				}
			}
			path = right;
		}
		return false;
	}

    LuaVar LuaState::createTable() {
        lua_newtable(L);
        LuaVar ret(L,-1);
        lua_pop(L,1);
        return ret;
    }

	LuaVar LuaState::createTable(const char * key)
	{
		lua_newtable(L);
		LuaVar ret(L, -1);
		set(key, ret);
		lua_pop(L, 1);
		return ret;
	}

	void LuaState::setTickFunction(LuaVar func)
	{
		stateTickFunc = func;
	}

	void LuaState::addRef(UObject* obj, void* ud, bool ref)
	{
		// �Ȱ���һ���Ƴ�
		auto* udptr = objRefs.Find(obj);
		// if any obj find in objRefs, it should be flag freed and removed
		if (udptr) {
			(*udptr)->flag |= UD_HADFREE;
			objRefs.Remove(obj);
		}

		GenericUserData* userData = (GenericUserData*)ud;
		if (ref && userData) {
			userData->flag |= UD_REFERENCE;
		}
		objRefs.Add(obj,userData);
	}

	FDeadLoopCheck::FDeadLoopCheck()
		: timeoutEvent(nullptr)
		, timeoutCounter(0)
		, stopCounter(0)
		, frameCounter(0)
	{
		thread = FRunnableThread::Create(this, TEXT("FLuaDeadLoopCheck"), 0, TPri_BelowNormal);
	}

	FDeadLoopCheck::~FDeadLoopCheck()
	{
		Stop();
		thread->WaitForCompletion();
		SafeDelete(thread);
	}

	uint32 FDeadLoopCheck::Run()
	{
		while (stopCounter.GetValue() == 0) {
			FPlatformProcess::Sleep(1.0f);
			if (frameCounter.GetValue() != 0) {
				timeoutCounter.Increment();
				Log::Log("script run time %d", timeoutCounter.GetValue());
				if(timeoutCounter.GetValue() >= MaxLuaExecTime)
					onScriptTimeout();
			}
		}
		return 0;
	}

	void FDeadLoopCheck::Stop()
	{
		stopCounter.Increment();
	}

	int FDeadLoopCheck::scriptEnter(ScriptTimeoutEvent* pEvent)
	{
		int ret = frameCounter.Increment();
		if ( ret == 1) {
			timeoutCounter.Set(0);
			timeoutEvent.store(pEvent);
		}
		return ret;
	}

	int FDeadLoopCheck::scriptLeave()
	{
		return frameCounter.Decrement();
	}

	void FDeadLoopCheck::onScriptTimeout()
	{
		auto pEvent = timeoutEvent.load();
		if (pEvent) {
			timeoutEvent.store(nullptr);
			pEvent->onTimeout();
		}
	}

	// �������
	LuaScriptCallGuard::LuaScriptCallGuard(lua_State * L_)
		:L(L_)
	{
		auto ls = LuaState::get(L);
		ls->deadLoopCheck->scriptEnter(this);
	}

	LuaScriptCallGuard::~LuaScriptCallGuard()
	{
		auto ls = LuaState::get(L);
		ls->deadLoopCheck->scriptLeave();
	}

	void LuaScriptCallGuard::onTimeout()
	{
		/*
		 * lua_Hook lua_gethook (lua_State *L)
		 * ���ص�ǰ�Ĺ��Ӻ���
		 */
		
		auto hook = lua_gethook(L);
		// if debugger isn't exists
		if (hook == nullptr) {
			/*
			 * void lua_sethook (lua_State *L, lua_Hook f, int mask, int count)
			 * ����һ�������ù��Ӻ���
			 * ���� f �ǹ��Ӻ���
			 * mask ָ������Щ�¼�ʱ����ã���������һ��λ�������� LUA_MASKCALL�� LUA_MASKRET�� LUA_MASKLINE�� LUA_MASKCOUNT
			 * ���� count ֻ�������а����� LUA_MASKCOUNT �����������ÿ���¼������ӱ����õ�����������£�
			 * call hook: �ڽ���������һ������ʱ�����á� ���ӽ��� Lua ����һ���º����� ������ȡ����ǰ������
			 * return hook: �ڽ�������һ�������з���ʱ����
			 *	���ӽ��� Lua �뿪����֮ǰ����һ�̱�����
			 *	û�б�׼���������ʱ��������ص���Щֵ
			 * line hook: �ڽ�����׼����ʼִ���µ�һ�д���ʱ�� ������ת�����д�����ʱ����ʹ��ͬһ������ת��������
			 *	������¼������� Lua ִ��һ�� Lua ����ʱ������
			 * count hook: �ڽ�����ÿִ�� count ��ָ��󱻵���
			 *	������¼������� Lua ִ��һ�� Lua ����ʱ������
			 * ���ӿ���ͨ������ mask Ϊ������
			 */
			// this function thread safe
			lua_sethook(L, scriptTimeout, LUA_MASKLINE, 0);
		}
	}

	void LuaScriptCallGuard::scriptTimeout(lua_State *L, lua_Debug *ar)
	{
		// only report once
		lua_sethook(L, nullptr, 0, 0);
		luaL_error(L, "script exec timeout");
	}

	UFunction* LuaState::ClassCache::findFunc(UClass* uclass, const char* fname)
	{
		auto item = cacheFuncMap.Find(uclass);
		if (!item) return nullptr;
		auto func = item->Find(UTF8_TO_TCHAR(fname));
		if(func!=nullptr)
			return func->IsValid() ? func->Get() : nullptr;
		return nullptr;
	}

	UProperty* LuaState::ClassCache::findProp(UClass* uclass, const char* pname)
	{
		auto item = cachePropMap.Find(uclass);
		if (!item) return nullptr;
		auto prop = item->Find(UTF8_TO_TCHAR(pname));
		if (prop != nullptr)
			return prop->IsValid() ? prop->Get() : nullptr;
		return nullptr;
	}

	void LuaState::ClassCache::cacheFunc(UClass* uclass, const char* fname, UFunction* func)
	{
		auto& item = cacheFuncMap.FindOrAdd(uclass);
		item.Add(UTF8_TO_TCHAR(fname), func);
	}

	void LuaState::ClassCache::cacheProp(UClass* uclass, const char* pname, UProperty* prop)
	{
		auto& item = cachePropMap.FindOrAdd(uclass);
		item.Add(UTF8_TO_TCHAR(pname), prop);
	}
}
