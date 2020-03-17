// Tencent is pleased to support the open source community by making sluaunreal available.

// Copyright (C) 2018 THL A29 Limited, a Tencent company. All rights reserved.
// Licensed under the BSD 3-Clause License (the "License"); 
// you may not use this file except in compliance with the License. You may obtain a copy of the License at

// https://opensource.org/licenses/BSD-3-Clause

// Unless required by applicable law or agreed to in writing, 
// software distributed under the License is distributed on an "AS IS" BASIS, 
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. 
// See the License for the specific language governing permissions and limitations under the License.

#ifdef _WIN32
#pragma warning (push)
#pragma warning (disable : 4018)
#endif

#include "LuaObject.h"
#include "LuaVar.h"
#include "LuaDelegate.h"
#include "LatentDelegate.h"
#include "UObject/StructOnScope.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"
#include "UObject/Stack.h"
#include "Blueprint/WidgetTree.h"
#include "LuaWidgetTree.h"
#include "LuaArray.h"
#include "LuaMap.h"
#include "Log.h"
#include "LuaState.h"
#include "LuaWrapper.h"
#include "SluaUtil.h"
#include "LuaReference.h"
#include "LuaBase.h"
#include "Engine/UserDefinedEnum.h"

namespace NS_SLUA { 
	static const FName NAME_LatentInfo = TEXT("LatentInfo");

	TMap<UClass*,LuaObject::PushPropertyFunction> pusherMap;
	TMap<UClass*,LuaObject::CheckPropertyFunction> checkerMap;

	// 拓展字段
	struct ExtensionField {
		bool isFunction = true;
		union {
			struct {
				lua_CFunction getter;
				lua_CFunction setter;
			};
			lua_CFunction func;
		};

		// 函数
		ExtensionField(lua_CFunction funcf) : isFunction(true), func(funcf) {
			ensure(funcf);
		}
		
		// 字段
		ExtensionField(lua_CFunction getterf, lua_CFunction setterf) 
			: isFunction(false)
			, getter(getterf)
			, setter(setterf) {}
	};
    
    TMap< UClass*, TMap<FString, ExtensionField> > extensionMMap;
    TMap< UClass*, TMap<FString, ExtensionField> > extensionMMap_static;

    namespace ExtensionMethod{
        void init();
    }

    DefTypeName(LuaStruct)

    // construct lua struct
	// LuaStruct构造函数
    LuaStruct::LuaStruct(uint8* b,uint32 s,UScriptStruct* u)
        :buf(b),size(s),uss(u) {
    }

	// LuaStruct析构函数
    LuaStruct::~LuaStruct() {
		if (buf) {
			uss->DestroyStruct(buf);
			FMemory::Free(buf);
			buf = nullptr;
		}
    }

	// 添加引用
	void LuaStruct::AddReferencedObjects(FReferenceCollector& Collector) {
		Collector.AddReferencedObject(uss);
		LuaReference::addRefByStruct(Collector, uss, buf);
	}

    void LuaObject::addExtensionMethod(UClass* cls,const char* n,lua_CFunction func,bool isStatic) {
		// 静态和非静态Map不是一个
    	if(isStatic) {
            auto& extmap = extensionMMap_static.FindOrAdd(cls);
            extmap.Add(n, ExtensionField(func));
        }
        else {
            auto& extmap = extensionMMap.FindOrAdd(cls);
            extmap.Add(n, ExtensionField(func));
        }
    }

	void LuaObject::addExtensionProperty(UClass * cls, const char * n, lua_CFunction getter, lua_CFunction setter, bool isStatic)
	{
		if (isStatic) {
			auto& extmap = extensionMMap_static.FindOrAdd(cls);
			extmap.Add(n, ExtensionField(getter,setter));
		}
		else {
			auto& extmap = extensionMMap.FindOrAdd(cls);
			extmap.Add(n, ExtensionField(getter, setter));
		}
	}

	// 获取name成员
    static int findMember(lua_State* L,const char* name) {
       int popn = 0;
    	// 元表
        if ((++popn, lua_getfield(L, -1, name) != 0)) {
			lua_remove(L, -2); // remove mt
            return 1;
        // get方法
        } else if ((++popn, lua_getfield(L, -2, ".get")) && (++popn, lua_getfield(L, -1, name))) {
            lua_pushvalue(L, 1);
            lua_call(L, 1, 1);
			lua_remove(L, -2); // remove .get
            return 1;
        // base
        } else {
            // find base
            lua_pop(L, popn);
            lua_getfield(L,-1, "__base");
			luaL_checktype(L, -1, LUA_TTABLE);
			// for each base
            {
                size_t cnt = lua_rawlen(L,-1);
                int r = 0;
                for(size_t n=0;n<cnt;n++) {
                    lua_geti(L,-1,n+1);
                    const char* tn = lua_tostring(L,-1);
                    lua_pop(L,1); // pop tn
                    luaL_getmetatable(L,tn);
					luaL_checktype(L, -1, LUA_TTABLE);
                	// 递归
					if (findMember(L, name)) return 1;
                }
                lua_remove(L,-2); // remove __base
                return r;
            }
        }
    }

	// 设置name成员
	static bool setMember(lua_State* L, const char* name) {
		int popn = 0;
    	// set方法
		if ((++popn, lua_getfield(L, -1, ".set")) && (++popn, lua_getfield(L, -1, name))) {
			// push ud
			lua_pushvalue(L, 1);
			// push value
			lua_pushvalue(L, 3);
			// call setter
			lua_call(L, 2, 0);
			lua_pop(L, 1); // pop .set
			return true;
		}
    	// base
		else {
			// find base
			lua_pop(L, popn);
			lua_getfield(L, -1, "__base");
			luaL_checktype(L, -1, LUA_TTABLE);
			// for each base
			{
				size_t cnt = lua_rawlen(L, -1);
				for (size_t n = 0; n < cnt; n++) {
					lua_geti(L, -1, n + 1);
					const char* tn = lua_tostring(L, -1);
					lua_pop(L, 1); // pop tn
					luaL_getmetatable(L, tn);
					luaL_checktype(L, -1, LUA_TTABLE);
					// 递归
					if (setMember(L, name)) return true;
				}
			}
			// pop __base
			lua_pop(L, 1);
			return false;
		}
	}

	int LuaObject::classIndex(lua_State* L) {
		lua_getmetatable(L, 1);
		const char* name = checkValue<const char*>(L, 2);
		if (!findMember(L, name))
			luaL_error(L, "can't get %s", name);
		lua_remove(L, -2);
		return 1;
	}

	int LuaObject::classNewindex(lua_State* L) {
		lua_getmetatable(L, 1);
		const char* name = checkValue<const char*>(L, 2);
		if(!setMember(L, name))
			luaL_error(L, "can't set %s", name);
		lua_pop(L, 1);
		return 0;
	}

