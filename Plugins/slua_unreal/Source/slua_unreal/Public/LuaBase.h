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
#include "CoreMinimal.h"
#include "LuaState.h"
#include "LuaBlueprintLibrary.h"
#if !((ENGINE_MINOR_VERSION>21) && (ENGINE_MAJOR_VERSION>=4))
#include "Kismet/GameplayStatics.h"
#endif
#include "LuaBase.generated.h"

namespace NS_SLUA {

// special tick function
#define UFUNCTION_TICK ((UFunction*)-1)

	// SuperOrRpc
	struct LuaSuperOrRpc {
		class LuaBase* base;
		LuaSuperOrRpc(class LuaBase* pBase) :base(pBase) {}
	};

	// Super
	struct LuaSuper : public LuaSuperOrRpc {
		LuaSuper(class LuaBase* pBase) :LuaSuperOrRpc(pBase) {}
	};

	// Rpc
	struct LuaRpc : public LuaSuperOrRpc {
		LuaRpc(class LuaBase* pBase) :LuaSuperOrRpc(pBase) {}
	};

	class SLUA_UNREAL_API LuaBase {
	public:
		// ��ʶ����
		enum IndexFlag {
			IF_NONE,
			IF_SUPER,
			IF_RPC,
		};

		// ���ú���
		virtual bool luaImplemented(UFunction* func, void* params);
		virtual ~LuaBase() {}

		const FWeakObjectPtr& getContext() const {
			return context;
		}

		// ��ȡ����
		const IndexFlag getIndexFlag() const {
			return indexFlag;
		}
	protected:

		// ��ȡGS
		inline UGameInstance* getGameInstance(AActor* self) {
			return self->GetGameInstance();
		}

		inline UGameInstance* getGameInstance(UActorComponent* self) {
			return self->GetOwner()->GetGameInstance();
		}

		inline UGameInstance* getGameInstance(UUserWidget* self) {
#if (ENGINE_MINOR_VERSION>21) && (ENGINE_MAJOR_VERSION>=4)
			return self->GetGameInstance();
#else
			return UGameplayStatics::GetGameInstance(self);
#endif
		}

		// GC
		template<typename T>
		static int genericGC(lua_State* L) {
			CheckUDGC(T, L, 1);
			delete UD;
			return 0;
		}

		// ��ʼ��
		template<typename T>
		bool init(T* ptrT, const char* typeName, const FString& stateName, const FString& luaPath)
		{
			if (luaPath.IsEmpty())
				return false;
			
			ensure(ptrT);
			// ��ȡls,ͨ��GS�û��ߴ��ݽ�����name
			auto ls = LuaState::get(getGameInstance(ptrT));
			if (stateName.Len() != 0) ls = LuaState::get(stateName);
			if (!ls) return false;

			// �����ļ�
			luaSelfTable = ls->doFile(TCHAR_TO_UTF8(*luaPath));
			if (!luaSelfTable.isTable())
				return false;

			context = ptrT;
			auto L = ls->getLuaState();
			// setup __cppinst
			// we use rawpush to bind objptr and SLUA_CPPINST
			luaSelfTable.push(L);
			LuaObject::push(L, ptrT, true);
			lua_setfield(L, -2, SLUA_CPPINST);

			LuaObject::pushType(L, new LuaSuper(this) , "LuaSuper", supermt, genericGC<LuaSuper>);
			lua_setfield(L, -2, "Super");

			LuaObject::pushType(L, new LuaRpc(this), "LuaRpc", rpcmt, genericGC<LuaRpc>);
			lua_setfield(L, -2, "Rpc");

			int top = lua_gettop(L);
			// �����غ���
			bindOverrideFunc(ptrT);
			int newtop = lua_gettop(L);

			// setup metatable
			// ����Ԫ��
			if (!metaTable.isValid()) {
				luaL_newmetatable(L, typeName);
				lua_pushcfunction(L, __index);
				lua_setfield(L, -2, "__index");
				lua_pushcfunction(L, __newindex);
				lua_setfield(L, -2, "__newindex");
				metaTable.set(L, -1);
				lua_pop(L, 1);
			}
			metaTable.push(L);
			lua_setmetatable(L, -2);

			// pop luaSelfTable
			lua_pop(L, 1);
			return true;
		}

		// Lua��������BP����
		static void hookBpScript(UFunction* func, FNativeFuncPtr hookfunc);
		// �����غ���
		void bindOverrideFunc(UObject* obj);
		DECLARE_FUNCTION(luaOverrideFunc);

		// super Ԫ��
		static int supermt(lua_State* L);
		// rpc Ԫ��
		static int rpcmt(lua_State* L);

		// store deltaTime for super call
		float deltaTime;

		// call member function in luaSelfTable
		// ���ú���
		LuaVar callMember(FString name, const TArray<FLuaBPVar>& args);

		// ��Tick������ֵ,���Ҳ�ѯ�Ƿ�Tick
		bool postInit(const char* tickFlag,bool rawget=true);
		// Tick����
		virtual void tick(float DeltaTime);
		// should override this function to support super::tick
		// ����Tick
		virtual void superTick(lua_State* L);
		// ���麯�����뱻����ʵ��g
		virtual void superTick() = 0;
		// ��������
		virtual int superOrRpcCall(lua_State* L, UFunction* func);
		static int __index(lua_State* L);
		static int __newindex(lua_State* L);
		static int __superIndex(lua_State* L);
		static int __rpcIndex(lua_State* L);
		static int __superTick(lua_State* L);
		static int __superCall(lua_State* L);
		static int __rpcCall(lua_State* L);

		LuaVar luaSelfTable;
		LuaVar tickFunction;
		FWeakObjectPtr context;
		LuaVar metaTable;
		IndexFlag indexFlag = IF_NONE;
		UFunction* currentFunction = nullptr;
	};

	DefTypeName(LuaSuper);
	DefTypeName(LuaRpc);
}

UINTERFACE()
class ULuaTableObjectInterface : public UInterface
{
	GENERATED_UINTERFACE_BODY()
};

class ILuaTableObjectInterface {
	GENERATED_IINTERFACE_BODY()

public:
	// �ж���Ч��
	static bool isValid(ILuaTableObjectInterface* luaTableObj);
	static int push(NS_SLUA::lua_State* L, ILuaTableObjectInterface* luaTableObj);

	// self
	virtual NS_SLUA::LuaVar getSelfTable() const = 0;
};