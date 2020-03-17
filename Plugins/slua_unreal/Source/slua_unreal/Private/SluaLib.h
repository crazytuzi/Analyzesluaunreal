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
#include "lua/lua.hpp"

namespace NS_SLUA {

    // used for lua interface
    class SluaUtil {
    public:
    	// ע�ᵽslua�µķ���
        static void openLib(lua_State* L);
    	// ע�ᵽslua�µ�����
        static void reg(lua_State* L,const char* fn,lua_CFunction f);
    private:
    	// ����UI
        static int loadUI(lua_State* L);
		// ������
    	static int loadClass(lua_State* L);
		// ����ί��
    	static int createDelegate(lua_State* L);

		// remote profile
    	// ����Tick����
		static int setTickFunction(lua_State* L);
		static int makeProfilePackage(lua_State* L);
    	// ��ȡus
		static int getMicroseconds(lua_State* L);
		// ��ȡms
    	static int getMiliseconds(lua_State* L);
    	// ���ض���
		static int loadObject(lua_State* L);
    	// �Ƿ������߳�gc
		static int threadGC(lua_State* L);
		// dump all uobject that referenced by lua
    	// ��ӡ����UObjects
		static int dumpUObjects(lua_State* L);
		// return whether an userdata is valid?
    	// �Ƿ���Ч
		static int isValid(lua_State* L);
    };

}