	// 设置元方法
	static void setMetaMethods(lua_State* L) {
		lua_newtable(L);
		lua_pushvalue(L, -1);
		lua_setfield(L, -3, ".get"); // upvalue
		lua_pushcclosure(L, LuaObject::classIndex, 1);
		lua_setfield(L, -2, "__index");
		lua_newtable(L);
		lua_pushvalue(L, -1);
		lua_setfield(L, -3, ".set"); // upvalue
		lua_pushcclosure(L, LuaObject::classNewindex, 1);
		lua_setfield(L, -2, "__newindex");
	}

	void LuaObject::newType(lua_State* L, const char* tn) {
		lua_pushglobaltable(L);				    // _G
		lua_newtable(L);							// local t = {}
		lua_pushvalue(L, -1);
		lua_setfield(L, -3, tn);					// _G[tn] = t
		lua_remove(L, -2);						// remove global table;

        lua_newtable(L);
		lua_pushvalue(L, -1);
		lua_setmetatable(L, -3);					// setmetatable(t, mt)
		setMetaMethods(L);

    	/*
    	 * int luaL_newmetatable (lua_State *L, const char *tname)
    	 * 如果注册表中已存在键 tname，返回 0
    	 * 否则， 为用户数据的元表创建一张新表
    	 * 向这张表加入 __name = tname 键值对， 并将 [tname] = new table 添加到注册表中， 返回 1
    	 * （__name项可用于一些错误输出函数。）
    	 * 这两种情况都会把最终的注册表中关联 tname 的值压栈。
    	 */
    	
		luaL_newmetatable(L, tn);
		setMetaMethods(L);
	}

    void LuaObject::newTypeWithBase(lua_State* L, const char* tn, std::initializer_list<const char*> bases) {
		newType(L,tn);

        // create base table
        lua_newtable(L);
        lua_pushvalue(L,-1);
        lua_setfield(L,-3,"__base");

    	// 多个base
        for(auto base:bases) {
            if(strlen(base)>0) {
                lua_pushstring(L,base);
                size_t p = lua_rawlen(L,-2);
                lua_seti(L,-2,p+1);
            }
        }
        // pop __base table
        lua_pop(L,1);
	}

	// push LuaLString
	int LuaObject::push(lua_State * L, const LuaLString& lstr)
	{
		lua_pushlstring(L, lstr.buf, lstr.len);
		return 1;
	}

	bool LuaObject::isBaseTypeOf(lua_State* L,const char* tn,const char* base) {
        AutoStack as(L);
        int t = luaL_getmetatable(L,tn);
        if(t!=LUA_TTABLE)
            return false;

        if(lua_getfield(L,-1,"__base")==LUA_TTABLE) {
            size_t len = lua_rawlen(L,-1);
            for(int n=0;n<len;n++) {
                if(lua_geti(L,-1,n+1)==LUA_TSTRING) {
                    const char* maybeBase = lua_tostring(L,-1);
                    if(strcmp(maybeBase,base)==0) return true;
                    else return isBaseTypeOf(L,maybeBase,base);
                }
            }
            return false;
        }
        return false;
    }

	void LuaObject::addMethod(lua_State* L, const char* name, lua_CFunction func, bool isInstance) {
		lua_pushcfunction(L, func);
		lua_setfield(L, isInstance ? -2 : -3, name);
	}

    void LuaObject::addGlobalMethod(lua_State* L, const char* name, lua_CFunction func) {
		lua_pushcfunction(L, func);
        lua_setglobal(L,name);
	}

	void LuaObject::addField(lua_State* L, const char* name, lua_CFunction getter, lua_CFunction setter, bool isInstance) {
		lua_getfield(L, isInstance ? -1 : -2, ".get");
		lua_pushcfunction(L, getter);
		lua_setfield(L, -2, name);
		lua_pop(L, 1);
		lua_getfield(L, isInstance ? -1 : -2, ".set");
		lua_pushcfunction(L, setter);
		lua_setfield(L, -2, name);
		lua_pop(L, 1);
	}

	void LuaObject::addOperator(lua_State* L, const char* name, lua_CFunction func) {
		lua_pushcfunction(L, func);
		lua_setfield(L, -2, name);
	}

	void LuaObject::finishType(lua_State* L, const char* tn, lua_CFunction ctor, lua_CFunction gc, lua_CFunction strHint) {
		// ()
    	if(ctor) {
		    lua_pushcclosure(L, ctor, 0);
		    lua_setfield(L, -3, "__call");
        }
        if(gc) {
		    lua_pushcclosure(L, gc, 0); // t, mt, _instance, __gc
    		lua_setfield(L, -2, "__gc"); // t, mt, _instance
        }
        if(strHint) {
            lua_pushcfunction(L, strHint);
            lua_setfield(L, -2, "__tostring");
        }
        lua_pop(L,3);
	}

	bool LuaObject::matchType(lua_State* L, int p, const char* tn, bool noprefix) {
		AutoStack autoStack(L);
		if (!lua_isuserdata(L, p)) {
			return false;
		}
		lua_getmetatable(L, p);
		if (lua_isnil(L, -1)) {
			return false;
		}
		lua_getfield(L, -1, "__name");
		if (lua_isnil(L, -1)) {
			return false;
		}
		auto name = luaL_checkstring(L, -1);
		// skip first prefix "F" or "U" or "A"
		if(noprefix) return strcmp(name+1, tn) == 0;
		else return strcmp(name,tn)==0;
	}

    LuaObject::PushPropertyFunction LuaObject::getPusher(UClass* cls) {
        auto it = pusherMap.Find(cls);
        if(it!=nullptr)
            return *it;
        return nullptr;
    }

    LuaObject::CheckPropertyFunction LuaObject::getChecker(UClass* cls) {
        auto it = checkerMap.Find(cls);
        if(it!=nullptr)
            return *it;
        return nullptr;
    }

    LuaObject::PushPropertyFunction LuaObject::getPusher(UProperty* prop) {
        return getPusher(prop->GetClass());
    }

    LuaObject::CheckPropertyFunction LuaObject::getChecker(UProperty* prop) {
        return getChecker(prop->GetClass());        
    }

    

	// 注册Push方法
    void regPusher(UClass* cls,LuaObject::PushPropertyFunction func) {
		pusherMap.Add(cls, func);
    }

	// 注册Check方法
    void regChecker(UClass* cls,LuaObject::CheckPropertyFunction func) {
		checkerMap.Add(cls, func);
    }

