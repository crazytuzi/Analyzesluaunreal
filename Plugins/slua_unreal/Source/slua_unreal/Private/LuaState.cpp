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

	// 最大执行时间
	const int MaxLuaExecTime = 5; // in second

    int import(lua_State *L) {
        const char* name = LuaObject::checkValue<const char*>(L,1);
        if(name) {
        	// 依次查询UClass,UScriptStruct和UEnum
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
    	// 参数的个数
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

	// Lua调用
	int dofile(lua_State *L) {

    	/*
    	 * const char *luaL_checkstring (lua_State *L, int arg)
    	 * 检查函数的第 arg 个参数是否是一个 字符串并返回这个字符串
    	 * 这个函数使用 lua_tolstring 来获取结果
    	 * 所以该函数有可能引发的转换都同样有效
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
    	 * 等价于调用 lua_tolstring,其参数 len 为 NULL
    	 */

    	/*
    	 * const char *lua_tolstring (lua_State *L, int index, size_t *len)
    	 * 把给定索引处的 Lua 值转换为一个 C 字符串
    	 * 如果 len 不为 NULL,它还把字符串长度设到 *len 中
    	 * 这个 Lua 值必须是一个字符串或是一个数字,否则返回返回 NULL
    	 * 如果值是一个数字,lua_tolstring 还会把堆栈中的那个值的实际类型转换为一个字符串
    	 * 当遍历一张表的时候,若把 lua_tolstring 作用在键上， 这个转换有可能导致 lua_next 弄错
    	 * lua_tolstring 返回一个已对齐指针指向 Lua 状态机中的字符串
    	 * 这个字符串总能保证（ C 要求的）最后一个字符为零 ('\0'),而且它允许在字符串内包含多个这样的零
    	 * 因为 Lua 中可能发生垃圾收集,所以不保证 lua_tolstring 返回的指针,在对应的值从堆栈中移除后依然有效
    	 */
    	
        const char* err = lua_tostring(L,1);

    	/*
    	 * void luaL_traceback (lua_State *L, lua_State *L1, const char *msg, int level)
    	 * 将栈 L1 的栈回溯信息压栈
    	 * 如果 msg 不为 NULL ，它会附加到栈回溯信息之前
    	 * level 参数指明从第几层开始做栈回溯
    	 */
    	
        luaL_traceback(L,L,err,1);
        err = lua_tostring(L,2);
        lua_pop(L,1);
		auto ls = LuaState::get(L);
		ls->onError(err);
        return 0;
    }

	void LuaState::onError(const char* err) {
    	// 如果有自定义代理
		if (errorDelegate) errorDelegate(err);
    	// 没有就输出Log
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
    	// 加载内容
        if(uint8* buf = state->loadFile(fn,len,filepath)) {
            AutoDeleteArray<uint8> defer(buf);

            char chunk[256];
            snprintf(chunk,256,"@%s",TCHAR_TO_UTF8(*filepath));

			/*
			 * int luaL_loadbuffer (lua_State *L,const char *buff,size_t sz,const char *name)
			 * 等价于 luaL_loadbufferx,其 mode 参数等于 NULL
			 */

        	/*
        	 * int luaL_loadbufferx (lua_State *L,const char *buff,size_t sz,const char *name,const char *mode)
        	 * 把一段缓存加载为一个 Lua 代码块
        	 * 这个函数使用 lua_load 来加载 buff 指向的长度为 sz 的内存区
        	 * 这个函数和 lua_load 返回值相同。
        	 * name 作为代码块的名字，用于调试信息和错误消息
        	 * mode 字符串的作用同函数 lua_load
        	 */

        	/*
        	 * int lua_load (lua_State *L,lua_Reader reader,void *data,const char *chunkname,const char *mode)
        	 * 加载一段 Lua 代码块，但不运行它
        	 * 如果没有错误,lua_load 把一个编译好的代码块作为一个 Lua 函数压到栈顶
        	 * 否则，压入错误消息
        	 * lua_load 的返回值可以是：
        	 * LUA_OK: 没有错误
        	 * LUA_ERRSYNTAX: 在预编译时碰到语法错误
        	 * LUA_ERRMEM: 内存分配错误
        	 * LUA_ERRGCMM: 在运行 __gc 元方法时出错了。（这个错误和代码块加载过程无关，它是由垃圾收集器引发的。）
        	 * lua_load 函数使用一个用户提供的 reader 函数来读取代码块
        	 * data 参数会被传入 reader 函数
        	 * chunkname 这个参数可以赋予代码块一个名字,这个名字被用于出错信息和调试信息。
        	 * lua_load 会自动检测代码块是文本的还是二进制的，然后做对应的加载操作
        	 * 字符串 mode 的作用和函数 load 一致。 它还可以是 NULL 等价于字符串 "bt"。
        	 * lua_load 的内部会使用栈， 因此 reader 函数必须永远在每次返回时保留栈的原样。
        	 * 如果返回的函数有上值， 第一个上值会被设置为 保存在注册表 LUA_RIDX_GLOBALS 索引处的全局环境
        	 * 在加载主代码块时，这个上值是 _ENV 变量
        	 * 其它上值均被初始化为 nil。
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

	// 加载文件,允许自定义委托
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
	// 一直去检查栈顶
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
		// 请求一次GC
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
        	 * 销毁指定 Lua 状态机中的所有对象
        	 * 如果有垃圾收集相关的元方法的话，会调用它们
        	 * 并且释放状态机中使用的所有动态内存
        	 * 在一些平台上，你可以不必调用这个函数， 因为当宿主程序结束的时候，所有的资源就自然被释放掉了
        	 * 另一方面，长期运行的程序，比如一个后台程序或是一个网站服务器
        	 * 会创建出多个 Lua 状态机,那么就应该在不需要时赶紧关闭它们 
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

		// 死循环检查,第一次调用会被创建
        if(deadLoopCheck)
            return false;

		// 调用init的即为主State
        if(!mainState) 
            mainState = this;

		enableMultiThreadGC = gcFlag;
		// 绑定GC回调
		pgcHandler = FCoreUObjectDelegates::GetPostGarbageCollect().AddRaw(this, &LuaState::onEngineGC);
		wcHandler = FWorldDelegates::OnWorldCleanup.AddRaw(this, &LuaState::onWorldCleanup);
		// 继承于FUObjectDeleteListener,被删除的时候会调用NotifyUObjectDeleted
		GUObjectArray.AddUObjectDeleteListener(this);

		latentDelegate = NewObject<ULatentDelegate>((UObject*)GetTransientPackage(), ULatentDelegate::StaticClass());
		latentDelegate->bindLuaState(this);

        stackCount = 0;
		// mainState 位于栈的第一个
        si = ++StateIndex;

		// 清空
		propLinks.Empty();
		classMap.clear();
		objRefs.Empty();

#if WITH_EDITOR
		// used for debug
		debugStringMap.Empty();
#endif

		deadLoopCheck = new FDeadLoopCheck();

        // use custom memory alloc func to profile memory footprint
		// 生成一个新的lua_State
		// 创建一个运行在新的独立的状态机中的线程。
		// 如果无法创建线程或状态机（由于内存有限）则返回 NULL。
		// 参数 f 是一个分配器函数； Lua 将通过这个函数做状态机内所有的内存分配操作。
		// 第二个参数 ud ，这个指针将在每次调用分配器时被转入。
        L = lua_newstate(LuaMemoryProfile::alloc,this);
		// 设置一个新的 panic 函数，并返回之前设置的那个。
		// panic 函数以错误消息处理器的方式运行
        lua_atpanic(L,_atPanic);
        // bind this to L
        *((void**)lua_getextraspace(L)) = this;
        stateMapFromIndex.Add(si,this);

        // init obj cache table
		
        /*
         * void lua_newtable (lua_State *L)
         * 创建一张空表，并将其压栈
         * 等价于 lua_createtable(L, 0, 0) 
         */
		
        lua_newtable(L);
        lua_newtable(L);
		
		/*
		 * const char *lua_pushstring (lua_State *L, const char *s)
		 * 将指针 s 指向的零结尾的字符串压栈
		 * 因此 s 处的内存在函数返回后，可以释放掉或是立刻重用于其它用途
		 * 返回内部副本的指针
		 * 如果 s 为 NULL，将 nil 压栈并返回 NULL
		 */
		
        lua_pushstring(L,"kv");
		
		/*
		 * void lua_setfield (lua_State *L, int index, const char *k)
		 * 做一个等价于 t[k] = v 的操作,这里 t 是给出的索引处的值,而 v 是栈顶的那个值
		 * 这个函数将把这个值弹出栈
		 * 跟在 Lua 中一样，这个函数可能触发一个 "newindex" 事件的元方法
		 */

		/*
		 * __mode
		 * 一张表的元表中的 __mode 域控制着这张表的弱属性
		 * 当 __mode 域是一个包含字符 'k' 的字符串时，这张表的所有键皆为弱引用
		 * 当 __mode 域是一个包含字符 'v' 的字符串时，这张表的所有值皆为弱引用
		 */
		
        lua_setfield(L,-2,"__mode");

		/*
		 * void lua_setmetatable (lua_State *L, int index)
		 * 把一张表弹出栈，并将其设为给定索引处的值的元表
		 */
		
        lua_setmetatable(L,-2);
        // register it
		
		/*
		 * int luaL_ref (lua_State *L, int t)
		 * 针对栈顶的对象，创建并返回一个在索引 t 指向的表中的引用（最后会弹出栈顶对象）
		 * 此引用是一个唯一的整数键。 只要你不向表 t 手工添加整数键,luaL_ref 可以保证它返回的键的唯一性
		 * 可以通过调用 lua_rawgeti(L, t, r) 来找回由 r 引用的对象。 函数 luaL_unref 用来释放一个引用关联的对象
		 * 如果栈顶的对象是 nil,luaL_ref 将返回常量 LUA_REFNIL
		 * 常量 LUA_NOREF 可以保证和 luaL_ref 能返回的其它引用值不同。
		 */
		
        cacheObjRef = luaL_ref(L,LUA_REGISTRYINDEX);

		/*
		 * int lua_gettop (lua_State *L)
		 * 返回栈顶元素的索引
		 * 因为索引是从 1 开始编号的,所以这个结果等于栈上的元素个数
		 * 特别指出，0 表示栈为空
		 */

        ensure(lua_gettop(L)==0);

		/*
		 * 上述栈流程分析
		 * 栈底================================>栈顶
		 * tableA,tableB								// new了两个table
		 * tableA,tableB,"kv"							// push一个字符串,用来设置弱表
		 *												// lua_setfield(L,-2,"__mode")
		 *												// => 设置-2处,即tableB的__mode为栈顶,即"kv"
		 *												// => tableB.__mode = "kv"
		 * tableA,tableB								// => 并将栈顶弹出
		 *												// lua_setmetatable(L,-2)
		 *												// => 把一张表弹出栈,现在栈上的表为tableB
		 * tableA										// => 设置为-2处,即tableA的元表
		 *												// luaL_ref(L,LUA_REGISTRYINDEX)
		 *												// => 针对栈顶的对象，即tableA
		 *												// => 创建并返回一个在索引 t 指向的表中的引用
		 *												// => 弹出栈顶对象
		 * 栈空
		 */

		/*
		 * void luaL_openlibs (lua_State *L)
		 * 打开指定状态机中的所有 Lua 标准库
		 */
        luaL_openlibs(L);

		/*
		 * void lua_pushcfunction (lua_State *L, lua_CFunction f)
		 * 将一个 C 函数压栈。 这个函数接收一个 C 函数指针,并将一个类型为 function 的 Lua 值压栈
		 * 当这个栈顶的值被调用时，将触发对应的 C 函数
		 * 注册到 Lua 中的任何函数都必须遵循正确的协议来接收参数和返回值
		 */

		/*
		 * void lua_setglobal (lua_State *L, const char *name)
		 * 从堆栈上弹出一个值，并将其设为全局变量 name 的新值
		 */

		// UClass,UScriptStruct和UEnum
        lua_pushcfunction(L,import);
        lua_setglobal(L, "import");

		// 内部调用UE_LOG
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
		// 此时栈顶即为loader函数
		// 栈此时为 loader
        int loaderFunc = lua_gettop(L);

		/*
		 * int lua_getglobal (lua_State *L, const char *name)
		 * 把全局变量 name 里的值压栈，返回该值的类型
		 */

		/*
		 * int lua_getfield (lua_State *L, int index, const char *k)
		 * 把 t[k] 的值压栈,这里的 t 是索引指向的值
		 * 在 Lua 中，这个函数可能触发对应 "index" 事件对应的元方法
		 * 函数将返回压入值的类型。
		 */

		 // 栈此时为 loader,package对应的值
        lua_getglobal(L,"package");
		// 把-1,即package对应的值,package.searchers压栈
		// 栈此时为 loader,package对应的值,package.searchers
        lua_getfield(L,-1,"searchers");

		// 设置loaderTable为package.searchers
        int loaderTable = lua_gettop(L);

		/*
		 * size_t lua_rawlen (lua_State *L, int index)
		 * 返回给定索引处值的固有“长度”
		 * 对于字符串,它指字符串的长度
		 * 对于表,它指不触发元方法的情况下取长度操作（'#'）应得到的值
		 * 对于用户数据，它指为该用户数据分配的内存块的大小
		 * 对于其它值，它为 0 
		 */

		/*
		 * int lua_rawgeti (lua_State *L, int index, lua_Integer n)
		 * 把 t[n] 的值压栈,这里的 t 是指给定索引处的表
		 * 这是一次直接访问；就是说，它不会触发元方法
		 * 返回入栈值的类型
		 */

		/*
		 * void lua_rawseti (lua_State *L, int index, lua_Integer i)
		 * 等价于 t[i] = v ,这里的 t 是指给定索引处的表,而 v 是栈顶的值
		 * 这个函数会将值弹出栈。 赋值是直接的；即不会触发元方法
		 */

		// 往后都顺延一位
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
		 * 把栈上给定索引处的元素作一个副本压栈
		 */

		/*
		 * void lua_settop (lua_State *L, int index)
		 * 参数允许传入任何索引以及 0
		 * 它将把堆栈的栈顶设为这个索引
		 * 如果新的栈顶比原来的大,超出部分的新元素将被填为 nil
		 * 如果 index 为 0 ,把栈上所有元素移除
		 */
		
		 // 栈此时为 loader,package对应的值,package.searchers,loader
        lua_pushvalue(L,loaderFunc);
		// 把loaderTable即对应,package.searchers[2]=loader
        lua_rawseti(L,loaderTable,2);
		lua_settop(L, 0);

		// 注册到slua下面
		LuaSocket::init(L);
        LuaObject::init(L);
        SluaUtil::openLib(L);
        LuaClass::reg(L);
        LuaArray::reg(L);
        LuaMap::reg(L);
#ifdef ENABLE_PROFILER
		LuaProfiler::init(L);
#endif

		// 广播一下初始化消息
		onInitEvent.Broadcast();

		/*
		 * int lua_gc (lua_State *L, int what, int data)
		 * 控制垃圾收集器
		 * 这个函数根据其参数 what 发起几种不同的任务：
		 * LUA_GCSTOP: 停止垃圾收集器
		 * LUA_GCRESTART: 重启垃圾收集器
		 * LUA_GCCOLLECT: 发起一次完整的垃圾收集循环
		 * LUA_GCCOUNT: 返回 Lua 使用的内存总量（以 K 字节为单位）
		 * LUA_GCCOUNTB: 返回当前内存使用量除以 1024 的余数
		 * LUA_GCSTEP: 发起一步增量垃圾收集
		 * LUA_GCSETPAUSE: 把 data 设为 垃圾收集器间歇率，并返回之前设置的值
		 * LUA_GCSETSTEPMUL: 把 data 设为 垃圾收集器步进倍率，并返回之前设置的值
		 * LUA_GCISRUNNING: 返回收集器是否在运行（即没有停止）
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

	// 找到根节点
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
		// Lua维护GC
		if (propud->flag & UD_AUTOGC) {
			auto propListPtr = propLinks.Find(propud);
			if (propListPtr) 
				for (auto& cprop : *propListPtr)
					// 把flag位置为已经被释放
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
	// 引擎GC回调
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

		// LUA_GCCOUNT: 返回 Lua 使用的内存总量（以 K 字节为单位）
		Log::Log("Unreal engine GC, lua used %d KB",lua_gc(L, LUA_GCCOUNT, 0));
	}

	void LuaState::onWorldCleanup(UWorld * World, bool bSessionEnded, bool bCleanupResources)
	{
		PROFILER_WATCHER(w1);
		unlinkUObject(World);
	}

	// 延迟释放
	// 先将Object添加到这个TArray
	// 在GC回调里面delete
	void LuaState::freeDeferObject()
	{
		// really delete FGCObject
		for (auto ptr : deferDelete)
			delete ptr;
		deferDelete.Empty();
	}

	LuaVar LuaState::doBuffer(const uint8* buf, uint32 len, const char* chunk, LuaVar* pEnv) {
		// 自动恢复栈,原理看实现
        AutoStack g(L);

		// 将Error回调压栈
        int errfunc = pushErrorHandler(L);

		// LUA_OK: 没有错误 =>0
        if(luaL_loadbuffer(L, (const char *)buf, len, chunk)) {
        	// 发生错误会压入错误消息
        	// 这里去取错误消息
            const char* err = lua_tostring(L,-1);
            Log::Error("DoBuffer failed: %s",err);
            return LuaVar();
        }

		// 把一个编译好的代码块作为一个 Lua 函数压到栈顶
		// 这里取-1,即是取这个编译好的代码块
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


	// 读取文件内容,再调用doBuffer
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

	// 把这个对象释放掉
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
		// 设置flag位
		ud->flag |= UD_HADFREE;
		// 移除cache
		// remove cache
		ensure(ud->ud == Object);
		LuaObject::removeFromCache(L, ud->ud);
	}

	// 把持有的对象加入这个收集器中
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
		 * 把 L 表示的线程压栈。 如果这个线程是当前状态机的主线程的话，返回 1
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
		 * 交换同一个状态机下不同线程中的值
		 * 这个函数会从 from 的栈上弹出 n 个值， 然后把它们压入 to 的栈上
		 */

		
		lua_xmove(thread, L, 1);
		lua_pop(thread, 1);

		/*
		 * int lua_isthread (lua_State *L, int index)
		 * 当给定索引的值是一条线程时，返回 1 ，否则返回 0
		 */

		 // 此时L栈顶为thread
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
			 * 返回线程 L 的状态
			 * 正常的线程状态是 0 （LUA_OK）
			 * 当线程用 lua_resume 执行完毕并抛出了一个错误时， 状态值是错误码
			 * 如果线程被挂起，状态为 LUA_YIELD
			 * 你只能在状态为 LUA_OK 的线程中调用函数
			 * 你可以延续一个状态为 LUA_OK 的线程 （用于开始新协程）
			 * 或是状态为 LUA_YIELD 的线程 （用于延续协程）
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
				 * 在给定线程中启动或延续一条协程
				 * 要启动一个协程的话， 你需要把主函数以及它需要的参数压入线程栈
				 * 然后调用 lua_resume ， 把 nargs 设为参数的个数
				 * 这次调用会在协程挂起时或是结束运行后返回
				 * 当函数返回时，堆栈中会有传给 lua_yield 的所有值， 或是主函数的所有返回值
				 * 当协程让出， lua_resume 返回 LUA_YIELD ， 若协程结束运行且没有任何错误时，返回 0
				 * 如果有错则返回错误代码
				 * 在发生错误的情况下,堆栈没有展开,因此你可以使用调试 API 来处理它
				 * 错误消息放在栈顶
				 * 要延续一个协程， 你需要清除上次 lua_yield 遗留下的所有结果
				 * 你把需要传给 yield 作结果的值压栈， 然后调用 lua_resume
				 * 参数 from 表示协程从哪个协程中来延续 L 的
				 * 如果不存在这样一个协程，这个参数可以是 NULL
				 */
				
				int status = lua_resume(thread, L, 0);
				if (status == LUA_OK || status == LUA_YIELD)
				{
					int nres = lua_gettop(thread);

					/*
					 * int lua_checkstack (lua_State *L, int n)
					 * 确保堆栈上至少有 n 个额外空位
					 * 如果不能把堆栈扩展到相应的尺寸，函数返回假
					 * 失败的原因包括将把栈扩展到比固定最大尺寸还大 （至少是几千个元素）或分配内存失败
					 * 这个函数永远不会缩小堆栈
					 * 如果堆栈已经比需要的大了，那么就保持原样
					 */
					if (!lua_checkstack(L, nres + 1))
					{
						lua_pop(thread, nres);  /* remove results anyway */
						Log::Error("too many results to resume");
						threadIsDead = true;
					}
					else
					{
						// 移动参数
						lua_xmove(thread, L, nres);  /* move yielded values */

						if (status == LUA_OK)
						{
							// 标志为已经关闭
							threadIsDead = true;
						}
					}
				}
				else
				{
					// 把错误移动到当前线程
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
		// 第一次的时候,a会从全局表里面取
        while(strSplit(path,".",&left,&right)) {
            if(lua_type(L,-1)!=LUA_TTABLE) break;
            lua_pushstring(L,TCHAR_TO_UTF8(*left));
            lua_gettable(L,-2);
            rt.set(L,-1);
        	// 把上一次的表移除,现在栈顶为读取出来的新表
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
					// a.b b为空,b加入全局
					lua_pushstring(L, TCHAR_TO_UTF8(*left));
					v.push(L);
					lua_rawset(L, -3);
					return true;
				}
				else {
					// a.b a不为空,b为空,先将a加入全局,再把b添加到a
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
				// a.b 如果a不为空,b不是一个表,错误
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
		// 先把上一个移除
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

	// 调用入口
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
		 * 返回当前的钩子函数
		 */
		
		auto hook = lua_gethook(L);
		// if debugger isn't exists
		if (hook == nullptr) {
			/*
			 * void lua_sethook (lua_State *L, lua_Hook f, int mask, int count)
			 * 设置一个调试用钩子函数
			 * 参数 f 是钩子函数
			 * mask 指定在哪些事件时会调用：它由下列一组位常量构成 LUA_MASKCALL， LUA_MASKRET， LUA_MASKLINE， LUA_MASKCOUNT
			 * 参数 count 只在掩码中包含有 LUA_MASKCOUNT 才有意义对于每个事件，钩子被调用的情况解释如下：
			 * call hook: 在解释器调用一个函数时被调用。 钩子将于 Lua 进入一个新函数后， 函数获取参数前被调用
			 * return hook: 在解释器从一个函数中返回时调用
			 *	钩子将于 Lua 离开函数之前的那一刻被调用
			 *	没有标准方法来访问被函数返回的那些值
			 * line hook: 在解释器准备开始执行新的一行代码时， 或是跳转到这行代码中时（即使在同一行内跳转）被调用
			 *	（这个事件仅仅在 Lua 执行一个 Lua 函数时发生）
			 * count hook: 在解释器每执行 count 条指令后被调用
			 *	（这个事件仅仅在 Lua 执行一个 Lua 函数时发生）
			 * 钩子可以通过设置 mask 为零屏蔽
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