	// class构造方法
    int classConstruct(lua_State* L) {
        UClass* cls = LuaObject::checkValue<UClass*>(L, 1);
		UObject* outter = LuaObject::checkValueOpt<UObject*>(L, 2, (UObject*)GetTransientPackage());
		if (cls && !outter->IsA(cls->ClassWithin)) {
			luaL_error(L, "Can't create object in %s", TCHAR_TO_UTF8(*outter->GetClass()->GetName()));
		}
		FName name = LuaObject::checkValueOpt<FName>(L, 3, FName(NAME_None));
        if(cls) {
        	// NewObject
            UObject* obj = NewObject<UObject>(outter,cls,name);
            if(obj) {
                LuaObject::push(L,obj);
                return 1;
            }
        }
        return 0;
    }

	// 查询拓展方法 UClass
    int searchExtensionMethod(lua_State* L,UClass* cls,const char* name,bool isStatic=false) {

        // search class and its super
        TMap<FString,ExtensionField>* mapptr=nullptr;
        while(cls!=nullptr) {
            mapptr = isStatic?extensionMMap_static.Find(cls):extensionMMap.Find(cls);
            if(mapptr!=nullptr) {
                // find field
                auto fieldptr = mapptr->Find(name);
				if (fieldptr != nullptr) {
					// is function
					if (fieldptr->isFunction) {
						lua_pushcfunction(L, fieldptr->func);
						return 1;
					} 
					// is property
					else {
						if (!fieldptr->getter) luaL_error(L, "Property %s is set only", name);
						lua_pushcfunction(L, fieldptr->getter);
						if (!isStatic) {
							lua_pushvalue(L, 1); // push self
							// 第二个参数为参数个数
							// : 实例方法需要把self传进去
							lua_call(L, 1, 1);
						} else 
							lua_call(L, 0, 1);
						return 1;
					}
				}
            }
        	// 找不到就从基类找
            cls=cls->GetSuperClass();
        }   
        return 0;
    }

	// 查询拓展方法 UObject
    int searchExtensionMethod(lua_State* L,UObject* o,const char* name,bool isStatic=false) {
        auto cls = o->GetClass();
        return searchExtensionMethod(L,cls,name,isStatic);
    }

    int classIndex(lua_State* L) {
        UClass* cls = LuaObject::checkValue<UClass*>(L, 1);
        const char* name = LuaObject::checkValue<const char*>(L, 2);
        // get blueprint member
    	// 先去蓝图里面查找
        UFunction* func = cls->FindFunctionByName(UTF8_TO_TCHAR(name));
        if(func) return LuaObject::push(L,func,cls);
        return searchExtensionMethod(L,cls,name,true);
    }

	// 结构体构造
    int structConstruct(lua_State* L) {
        UScriptStruct* uss = LuaObject::checkValue<UScriptStruct*>(L, 1);
        if(uss) {
        	// MinAlignment为1
            uint32 size = uss->GetStructureSize() ? uss->GetStructureSize() : 1;
            
            uint8* buf = (uint8*)FMemory::Malloc(size);
            uss->InitializeStruct(buf);
            LuaStruct* ls=new LuaStruct(buf,size,uss);
            LuaObject::push(L,ls);
            return 1;
        }
        return 0;
    }

	// 从State中获取参数
    int fillParamFromState(lua_State* L,UProperty* prop,uint8* params,int i) {

        // if is out param, can accept nil
    	// out参数可以为nil
        uint64 propflag = prop->GetPropertyFlags();
        if(propflag&CPF_OutParm && lua_isnil(L,i))
            return prop->GetSize();

        auto checker = LuaObject::getChecker(prop);
        if(checker) {
            checker(L,prop,params,i);
            return prop->GetSize();
        }
        else {
            FString tn = prop->GetClass()->GetName();
            luaL_error(L,"unsupport param type %s at %d",TCHAR_TO_UTF8(*tn),i);
            return 0;
        }
        
    }

    void LuaObject::fillParam(lua_State* L,int i,UFunction* func,uint8* params) {
		auto funcFlag = func->FunctionFlags;
        for(TFieldIterator<UProperty> it(func);it && (it->PropertyFlags&CPF_Parm);++it) {
            UProperty* prop = *it;
            uint64 propflag = prop->GetPropertyFlags();
			if (funcFlag & EFunctionFlags::FUNC_Native) {
				// 返回值
				if ((propflag&CPF_ReturnParm))
					continue;
			}
			else if (IsRealOutParam(propflag))
				continue;

			if (prop->GetFName() == NAME_LatentInfo) {
				// bind a callback to the latent function
				// 主线程
				lua_State *mainThread = G(L)->mainthread;

				ULatentDelegate *obj = LuaObject::getLatentDelegate(mainThread);
				int threadRef = obj->getThreadRef(L);
				FLatentActionInfo LatentActionInfo(threadRef, GetTypeHash(FGuid::NewGuid()), *ULatentDelegate::NAME_LatentCallback, obj);

				prop->CopySingleValue(prop->ContainerPtrToValuePtr<void>(params), &LatentActionInfo);
			}
			else {
				fillParamFromState(L, prop, params + prop->GetOffset_ForInternal(), i);
				i++;
			}
        }
    }

	void LuaObject::callRpc(lua_State* L, UObject* obj, UFunction* func, uint8* params) {
		// call rpc without outparams
    	// 有没有返回值
		const bool bHasReturnParam = func->ReturnValueOffset != MAX_uint16;
		uint8* ReturnValueAddress = bHasReturnParam ? ((uint8*)params + func->ReturnValueOffset) : nullptr;
		FFrame NewStack(obj, func, params, NULL, func->Children);
		NewStack.OutParms = nullptr;
		func->Invoke(obj, NewStack, ReturnValueAddress);
	}

	void LuaObject::callUFunction(lua_State* L, UObject* obj, UFunction* func, uint8* params) {
		auto ff = func->FunctionFlags;
		// it's an RPC function
    	// RPC函数
		if (ff&FUNC_Net)
			LuaObject::callRpc(L, obj, func, params);
		else
		// it's a local function
			// 客户端函数
			obj->ProcessEvent(func, params);
	}

    // handle return value and out params
    int LuaObject::returnValue(lua_State* L,UFunction* func,uint8* params) {

        // check is function has return value
		const bool bHasReturnParam = func->ReturnValueOffset != MAX_uint16;

		// put return value as head
        int ret = 0;
        if(bHasReturnParam) {
            UProperty* p = func->GetReturnProperty();
            ret += LuaObject::push(L,p,params+p->GetOffset_ForInternal());
        }

		bool isLatentFunction = false;
        // push out parms
        for(TFieldIterator<UProperty> it(func);it;++it) {
            UProperty* p = *it;
            uint64 propflag = p->GetPropertyFlags();
            // skip return param
        	// 返回值参数,上面已经处理了
            if(propflag&CPF_ReturnParm)
                continue;

			if (p->GetFName() == NAME_LatentInfo) {
				isLatentFunction = true;
			}
            else if(IsRealOutParam(propflag)) // out params should be not const and not readonly
                ret += LuaObject::push(L,p,params+p->GetOffset_ForInternal());
        }
        
		if (isLatentFunction) {
			return lua_yield(L, ret);
		}
		else {
			return ret;
		}
    }

	// UFunction闭包
    int ufuncClosure(lua_State* L) {
        lua_pushvalue(L,lua_upvalueindex(1));
        void* ud = lua_touserdata(L, -1);
        lua_pop(L, 1); // pop ud of func
        
        if(!ud) luaL_error(L, "Call ufunction error");

        lua_pushvalue(L,lua_upvalueindex(2));
        UClass* cls = reinterpret_cast<UClass*>(lua_touserdata(L, -1));
        lua_pop(L,1); // pop staticfunc flag
        
        UObject* obj;
        int offset=1;
        // use ClassDefaultObject if is static function call
    	// 静态方法使用CDO
        if(cls) obj = cls->ClassDefaultObject;
        // use obj instance if is member function call
        // 实例方法
        // and offset set 2 to skip self
        // 偏移设置成2,跳过self
        else {
            obj = LuaObject::checkValue<UObject*>(L, 1);
            offset++;
        }
        
        UFunction* func = reinterpret_cast<UFunction*>(ud);
        
		FStructOnScope params(func);
		LuaObject::fillParam(L, offset, func, params.GetStructMemory());
		{
			LuaObject::callUFunction(L, obj, func, params.GetStructMemory());
		}
		// return value to push lua stack
    	// 返回值压栈
		return LuaObject::returnValue(L, func, params.GetStructMemory());
    }

    // find ufunction from cache
    UFunction* LuaObject::findCacheFunction(lua_State* L, UClass* cls,const char* fname) {
        auto state = LuaState::get(L);
		return state->classMap.findFunc(cls, fname);
    }

    // cache ufunction for reuse
    void LuaObject::cacheFunction(lua_State* L,UClass* cls,const char* fname,UFunction* func) {
        auto state = LuaState::get(L);
        state->classMap.cacheFunc(cls,fname,func);
    }

    UProperty* LuaObject::findCacheProperty(lua_State* L, UClass* cls, const char* pname)
    {
		auto state = LuaState::get(L);
		return state->classMap.findProp(cls, pname);
    }

    void LuaObject::cacheProperty(lua_State* L, UClass* cls, const char* pname, UProperty* property)
    {
		auto state = LuaState::get(L);
		state->classMap.cacheProp(cls, pname, property);
    }

	// cache class property's
	// 缓存class的属性
	void cachePropertys(lua_State* L, UClass* cls) {
		auto PropertyLink = cls->PropertyLink;
		for (UProperty* Property = PropertyLink; Property != NULL; Property = Property->PropertyLinkNext) {
			LuaObject::cacheProperty(L, cls, TCHAR_TO_UTF8(*(Property->GetName())), Property);
		}
	}

	// 实例的__index
    int instanceIndex(lua_State* L) {
        UObject* obj = LuaObject::checkValue<UObject*>(L, 1);
        const char* name = LuaObject::checkValue<const char*>(L, 2);

		UClass* cls = obj->GetClass();
    	// UProperty
        UProperty* up = LuaObject::findCacheProperty(L, cls, name);
        if (up)
        {
            return LuaObject::push(L, up, obj, false);
        }

    	// UFunction
        UFunction* func = LuaObject::findCacheFunction(L, cls, name);
        if (func)
        {
            return LuaObject::push(L, func);
        }

        // get blueprint member
    	// 蓝图方法
		FName wname(UTF8_TO_TCHAR(name));
        func = cls->FindFunctionByName(wname);
        if(!func) {
			cachePropertys(L, cls);

			up = LuaObject::findCacheProperty(L, cls, name);
            if (up) {
                return LuaObject::push(L, up, obj, false);
            }
            
            // search extension method
            return searchExtensionMethod(L, obj, name);
        }
        else {
			LuaObject::cacheFunction(L, cls, name, func);
            return LuaObject::push(L,func);
        }
    }

	// 实例的__newindex
    int newinstanceIndex(lua_State* L) {
        UObject* obj = LuaObject::checkValue<UObject*>(L, 1);
        const char* name = LuaObject::checkValue<const char*>(L, 2);
        UClass* cls = obj->GetClass();
    	// UProperty
		UProperty* up = LuaObject::findCacheProperty(L, cls, name);
		if (!up)
		{
			// 没有的话先加入cache
			cachePropertys(L, cls);
			up = LuaObject::findCacheProperty(L, cls, name);
		}
		if (!up) luaL_error(L, "Property %s not found", name);
    	// 蓝图可读
        if(up->GetPropertyFlags() & CPF_BlueprintReadOnly)
            luaL_error(L,"Property %s is readonly",name);

        auto checker = LuaObject::getChecker(up);
        if(!checker) luaL_error(L,"Property %s type is not support",name);
        // set property value
        checker(L,up,up->ContainerPtrToValuePtr<uint8>(obj),3);
        return 0;
    }

	// 通过Name查找Struct中的属性
	UProperty* FindStructPropertyByName(UScriptStruct* scriptStruct, const char* name)
	{
    	// 原生的就直接去取
		if (scriptStruct->IsNative())
		{
			return scriptStruct->FindPropertyByName(UTF8_TO_TCHAR(name));
		}

		FString propName = UTF8_TO_TCHAR(name);
    	// 遍历
		for (UProperty* Property = scriptStruct->PropertyLink; Property != nullptr; Property = Property->PropertyLinkNext)
		{
			FString fieldName = Property->GetName();
			// 大小写敏感
			if (fieldName.StartsWith(propName, ESearchCase::CaseSensitive))
			{
				int index = fieldName.Len();
				for (int i = 0; i < 2; ++i)
				{
					int findIndex = fieldName.Find(TEXT("_"), ESearchCase::CaseSensitive, ESearchDir::FromEnd, index);
					if (findIndex != INDEX_NONE)
					{
						index = findIndex;
					}
				}
				if (propName.Len() == index)
				{
					return Property;
				}
			}
		}

		return nullptr;
	}

	// StructInstance的__index
    int instanceStructIndex(lua_State* L) {
        LuaStruct* ls = LuaObject::checkValue<LuaStruct*>(L, 1);
        const char* name = LuaObject::checkValue<const char*>(L, 2);
        
        auto* cls = ls->uss;
        UProperty* up = FindStructPropertyByName(cls, name);
        if(!up) return 0;
        return LuaObject::push(L,up,ls->buf+up->GetOffset_ForInternal(),false);
    }

	// StructInstance的__newindex
    int newinstanceStructIndex(lua_State* L) {
        LuaStruct* ls = LuaObject::checkValue<LuaStruct*>(L, 1);
        const char* name = LuaObject::checkValue<const char*>(L, 2);

        auto* cls = ls->uss;
        UProperty* up = FindStructPropertyByName(cls, name);
        if (!up) luaL_error(L, "Can't find property named %s", name);
    	// 蓝图只读
        if (up->GetPropertyFlags() & CPF_BlueprintReadOnly)
            luaL_error(L, "Property %s is readonly", name);

        auto checker = LuaObject::getChecker(up);
        if(!checker) luaL_error(L,"Property %s type is not support",name);

        checker(L, up, ls->buf + up->GetOffset_ForInternal(), 3);
        return 0;
    }

	// Instance的self的__index
    int instanceIndexSelf(lua_State* L) {
        lua_getmetatable(L,1);
        const char* name = LuaObject::checkValue<const char*>(L, 2);
        
        lua_getfield(L,-1,name);
        lua_remove(L,-2); // remove mt of ud
        return 1;
    }

	// object的__tostring
	int LuaObject::objectToString(lua_State* L)
	{
        const int BufMax = 128;
        static char buffer[BufMax] = { 0 };
		UObject* obj = LuaObject::testudata<UObject>(L, 1);
    	// UObject
		if (obj) {
			auto clsname = obj->GetClass()->GetFName().ToString();
			auto objname = obj->GetFName().ToString();
			snprintf(buffer, BufMax, "%s: %s %p",TCHAR_TO_UTF8(*clsname),TCHAR_TO_UTF8(*objname), obj);
		}
    	// __name
        else {
            // if ud isn't a uobject, get __name of metatable to cast it to string
            const void* ptr = lua_topointer(L,1);
            luaL_getmetafield(L,1,"__name");
            // should have __name field
            if(lua_type(L,-1)==LUA_TSTRING) {
                const char* metaname = lua_tostring(L,-1);
                snprintf(buffer, BufMax, "%s: %p", metaname,ptr);
            }
            lua_pop(L,1);
        }

		lua_pushstring(L, buffer);
		return 1;
	}

	void LuaObject::setupMetaTable(lua_State* L, const char* tn, lua_CFunction setupmt, lua_CFunction gc)
	{
		if (luaL_newmetatable(L, tn)) {
			if (setupmt)
				setupmt(L);
			if (gc) {
				lua_pushcfunction(L, gc);
				lua_setfield(L, -2, "__gc");
			}
		}
		lua_setmetatable(L, -2);
	}

	void LuaObject::setupMetaTable(lua_State* L, const char* tn, lua_CFunction setupmt, int gc)
	{
		if (luaL_newmetatable(L, tn)) {
			if (setupmt)
				setupmt(L);
			if (gc) {
				lua_pushvalue(L, gc);
				lua_setfield(L, -2, "__gc");
			}
		}
		lua_setmetatable(L, -2);
	}

	void LuaObject::setupMetaTable(lua_State* L, const char* tn, lua_CFunction gc)
	{
		luaL_getmetatable(L, tn);
		if (lua_isnil(L, -1))
			luaL_error(L, "Can't find type %s exported", tn);

		lua_pushcfunction(L, gc);
		lua_setfield(L, -2, "__gc");
		lua_setmetatable(L, -2);
	}

	// UProperty
    template<typename T>
    int pushUProperty(lua_State* L,UProperty* prop,uint8* parms,bool ref) {
        auto p=Cast<T>(prop);
        ensure(p);
        return LuaObject::push(L,p->GetPropertyValue(parms));
    }

	// Enum
	int pushEnumProperty(lua_State* L, UProperty* prop, uint8* parms,bool ref) {
		auto p = Cast<UEnumProperty>(prop);
		ensure(p);
		auto p2 = p->GetUnderlyingProperty();
		ensure(p2);
		int i = p2->GetSignedIntPropertyValue(parms);
		return LuaObject::push(L, i);
	}

	// UArray
    int pushUArrayProperty(lua_State* L,UProperty* prop,uint8* parms,bool ref) {
        auto p = Cast<UArrayProperty>(prop);
        ensure(p);
        FScriptArray* v = p->GetPropertyValuePtr(parms);
		return LuaArray::push(L, p->Inner, v);
    }

	// UMap
    int pushUMapProperty(lua_State* L,UProperty* prop,uint8* parms,bool ref) {
        auto p = Cast<UMapProperty>(prop);
        ensure(p);
		FScriptMap* v = p->GetPropertyValuePtr(parms);
		return LuaMap::push(L, p->KeyProp, p->ValueProp, v);
    }

	// UWeakObject
	int pushUWeakProperty(lua_State* L, UProperty* prop, uint8* parms,bool ref) {
		auto p = Cast<UWeakObjectProperty>(prop);
		ensure(p);
		FWeakObjectPtr v = p->GetPropertyValue(parms);
		return LuaObject::push(L, v);
	}

	// UArray index
    int checkUArrayProperty(lua_State* L,UProperty* prop,uint8* parms,int i) {
        auto p = Cast<UArrayProperty>(prop);
        ensure(p);
        CheckUD(LuaArray,L,i);
        LuaArray::clone((FScriptArray*)parms,p->Inner,UD->get());
        return 0;
    }

	// UMap index
	int checkUMapProperty(lua_State* L, UProperty* prop, uint8* parms, int i) {
		auto p = Cast<UMapProperty>(prop);
		ensure(p);
		CheckUD(LuaMap, L, i);
        LuaMap::clone((FScriptMap*)parms,p->KeyProp,p->ValueProp,UD->get());
		return 0;
	}

	// UStruct
    int pushUStructProperty(lua_State* L,UProperty* prop,uint8* parms,bool ref) {
        auto p = Cast<UStructProperty>(prop);
        ensure(p);
        auto uss = p->Struct;

    	// 先从缓存里面找
		if (LuaWrapper::pushValue(L, p, uss, parms))
			return 1;

    	// 蓝图变量
		if (uss->GetName() == "LuaBPVar") {
			((FLuaBPVar*)parms)->value.push(L);
			return 1;
		}

    	// 新建一个
		uint32 size = uss->GetStructureSize() ? uss->GetStructureSize() : 1;
		uint8* buf = (uint8*)FMemory::Malloc(size);
		uss->InitializeStruct(buf);
		uss->CopyScriptStruct(buf, parms);
		return LuaObject::push(L, new LuaStruct(buf,size,uss));
    }  

	// UDelegate
	int pushUDelegateProperty(lua_State* L, UProperty* prop, uint8* parms, bool ref) {
		auto p = Cast<UDelegateProperty>(prop);
		ensure(p);
		FScriptDelegate* delegate = p->GetPropertyValuePtr(parms);
		return LuaDelegate::push(L, delegate, p->SignatureFunction, prop->GetNameCPP());
	}

	// UMulticastDelegate
    int pushUMulticastDelegateProperty(lua_State* L,UProperty* prop,uint8* parms,bool ref) {
        auto p = Cast<UMulticastDelegateProperty>(prop);
        ensure(p);
#if (ENGINE_MINOR_VERSION>=23) && (ENGINE_MAJOR_VERSION>=4)
		FMulticastScriptDelegate* delegate = const_cast<FMulticastScriptDelegate*>(p->GetMulticastDelegate(parms));
#else
        FMulticastScriptDelegate* delegate = p->GetPropertyValuePtr(parms);
#endif
		return LuaMultiDelegate::push(L, delegate, p->SignatureFunction, prop->GetNameCPP());
    }

	// UMulticastInlineDelegate
#if (ENGINE_MINOR_VERSION>=23) && (ENGINE_MAJOR_VERSION>=4)
	int pushUMulticastInlineDelegateProperty(lua_State* L, UProperty* prop, uint8* parms, bool ref) {
		auto p = Cast<UMulticastInlineDelegateProperty>(prop);
		ensure(p);
		FMulticastScriptDelegate* delegate = const_cast<FMulticastScriptDelegate*>(p->GetMulticastDelegate(parms));
		return LuaMultiDelegate::push(L, delegate, p->SignatureFunction, prop->GetNameCPP());
	}
#endif

    int checkUDelegateProperty(lua_State* L,UProperty* prop,uint8* parms,int i) {
        auto p = Cast<UDelegateProperty>(prop);
        ensure(p);
        CheckUD(UObject,L,i);
        // bind SignatureFunction
        if(auto dobj=Cast<ULuaDelegate>(UD)) dobj->bindFunction(p->SignatureFunction);
        else luaL_error(L,"arg 1 expect an UDelegateObject");

        FScriptDelegate d;
        d.BindUFunction(UD, TEXT("EventTrigger"));

        p->SetPropertyValue(parms,d);
        return 0;
    }

	// UObject
    int pushUObjectProperty(lua_State* L,UProperty* prop,uint8* parms,bool ref) {
        auto p = Cast<UObjectProperty>(prop);
        ensure(p);   
        UObject* o = p->GetPropertyValue(parms);
    	// UI
        if(auto tr=Cast<UWidgetTree>(o))
            return LuaWidgetTree::push(L,tr);
        else
            return LuaObject::push(L,o,false,ref);
    }

    template<typename T>
    int checkUProperty(lua_State* L,UProperty* prop,uint8* parms,int i) {
        auto p = Cast<T>(prop);
        ensure(p);
        p->SetPropertyValue(parms,LuaObject::checkValue<typename T::TCppType>(L,i));
        return 0;
    }

	// UEnum
    template<>
	int checkUProperty<UEnumProperty>(lua_State* L, UProperty* prop, uint8* parms, int i) {
		auto p = Cast<UEnumProperty>(prop);
		ensure(p);
		auto v = (int64)LuaObject::checkValue<int>(L, i);
		p->CopyCompleteValue(parms, &v);
		return 0;
	}

	// UObject
	template<>
	int checkUProperty<UObjectProperty>(lua_State* L, UProperty* prop, uint8* parms, int i) {
		auto p = Cast<UObjectProperty>(prop);
		ensure(p);
		UObject* arg = LuaObject::checkValue<UObject*>(L, i);
		if (arg && arg->GetClass() != p->PropertyClass && !arg->GetClass()->IsChildOf(p->PropertyClass))
			luaL_error(L, "arg %d expect %s, but got %s", i,
				p->PropertyClass ? TCHAR_TO_UTF8(*p->PropertyClass->GetName()) : "", 
				arg->GetClass() ? TCHAR_TO_UTF8(*arg->GetClass()->GetName()) : "");

		p->SetPropertyValue(parms, arg);
		return LuaObject::push(L, arg);
	}

	// UStruct
    int checkUStructProperty(lua_State* L,UProperty* prop,uint8* parms,int i) {
        auto p = Cast<UStructProperty>(prop);
        ensure(p);
        auto uss = p->Struct;

		// if it's LuaBPVar
    	// 蓝图变量
		if (uss->GetName() == "LuaBPVar")
			return FLuaBPVar::checkValue(L, p, parms, i);

		// skip first char to match type
    	// 跳过第一个字母
		if (LuaObject::matchType(L, i, TCHAR_TO_UTF8(*uss->GetName()),true)) {
			if (LuaWrapper::checkValue(L, p, uss, parms, i))
				return 0;
		}

		LuaStruct* ls = LuaObject::checkValue<LuaStruct*>(L, i);
		if(!ls)
			luaL_error(L, "expect struct but got nil");

		if (p->GetSize() != ls->size)
			luaL_error(L, "expect struct size == %d, but got %d", p->GetSize(), ls->size);
		p->CopyCompleteValue(parms, ls->buf);
		return 0;
    }

	// UClass
	int pushUClassProperty(lua_State* L, UProperty* prop, uint8* parms, bool ref) {
		auto p = Cast<UClassProperty>(prop);
		ensure(p);
		UClass* cls = Cast<UClass>(p->GetPropertyValue(parms));
		return LuaObject::pushClass(L, cls);
	}

	// UClass
	int checkUClassProperty(lua_State* L, UProperty* prop, uint8* parms, int i) {
		auto p = Cast<UClassProperty>(prop);
		ensure(p);
		p->SetPropertyValue(parms, LuaObject::checkValue<UClass*>(L, i));
		return 0;
	}

	// check UD match name
	bool checkType(lua_State* L, int p, const char* tn) {
		if (!lua_isuserdata(L, p))
			return false;
		luaL_getmetafield(L, p, "__name");
		if (lua_isstring(L, -1) && strcmp(tn, lua_tostring(L, -1)) == 0)
		{
			lua_pop(L, 1);
			return true;
		}
		lua_pop(L, 1);
		return false;
	}

    // search obj from registry, push cached obj and return true if find it
    bool LuaObject::getFromCache(lua_State* L,void* obj,const char* tn,bool check) {
        LuaState* ls = LuaState::get(L);
        ensure(ls->cacheObjRef!=LUA_NOREF);
        lua_geti(L,LUA_REGISTRYINDEX,ls->cacheObjRef);
        // should be a table
        ensure(lua_type(L,-1)==LUA_TTABLE);
        // push obj as key
        lua_pushlightuserdata(L,obj);
        // get key from table
        lua_rawget(L,-2);
        lua_remove(L,-2); // remove cache table
        
        if(lua_isnil(L,-1)) {
            lua_pop(L,1);
			return false;
        }
		if (!check)
			return true;
		// check type of ud matched
		return checkType(L, -1, tn);
    }

    void LuaObject::addRef(lua_State* L,UObject* obj,void* ud,bool ref) {
        auto sl = LuaState::get(L);
        sl->addRef(obj,ud,ref);
    }


    void LuaObject::removeRef(lua_State* L,UObject* obj) {
        auto sl = LuaState::get(L);
        sl->unlinkUObject(obj);
    }

	void LuaObject::releaseLink(lua_State* L, void* prop) {
		LuaState* ls = LuaState::get(L);
		ls->releaseLink(prop);
	}

	void LuaObject::linkProp(lua_State* L, void* parent, void* prop) {
		LuaState* ls = LuaState::get(L);
		ls->linkProp(parent,prop);
	}

    void LuaObject::cacheObj(lua_State* L,void* obj) {
        LuaState* ls = LuaState::get(L);
        lua_geti(L,LUA_REGISTRYINDEX,ls->cacheObjRef);
        lua_pushlightuserdata(L,obj);
        lua_pushvalue(L,-3); // obj userdata
        lua_rawset(L,-3);
        lua_pop(L,1); // pop cache table        
    }

	void LuaObject::removeFromCache(lua_State * L, void* obj)
	{
		// get cache table
		LuaState* ls = LuaState::get(L);
		lua_geti(L, LUA_REGISTRYINDEX, ls->cacheObjRef);
		ensure(lua_type(L, -1) == LUA_TTABLE);
		lua_pushlightuserdata(L, obj);
		lua_pushnil(L);
		// cache[obj] = nil
		lua_rawset(L, -3);
		// pop cache table;
		lua_pop(L, 1);
	}

	void LuaObject::deleteFGCObject(lua_State* L, FGCObject * obj)
	{
		auto ls = LuaState::get(L);
		ensure(ls);
		ls->deferDelete.Add(obj);
	}
	
	ULatentDelegate* LuaObject::getLatentDelegate(lua_State* L)
	{
		LuaState* ls = LuaState::get(L);
		return ls->getLatentDelegate();
	}

	void LuaObject::createTable(lua_State* L, const char * tn)
	{
		auto ls = LuaState::get(L);
		ensure(ls);
		LuaVar t = ls->createTable(tn);
		t.push(L);
	}


    int LuaObject::pushClass(lua_State* L,UClass* cls) {
        if(!cls) {
            lua_pushnil(L);
            return 1;
        }
		return pushGCObject<UClass*>(L, cls, "UClass", setupClassMT, gcClass, true);
    }

    int LuaObject::pushStruct(lua_State* L,UScriptStruct* cls) {
        if(!cls) {
            lua_pushnil(L);
            return 1;
        }    
        return pushGCObject<UScriptStruct*>(L,cls,"UScriptStruct",setupStructMT,gcStructClass,true);
    }

	int LuaObject::pushEnum(lua_State * L, UEnum * e)
	{
    	// 是不是蓝图枚举
		bool isbpEnum = Cast<UUserDefinedEnum>(e) != nullptr;
		// return a enum as table
		lua_newtable(L);
		int num = e->NumEnums();
		for (int i = 0; i < num; i++) {
			FString name;
			// if is bp enum, can't get name as key
			if(isbpEnum)
				name = e->GetDisplayNameTextByIndex(i).ToString();
			else
				name = e->GetNameStringByIndex(i);
			int64 value = e->GetValueByIndex(i);
			lua_pushinteger(L, value);
			lua_setfield(L, -2, TCHAR_TO_UTF8(*name));
		}
		return 1;
	}

    int LuaObject::gcObject(lua_State* L) {
		CheckUDGC(UObject,L,1);
        removeRef(L,UD);
        return 0;
    }

    int LuaObject::gcClass(lua_State* L) {
		CheckUDGC(UClass,L,1);
        removeRef(L,UD);
        return 0;
    }

    int LuaObject::gcStructClass(lua_State* L) {
		CheckUDGC(UScriptStruct,L,1);
        removeRef(L,UD);
        return 0;
    }

	int LuaObject::gcStruct(lua_State* L) {
		CheckUDGC(LuaStruct, L, 1);
		deleteFGCObject(L,UD);
		return 0;
	}

    int LuaObject::push(lua_State* L, UObject* obj, bool rawpush, bool ref) {
		if (!obj) return pushNil(L);
		if (!rawpush) {
			if (auto it = Cast<ILuaTableObjectInterface>(obj)) {
				return ILuaTableObjectInterface::push(L, it);
			}
		}
		if (auto e = Cast<UEnum>(obj))
			return pushEnum(L, e);
		else if (auto c = Cast<UClass>(obj))
			return pushClass(L, c);
		else if (auto s = Cast<UScriptStruct>(obj))
			return pushStruct(L, s);
		else
			return pushGCObject<UObject*>(L,obj,"UObject",setupInstanceMT,gcObject,ref);
    }

	int LuaObject::push(lua_State* L, FWeakObjectPtr ptr) {
		if (!ptr.IsValid()) {
			lua_pushnil(L);
			return 1;
		}
		UObject* obj = ptr.Get();
    	// weak 缓存里有就不再push
		if (getFromCache(L, obj, "UObject")) return 1;
		int r = pushWeakType(L, new WeakUObjectUD(ptr));
		if (r) cacheObj(L, obj);
		return r;
	}

	// 注册push
    template<typename T>
    inline void regPusher() {
		pusherMap.Add(T::StaticClass(), pushUProperty<T>);
    }

	// 注册check
    template<typename T>
    inline void regChecker() {
		checkerMap.Add(T::StaticClass(), checkUProperty<T>);
    }

    void LuaObject::init(lua_State* L) {
    	// 基本类型
		regPusher<UIntProperty>();
		regPusher<UUInt32Property>();
        regPusher<UInt64Property>();
        regPusher<UUInt64Property>();
		regPusher<UInt16Property>();
		regPusher<UUInt16Property>();
		regPusher<UInt8Property>();
		regPusher<UByteProperty>(); // uint8
		regPusher<UFloatProperty>();
		regPusher<UDoubleProperty>();
        regPusher<UBoolProperty>();
        regPusher<UTextProperty>();
        regPusher<UStrProperty>();
        regPusher<UNameProperty>();

    	// UE4类型
		regPusher(UDelegateProperty::StaticClass(), pushUDelegateProperty);
        regPusher(UMulticastDelegateProperty::StaticClass(),pushUMulticastDelegateProperty);
#if (ENGINE_MINOR_VERSION>=23) && (ENGINE_MAJOR_VERSION>=4)
		regPusher(UMulticastInlineDelegateProperty::StaticClass(), pushUMulticastInlineDelegateProperty);
#endif
        regPusher(UObjectProperty::StaticClass(),pushUObjectProperty);
        regPusher(UArrayProperty::StaticClass(),pushUArrayProperty);
        regPusher(UMapProperty::StaticClass(),pushUMapProperty);
        regPusher(UStructProperty::StaticClass(),pushUStructProperty);
		regPusher(UEnumProperty::StaticClass(), pushEnumProperty);
		regPusher(UClassProperty::StaticClass(), pushUClassProperty);
		regPusher(UWeakObjectProperty::StaticClass(), pushUWeakProperty);
		
		regChecker<UIntProperty>();
		regChecker<UUInt32Property>();
        regChecker<UInt64Property>();
        regChecker<UUInt64Property>();
		regChecker<UInt16Property>();
		regChecker<UUInt16Property>();
		regChecker<UInt8Property>();
		regChecker<UByteProperty>(); // uint8
		regChecker<UFloatProperty>();
		regChecker<UDoubleProperty>();
		regChecker<UBoolProperty>();
        regChecker<UNameProperty>();
        regChecker<UTextProperty>();
		regChecker<UObjectProperty>();
        regChecker<UStrProperty>();
        regChecker<UEnumProperty>();

        regChecker(UArrayProperty::StaticClass(),checkUArrayProperty);
        regChecker(UMapProperty::StaticClass(),checkUMapProperty);
        regChecker(UDelegateProperty::StaticClass(),checkUDelegateProperty);
        regChecker(UStructProperty::StaticClass(),checkUStructProperty);
		regChecker(UClassProperty::StaticClass(), checkUClassProperty);
		
		LuaWrapper::init(L);
        ExtensionMethod::init();
    }

    int LuaObject::push(lua_State* L,UFunction* func,UClass* cls)  {
        lua_pushlightuserdata(L, func);
        if(cls) {
            lua_pushlightuserdata(L, cls);
            lua_pushcclosure(L, ufuncClosure, 2);
        }
        else
            lua_pushcclosure(L, ufuncClosure, 1);
        return 1;
    }

    int LuaObject::push(lua_State* L,UProperty* prop,uint8* parms,bool ref) {
        auto pusher = getPusher(prop);
        if (pusher)
            return pusher(L,prop,parms,ref);
        else {
            FString name = prop->GetClass()->GetName();
            Log::Error("unsupport type %s to push",TCHAR_TO_UTF8(*name));
            return 0;
        }
    }

	int LuaObject::push(lua_State* L, UProperty* up, UObject* obj, bool ref) {
		auto cls = up->GetClass();
		// if it's an UArrayProperty
		if (cls==UArrayProperty::StaticClass())
			return LuaArray::push(L, Cast<UArrayProperty>(up), obj);
        // if it's an UMapProperty
        else if(cls==UMapProperty::StaticClass())
            return LuaMap::push(L,Cast<UMapProperty>(up),obj);
		else
			return push(L, up, up->ContainerPtrToValuePtr<uint8>(obj), ref);
	}

	int LuaObject::push(lua_State* L, LuaStruct* ls) {
		return pushType<LuaStruct*>(L, ls, "LuaStruct", setupInstanceStructMT, gcStruct);
	}

	int LuaObject::push(lua_State* L, double v) {
		lua_pushnumber(L, v);
		return 1;
	}

	int LuaObject::push(lua_State* L, float v) {
		lua_pushnumber(L, v);
		return 1;
	}

    int LuaObject::push(lua_State* L, int64 v) {
		lua_pushinteger(L, v);
		return 1;
	}

    int LuaObject::push(lua_State* L, uint64 v) {
		lua_pushinteger(L, v);
		return 1;
	}

	int LuaObject::push(lua_State* L, int v) {
		lua_pushinteger(L, v);
		return 1;
	}

    int LuaObject::push(lua_State* L, bool v) {
		lua_pushboolean(L, v);
		return 1;
	}

	int LuaObject::push(lua_State* L, uint32 v) {
		lua_pushinteger(L, v);
		return 1;
	}

    int LuaObject::push(lua_State* L, int16 v) {
		lua_pushinteger(L, v);
		return 1;
	}

    int LuaObject::push(lua_State* L, uint16 v) {
		lua_pushinteger(L, v);
		return 1;
	}

    int LuaObject::push(lua_State* L, int8 v) {
		lua_pushinteger(L, v);
		return 1;
	}

    int LuaObject::push(lua_State* L, uint8 v) {
		lua_pushinteger(L, v);
		return 1;
	}

    int LuaObject::push(lua_State* L, const LuaVar& v) {
        return v.push(L);
	}

    int LuaObject::push(lua_State* L, void* ptr) {
		lua_pushlightuserdata(L,ptr);
		return 1;
	}

	int LuaObject::push(lua_State* L, const FText& v) {
		FString str = v.ToString();
		lua_pushstring(L, TCHAR_TO_UTF8(*str));
		return 1;
	}

	int LuaObject::push(lua_State* L, const FString& str) {
		lua_pushstring(L, TCHAR_TO_UTF8(*str));
		return 1;
	}

    int LuaObject::push(lua_State* L, const FName& name) {
		lua_pushstring(L, TCHAR_TO_UTF8(*name.ToString()));
		return 1;
	}

    int LuaObject::push(lua_State* L, const char* str) {
		lua_pushstring(L, str);
		return 1;
	}

    int LuaObject::setupMTSelfSearch(lua_State* L) {
        lua_pushcfunction(L,instanceIndexSelf);
		lua_setfield(L, -2, "__index");
		lua_pushcfunction(L, objectToString);
		lua_setfield(L, -2, "__tostring");
        return 0;
    }


    int LuaObject::setupClassMT(lua_State* L) {
        lua_pushcfunction(L,classConstruct);
        lua_setfield(L, -2, "__call");
        lua_pushcfunction(L,NS_SLUA::classIndex);
		lua_setfield(L, -2, "__index");
		lua_pushcfunction(L, objectToString);
		lua_setfield(L, -2, "__tostring");
        return 0;
    }

    int LuaObject::setupStructMT(lua_State* L) {
        lua_pushcfunction(L,structConstruct);
		lua_setfield(L, -2, "__call");
		lua_pushcfunction(L, objectToString);
		lua_setfield(L, -2, "__tostring");
        return 0;
    }

    int LuaObject::setupInstanceMT(lua_State* L) {
        lua_pushcfunction(L,instanceIndex);
        lua_setfield(L, -2, "__index");
        lua_pushcfunction(L,newinstanceIndex);
        lua_setfield(L, -2, "__newindex");
		lua_pushcfunction(L, objectToString);
		lua_setfield(L, -2, "__tostring");
        return 0;
    }

    int LuaObject::setupInstanceStructMT(lua_State* L) {
        lua_pushcfunction(L,instanceStructIndex);
        lua_setfield(L, -2, "__index");
        lua_pushcfunction(L, newinstanceStructIndex);
		lua_setfield(L, -2, "__newindex");
		lua_pushcfunction(L, objectToString);
		lua_setfield(L, -2, "__tostring");
        return 0;
    }
}

#ifdef _WIN32
#pragma warning (pop)
#endif