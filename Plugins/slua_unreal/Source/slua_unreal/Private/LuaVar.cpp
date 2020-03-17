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

#include "LuaVar.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"
#include "UObject/Stack.h"
#include "Blueprint/WidgetTree.h"
#include "LuaState.h"

namespace NS_SLUA {

	const int INVALID_INDEX = -1;
	// �������͵ĳ�ʼ��
    LuaVar::LuaVar()
        :stateIndex(INVALID_INDEX)
    {
        vars = nullptr;
        numOfVar = 0;
    }

    LuaVar::LuaVar(lua_Integer v)
        :LuaVar()
    {
        set(v);
    }

    LuaVar::LuaVar(int v)
        :LuaVar()
    {
        set((lua_Integer)v);
    }

    LuaVar::LuaVar(size_t v)
        :LuaVar()
    {
        set((lua_Integer)v);
    }

    LuaVar::LuaVar(lua_Number v)
        :LuaVar()
    {
        set(v);
    }

    LuaVar::LuaVar(bool v)
        :LuaVar()
    {
        set(v);
    }

    LuaVar::LuaVar(const char* v)
        :LuaVar()
    {
        set(v,strlen(v));
    }

	LuaVar::LuaVar(const char* v,size_t len)
		: LuaVar()
	{
		set(v, len);
	}

	// �Զ�ʶ������
    LuaVar::LuaVar(lua_State* l,int p):LuaVar() {
        set(l,p);
    }

	// �Զ�ʶ������
    void LuaVar::set(lua_State* l,int p) {
    	// �������
        free();

    	/*
    	 * int lua_type (lua_State *L, int index)
    	 * ���ظ�����Ч������ֵ������
    	 * ��������Ч�����޷����ʣ�ʱ�򷵻� LUA_TNONE
    	 * lua_type ���ص����ͱ�����ΪһЩ���� lua.h �ж���ĳ���
    	 * LUA_TNIL�� LUA_TNUMBER�� LUA_TBOOLEAN�� LUA_TSTRING
    	 * LUA_TTABLE�� LUA_TFUNCTION�� LUA_TUSERDATA�� LUA_TTHREAD
    	 * LUA_TLIGHTUSERDATA
    	 */
    	
        int t = lua_type(l,p);
        LuaVar::Type type = LV_NIL;
        switch(t) {
            case LUA_TNUMBER:
                {
            		
            		/*
            		 * int lua_isinteger (lua_State *L, int index)
            		 * ������������ֵ��һ������
            		 * ����ֵ��һ�����֣����ڲ����������棩ʱ������ 1
            		 * ���򷵻� 0 ��
            		 */
            		
                if(lua_isinteger(l,p))
                    type = LV_INT;
                else
                    type = LV_NUMBER;
                }
                break;
            case LUA_TSTRING:
                type = LV_STRING;
                break;
            case LUA_TFUNCTION:
                type = LV_FUNCTION;
                break;
            case LUA_TTABLE:
                type = LV_TABLE;
                break;
            case LUA_TUSERDATA:
                type = LV_USERDATA;
                break;
            case LUA_TLIGHTUSERDATA:
                type = LV_LIGHTUD;
                break;
            case LUA_TBOOLEAN:
                type = LV_BOOL;
                break;
            case LUA_TNIL:
            default:
                type = LV_NIL;
                break;
        }
    	// ��ʼ��
        init(l,p,type);
    }

	// �ֶ�ָ������
    LuaVar::LuaVar(lua_State* l,int p,LuaVar::Type type):LuaVar() {
        init(l,p,type);
    }

    // used to create number n of tuple
    // it used for return value from lua
    // don't call it to create n element of tuple
    // �����ռ�����ֵ
    // ��Ҫ���������ط�
    LuaVar::LuaVar(lua_State* l,size_t n):LuaVar() {
        init(l,n,LV_TUPLE);
    }

	// ��ȡ��ǰ��������������
    lua_State* LuaVar::getState() const
    {
		auto ls = LuaState::get(stateIndex);
		return ls ? ls->getLuaState() : nullptr;
    }

    void LuaVar::init(lua_State* l,int p,LuaVar::Type type) {
        auto state = LuaState::get(l);
        stateIndex = state->stateIndex();
        switch(type) {
        case LV_NIL:
            break;
        case LV_INT:
        	
        	/*
        	 * lua_Integer lua_tointeger (lua_State *L, int index)
        	 * �ȼ��ڵ��� lua_tointegerx�� ����� isnum Ϊ NULL
        	 */

        	/*
        	 * lua_Integer lua_tointegerx (lua_State *L, int index, int *isnum)
        	 * �������������� Lua ֵת��Ϊ�����ŵ��������� lua_Integer
        	 * ��� Lua ֵ������һ������������һ�����Ա�ת��Ϊ���������ֻ��ַ���
        	 * ����lua_tointegerx ���� 0
        	 * ��� isnum ���� NULL�� *isnum �ᱻ��Ϊ�����Ƿ�ɹ�
        	 */
        	
            set(lua_tointeger(l,p));
            break;
        case LV_NUMBER:

        	/*
        	 * lua_Number lua_tonumber (lua_State *L, int index)
        	 * �ȼ��ڵ��� lua_tonumberx�� ����� isnum Ϊ NULL
        	 */

        	/*
        	 * lua_Number lua_tonumberx (lua_State *L, int index, int *isnum)
        	 * �Ѹ����������� Lua ֵת��Ϊ lua_Number ����һ�� C ����
        	 * ��� Lua ֵ������һ�����ֻ���һ����ת��Ϊ���ֵ��ַ���
        	 * ���� lua_tonumberx ���� 0
        	 * ��� isnum ���� NULL�� *isnum �ᱻ��Ϊ�����Ƿ�ɹ�
        	 */
        	
            set(lua_tonumber(l,p));
            break;
		case LV_STRING: {
			size_t len;
			const char* buf = lua_tolstring(l, p, &len);
			set(buf,len);
			break;
		}
        case LV_BOOL:

        	/*
        	 * int lua_toboolean (lua_State *L, int index)
        	 * �Ѹ����������� Lua ֵת��Ϊһ�� C �еĲ������� 0 ���� 1 ��
        	 * �� Lua ���������в���һ���� lua_toboolean ����κβ�ͬ�� false �� nil ��ֵ�����淵��
        	 * ����ͷ��ؼ�
        	 * ���������ֻ���������� boolean ֵ�� ����Ҫʹ�� lua_isboolean ������ֵ������
        	 */
        	
            set(!!lua_toboolean(l,p));
            break;
        case LV_LIGHTUD:
        	// �����û����� ����ָһ���򵥵� C ָ��

        	/*
        	 * void *lua_touserdata (lua_State *L, int index)
        	 * ���������������ֵ��һ����ȫ�û����ݣ� �����������ڴ��ĵ�ַ
        	 * ���ֵ��һ�������û����ݣ� ��ô�ͷ�������ʾ��ָ��
        	 * ���򣬷��� NULL
        	 */
        	
            alloc(1);
            vars[0].ptr = lua_touserdata(l,p);
            vars[0].luatype = type;
            break;
        case LV_FUNCTION: 
        case LV_TABLE:
        case LV_USERDATA:
            alloc(1);
        	// ����ѹջ��Ŀ����Ϊ��
        	// RefRef���캯���л����luaL_ref
        	// �������ֵ
        	// ͬʱluaL_ref�ᵯ��ջ��
        	// ��������push֮��û��pop����
            lua_pushvalue(l,p);
            vars[0].ref = new RefRef(l);
            vars[0].luatype=type;
            break;
        case LV_TUPLE:
        	// Ԫ��,������Լ����������,��ͬ��LuaԪ�����
        	// Ŀ����Ϊ���ռ�����ֵ
            ensure(p>0 && lua_gettop(l)>=p);
            initTuple(l,p);
            break;
        default:
            break;
        }
    }

    void LuaVar::initTuple(lua_State* l,size_t n) {
    	// ȷ��ջ����������n
        ensure(lua_gettop(l)>=n);
    	// ���ٿռ�
        alloc(n);
    	// �������ֵ��2��,���Բ���lua_gettop(l)Ϊ2
    	// �ʴ�ʱfΪ1
        int f = lua_gettop(l)-n+1;
        for(size_t i=0;i<n;i++) {
            
            int p = i+f;
            int t = lua_type(l,p);

            switch(t) {
            case LUA_TBOOLEAN:
                vars[i].luatype = LV_BOOL;
                vars[i].b = !!lua_toboolean(l, p);
                break;
            case LUA_TNUMBER:
                {
                    if(lua_isinteger(l,p)) {
                        vars[i].luatype = LV_INT;
                        vars[i].i = lua_tointeger(l,p);
                    }
                    else {
                        vars[i].luatype = LV_NUMBER;
                        vars[i].d = lua_tonumber(l,p);
                    }
                }
                break;
			case LUA_TSTRING: {
				vars[i].luatype = LV_STRING;
				size_t len;
				const char* buf = lua_tolstring(l, p, &len);
				vars[i].s = new RefStr(buf,len);
				break;
			}
            case LUA_TFUNCTION:
                vars[i].luatype = LV_FUNCTION;
                lua_pushvalue(l,p);
                vars[i].ref = new RefRef(l);
                break;
            case LUA_TTABLE:
                vars[i].luatype = LV_TABLE;
                lua_pushvalue(l,p);
                vars[i].ref = new RefRef(l);
                break;
			case LUA_TUSERDATA:
				vars[i].luatype = LV_USERDATA;
				lua_pushvalue(l, p);
				vars[i].ref = new RefRef(l);
				break;
			case LUA_TLIGHTUSERDATA:
				vars[i].luatype = LV_LIGHTUD;
				vars[i].ptr = lua_touserdata(l, p);
				break;
            case LUA_TNIL:
            default:
                vars[i].luatype = LV_NIL;
                break;
            }
        }
    }

	// �������������ͷ�
    LuaVar::~LuaVar() {
        free();
    }

    LuaVar::RefRef::RefRef(lua_State* l) 
        :LuaVar::Ref() 
    {
    	// ��������,���ᵯ��ջ������
        ref=luaL_ref(l,LUA_REGISTRYINDEX);
        stateIndex = LuaState::get(l)->stateIndex();
    }

    LuaVar::RefRef::~RefRef() {
        if(LuaState::isValid(stateIndex)) {
            auto state = LuaState::get(stateIndex);
        	/*
        	 * void luaL_unref (lua_State *L, int t, int ref)
        	 * �ͷ����� t ����� ref ���ö���
        	 * ����Ŀ��ӱ����Ƴ����������õĶ���ɱ������ռ�
        	 * ������ ref Ҳ�������ٴ�ʹ��
        	 * ��� ref Ϊ LUA_NOREF �� LUA_REFNIL�� luaL_unref ʲôҲ����
        	 */

            luaL_unref(state->getLuaState(),LUA_REGISTRYINDEX,ref);
        }
    }

	// �ͷ�
    void LuaVar::free() {
        for(size_t n=0;n<numOfVar;n++) {
            if( (vars[n].luatype==LV_FUNCTION || vars[n].luatype==LV_TABLE || vars[n].luatype == LV_USERDATA)
                && vars[n].ref->isValid() )
                vars[n].ref->release();
            else if(vars[n].luatype==LV_STRING)
                vars[n].s->release();
        }
        numOfVar = 0;
        delete[] vars;
        vars = nullptr;
    }

    void LuaVar::alloc(int n) {
        if(n>0) {
            vars = new lua_var[n];
            numOfVar = n;
        }
    }

    bool LuaVar::next(LuaVar& key,LuaVar& value) {
        if(!isTable())
            return false;

        auto L = getState();
    	// ��ʱջ��=>ջ�� t
        push(L);
    	// ��ʱջ��=>ջ�� t key1
        key.push(L);

    	/*
    	 * int lua_next (lua_State *L, int index)
    	 * ��ջ������һ������ Ȼ�������ָ���ı��е�һ����ֵ��ѹջ
    	 * �������ļ�֮��� ����һ�� �ԣ�
    	 * ����������޸���Ԫ�أ� ��ô lua_next ������ 0 ��ʲôҲ��ѹջ��
    	 */

    	// lua_next�ɹ���
    	// �Ƚ�key1����
    	// Ȼ��key1��һ�Լ�ֵ��ѹջkey2,value2
		// ��ʱջ��=>ջ�� t key2,value2
        if(lua_next(L,-2)!=0) {
        	// �����µ�key
            key.set(L,-2);
        	// �����µ�value
            value.set(L,-1);
        	// ���ջ
            lua_pop(L,3);
            return true;
        }
        else {
            key.free();
            value.free();
        	// ��Ϊlua_next�Ѿ���key1����,����ջֻʣ��t
        	// �ʵ���1������
            lua_pop(L,1);
            return false;
        }
    }

    const char* LuaVar::toString() {
        auto L = getState();
        push(L);
        const char* ret;
        ret = luaL_tolstring(L,-1,NULL);
        lua_pop(L,2);
        return ret;
    }

	// ��ȡ����
	// �����lua_rawlen
	// ��������numOfVar
    size_t LuaVar::count() const {
        if(isTable()) {
            auto L = getState();
            push(L);
            size_t n = lua_rawlen(L,-1);
            lua_pop(L,1);
            return n;
        }
        return numOfVar;
    }

    int LuaVar::asInt() const {
        ensure(numOfVar==1);
        switch(vars[0].luatype) {
        case LV_INT:
            return vars[0].i;
        case LV_NUMBER:
            return vars[0].d;
        default:
            return -1;
        }
    }

    int64 LuaVar::asInt64() const {
        ensure(numOfVar==1);
        switch(vars[0].luatype) {
        case LV_INT:
            return vars[0].i;
        case LV_NUMBER:
            return vars[0].d;
        default:
            return -1;
        }
    }

    float LuaVar::asFloat() const {
        ensure(numOfVar==1);
        switch(vars[0].luatype) {
        case LV_INT:
            return vars[0].i;
        case LV_NUMBER:
            return vars[0].d;
        default:
            return NAN;
        }
    }

    double LuaVar::asDouble() const {
        ensure(numOfVar==1);
        switch(vars[0].luatype) {
        case LV_INT:
            return vars[0].i;
        case LV_NUMBER:
            return vars[0].d;
        default:
            return NAN;
        }
    }

    const char* LuaVar::asString(size_t* outlen) const {
        ensure(numOfVar==1 && vars[0].luatype==LV_STRING);
		if(outlen) *outlen = vars[0].s->length;
        return vars[0].s->buf;
    }

	LuaLString LuaVar::asLString() const
	{
		ensure(numOfVar == 1 && vars[0].luatype == LV_STRING);
		return { vars[0].s->buf,vars[0].s->length };
	}

    bool LuaVar::asBool() const {
        ensure(numOfVar==1 && vars[0].luatype==LV_BOOL);
        return vars[0].b;
    }

    void* LuaVar::asLightUD() const {
        ensure(numOfVar==1 && vars[0].luatype==LV_LIGHTUD);
        return vars[0].ptr;
    }

    LuaVar LuaVar::getAt(size_t index) const {
        auto L = getState();
        if(isTable()) { // ��
            push(L); // push this table
            lua_geti(L,-1,index); // get by index
            LuaVar r(L,-1); // construct LuaVar
            lua_pop(L,2); // pop table and value
            return r;
        }
        else { // Ԫ��
            ensure(index>0);
            ensure(numOfVar>=index);
            LuaVar r;
            r.alloc(1);
            r.stateIndex = this->stateIndex;
            varClone(r.vars[0],vars[index-1]);
            return r;
        }
    }

    void LuaVar::set(lua_Integer v) {
        free();
        alloc(1);
        vars[0].i = v;
        vars[0].luatype = LV_INT;
    }

    void LuaVar::set(int v) {
        free();
        alloc(1);
        vars[0].i = v;
        vars[0].luatype = LV_INT;
    }

    void LuaVar::set(lua_Number v) {
        free();
        alloc(1);
        vars[0].d = v;
        vars[0].luatype = LV_NUMBER;
    }

    void LuaVar::set(const char* v,size_t len) {
        free();
        alloc(1);
        vars[0].s = new RefStr(v,len);
        vars[0].luatype = LV_STRING;
    }

	void LuaVar::set(const LuaLString & lstr)
	{
		set(lstr.buf, lstr.len);
	}

    void LuaVar::set(bool b) {
        free();
        alloc(1);
        vars[0].b = b;
        vars[0].luatype = LV_BOOL;
    }

    void LuaVar::pushVar(lua_State* l,const lua_var& ov) const {
        switch(ov.luatype) {
        case LV_INT:
            lua_pushinteger(l,ov.i);
            break;
        case LV_NUMBER:
            lua_pushnumber(l,ov.d);
            break;
        case LV_BOOL:
            lua_pushboolean(l,ov.b);
            break;
        case LV_STRING:
            lua_pushlstring(l,ov.s->buf,ov.s->length);
            break;
        case LV_FUNCTION:
        case LV_TABLE:
        case LV_USERDATA:
            ov.ref->push(l);
            break;
        case LV_LIGHTUD:
            lua_pushlightuserdata(l,ov.ptr);
            break;
        default:
            lua_pushnil(l);
            break;
        }
    }

    int LuaVar::push(lua_State* l) const {
        if(l==nullptr) l=getState();
        if(l==nullptr) return 0;

        if(vars==nullptr || numOfVar==0) {
            lua_pushnil(l);
            return 1;
        }
        
        if(numOfVar==1) {
            const lua_var& ov = vars[0];
            pushVar(l,ov);
            return 1;
        }
        for(size_t n=0;n<numOfVar;n++) {
            const lua_var& ov = vars[n];
            pushVar(l,ov);
        }
        return numOfVar;
    }

    bool LuaVar::isValid() const {
        return numOfVar>0 && stateIndex>0 && LuaState::isValid(stateIndex);
    }

    bool LuaVar::isNil() const {
        return vars==nullptr || numOfVar==0;
    }

    bool LuaVar::isFunction() const {
        return numOfVar==1 && vars[0].luatype==LV_FUNCTION;
    }

    bool LuaVar::isTuple() const {
        return numOfVar>1;
    }

    bool LuaVar::isTable() const {
        return numOfVar==1 && vars[0].luatype==LV_TABLE;
    }

    bool LuaVar::isInt() const {
        return numOfVar==1 && vars[0].luatype==LV_INT;
    }

    bool LuaVar::isNumber() const {
        return numOfVar==1 && vars[0].luatype==LV_NUMBER;
    }

    bool LuaVar::isBool() const {
        return numOfVar==1 && vars[0].luatype==LV_BOOL;
    }

    bool LuaVar::isUserdata(const char* t) const {
        if(numOfVar==1 && vars[0].luatype==LV_USERDATA) {
            auto L = getState();
            push(L);
            void* p = luaL_testudata(L, -1, t);
            lua_pop(L,1);
            return p!=nullptr;
        }
        return false;
    }

    bool LuaVar::isLightUserdata() const {
        return numOfVar==1 && vars[0].luatype==LV_LIGHTUD;
    }

    bool LuaVar::isString() const {
        return numOfVar==1 && vars[0].luatype==LV_STRING;
    }

    LuaVar::Type LuaVar::type() const {
        if(numOfVar==0)
            return LV_NIL;
        else if(numOfVar==1)
            return vars[0].luatype;
        else
            return LV_TUPLE;
    }

    int LuaVar::docall(int argn) const {
        if(!isValid()) {
            Log::Error("State of lua function is invalid");
            return 0;
        }
    	// �ٶ���ǰջ�������ǲ�������(���費Ӱ���������)
        auto L = getState();
        int top = lua_gettop(L); // top = argn
        top=top-argn+1; // top = 1
		// ջ��=>ջ�� n������ error����
        LuaState::pushErrorHandler(L);
    	
    	/*
    	 * void lua_insert (lua_State *L, int index)
    	 * ��ջ��Ԫ���ƶ���ָ������Ч�������� �����ƶ��������֮�ϵ�Ԫ��
    	 * ��Ҫ��α������������������� ��Ϊα����û������ָ��ջ�ϵ�λ��
    	 */

    	// ����error������ջ��
		// ջ��=>ջ�� error���� n������
        lua_insert(L,top);
    	// �Ѻ���ָ��ѹջ
    	// ջ��=>ջ�� error���� n������ ����
        vars[0].ref->push(L);

		{
        	/*
        	 * int lua_pcallk (lua_State *L,
                int nargs,
                int nresults,
                int msgh,
                lua_KContext ctx,
                lua_KFunction k)
        	 * �����������Ϊ�� lua_pcall ��ȫһ�£�ֻ���������������õĺ����ó�
        	 */

        	/*
        	 * int lua_pcall (lua_State *L, int nargs, int nresults, int msgh)
        	 * �Ա���ģʽ����һ������
        	 * nargs �� nresults �ĺ����� lua_call �е���ͬ
        	 * ����ڵ��ù�����û�з������� lua_pcall ����Ϊ�� lua_call ��ȫһ��
        	 * ���ǣ�����д������Ļ��� lua_pcall �Ჶ������ Ȼ���Ψһ��ֵ��������Ϣ��ѹջ��Ȼ�󷵻ش�����
        	 * ͬ lua_call һ���� lua_pcall ���ǰѺ�����������Ĳ�����ջ���Ƴ�
        	 * ��� msgh �� 0 �� ������ջ���Ĵ�����Ϣ�ͺ�ԭʼ������Ϣ��ȫһ��
        	 * ���� msgh �ͱ������� �������� ��ջ�ϵ�����λ��
        	 * �ڵ�ǰ��ʵ����������������α����
        	 * �ڷ�������ʱ����ʱ�� ��������ᱻ���ö��������Ǵ�����Ϣ
        	 * ���������ķ���ֵ���� lua_pcall ��Ϊ������Ϣ�����ڶ�ջ��
        	 * ���͵��÷��У�����������������������Ϣ���ϸ���ĵ�����Ϣ�� ����ջ������Ϣ
        	 * ��Щ��Ϣ�� lua_pcall ���غ� ����ջ�Ѿ�չ���������ռ�������
        	 * lua_pcall �����᷵�����г��� �������� lua.h �ڣ��е�һ����
        	 * LUA_OK (0): �ɹ�
        	 * LUA_ERRRUN: ����ʱ����
        	 * LUA_ERRMEM: �ڴ������󡣶������ִ�Lua ������ô�������
        	 * LUA_ERRERR: �����д�������ʱ�����Ĵ���
        	 * LUA_ERRGCMM: ������ __gc Ԫ����ʱ�����Ĵ��� ���������ͱ����õĺ����޹ء���
        	 */

        	/*
        	 * void lua_call (lua_State *L, int nargs, int nresults)
        	 * ����һ������
        	 * Ҫ����һ����������ѭ����Э�飺
        	 * ���ȣ�Ҫ���õĺ���Ӧ�ñ�ѹ��ջ
        	 * ���ţ�����Ҫ���ݸ���������Ĳ���������ѹջ
        	 * ����ָ��һ����������ѹջ
        	 * ������һ�� lua_call
        	 * nargs ����ѹ��ջ�Ĳ�������
        	 * ������������Ϻ����еĲ����Լ������������ջ
        	 * �������ķ���ֵ��ʱ��ѹջ
        	 * ����ֵ�ĸ�����������Ϊ nresults ���� ���� nresults �����ó� LUA_MULTRET
        	 * ����������£����еķ���ֵ����ѹ���ջ��
        	 * Lua �ᱣ֤����ֵ������ջ�ռ���
        	 * ��������ֵ��������ѹջ����һ������ֵ����ѹջ���� ����ڵ��ý��������һ������ֵ��������ջ��
        	 * �����ú����ڷ����Ĵ��󽫣�ͨ�� longjmp ��һֱ����
        	 */
        	
        	// ��ֹ��������ʱ�����,����ѭ����
			LuaScriptCallGuard g(L);
        	// �Ѻ��������ջ���ƶ���top+1��λ��
        	// �������õ���Ҫ�Ǻ�����ѹջ,Ȼ���ٲ���ѹջ
        	// ջ��=>ջ�� error���� ���� n������
			lua_insert(L, top + 1);
			// top is err handler
        	// top��errpr����
        	// nresults==LUA_MULTRET�����еķ���ֵ����push��ջ
			// nresults != LUA_MULTRET������ֵ��������nresults������
			if (lua_pcallk(L, argn, LUA_MULTRET, top, NULL, NULL))
				// LUA_OK (0),�����������,�Ƿ����˴���
				// ����������
				lua_pop(L, 1);
        	// �Ƴ�error����
			lua_remove(L, top); // remove err handler;
		}
    	// ��������Ͳ�����������ջ��
    	// ���ط���ֵ����
        return lua_gettop(L)-top+1;
    }

    int LuaVar::pushArgByParms(UProperty* prop,uint8* parms) {
        auto L = getState();
        if (LuaObject::push(L,prop,parms,false))
            return prop->ElementSize;
        return 0;
    }

    bool LuaVar::callByUFunction(UFunction* func,uint8* parms, LuaVar* pSelf, FOutParmRec* OutParms) {
        
        if(!func) return false;

        if(!isValid()) {
            Log::Error("State of lua function is invalid");
            return false;
        }

    	// ��û�з���ֵ
        const bool bHasReturnParam = func->ReturnValueOffset != MAX_uint16;
    	// û�в�����û�з���ֵ��ʱ��,��ֱ�ӵ���
        if(func->ParmsSize==0 && !bHasReturnParam) {
			int nArg = 0;
        	// ��û��self
			if (pSelf) {
				pSelf->push();
				nArg++;
			}

			auto L = getState();
			int n = docall(nArg);
			lua_pop(L, n);
            return true;
        }
        // push self if valid
        int n=0;
		if (pSelf) {
			pSelf->push();
			n++;
		}
        // push arguments to lua state
    	// ��������ӵ�luastate��
        for(TFieldIterator<UProperty> it(func);it && (it->PropertyFlags&CPF_Parm);++it) {
            UProperty* prop = *it;
            uint64 propflag = prop->GetPropertyFlags();
        	// �������ֵ��out��������
            if((propflag&CPF_ReturnParm) || IsRealOutParam(propflag))
                continue;

            pushArgByParms(prop,parms+prop->GetOffset_ForInternal());
            n++;
        }
        
		auto L = getState();
        int retCount = docall(n);
		int remain = retCount;
        // if lua return value
        // we only handle first lua return value
        // �������ֵ��������0 && �з���ֵ
        // ������һ���з���ֵ,ȴû�и�����ֵ�����,Ӧ�ô���
        // C++ֻ֧��һ������ֵ,����out����
        // ������remain>0
        if(remain >0 && bHasReturnParam) {
        	// ��ȡ����ֵ����
            auto prop = func->GetReturnProperty();
        	// ����
            auto checkder = prop?LuaObject::getChecker(prop):nullptr;

        	/*
        	 * int lua_absindex (lua_State *L, int idx)
        	 * ��һ���ɽ��ܵ����� idx ת��Ϊ�������� ������һ��������ջ�����ĵ�ֵ��
        	 */
        	
            if (checkder) {
                (*checkder)(L, prop, parms+prop->GetOffset_ForInternal(), lua_absindex(L, -remain));
            }
			remain--;
        }

		// fill lua return value to blueprint stack if argument is out param
    	// �ռ�out����
		for (TFieldIterator<UProperty> it(func); remain >0 && it && (it->PropertyFlags&CPF_Parm); ++it) {
			UProperty* prop = *it;
			uint64 propflag = prop->GetPropertyFlags();
			// out����
			if (IsRealOutParam(propflag))
			{
				auto checkder = prop ? LuaObject::getChecker(prop) : nullptr;
				uint8* outPamams = OutParms ? OutParms->PropAddr : parms + prop->GetOffset_ForInternal();
				if (checkder) {
					// checkder��һ������ָ��,�������ڵ��ú���
					// ����ͬʱ,����ֵ
					(*checkder)(L, prop, outPamams, lua_absindex(L, -remain));
				}
				// ��һ��out����
				if(OutParms) OutParms = OutParms->NextOutParm;
				remain--;
			}
		}
        // pop returned value
    	// ������ֵ����
        lua_pop(L, retCount);
		return true;
    }

    // clone luavar
    void LuaVar::varClone(lua_var& tv,const lua_var& ov) const {
        switch(ov.luatype) {
        case LV_INT:
            tv.i = ov.i;
            break;
        case LV_BOOL:
            tv.b = ov.b;
            break;
        case LV_NUMBER:
            tv.d = ov.d;
            break;
        case LV_STRING:
            tv.s = ov.s;
        	// ���ü���+1
            tv.s->addRef();
            break;
        case LV_FUNCTION:
        case LV_TABLE:
        case LV_USERDATA:
            tv.ref = ov.ref;
        	// ���ü���+1
            tv.ref->addRef();
            break;
        case LV_LIGHTUD:
            tv.ptr = ov.ptr;
            break;
        // nil and tuple not need to clone
        case LV_NIL:
        case LV_TUPLE:
            break;
        }
        tv.luatype = ov.luatype;
    }

    void LuaVar::clone(const LuaVar& other) {
        stateIndex = other.stateIndex;
        numOfVar = other.numOfVar;
        if(numOfVar>0 && other.vars) {
            vars = new lua_var[numOfVar];
            for(size_t n=0;n<numOfVar;n++) {
                varClone( vars[n], other.vars[n] );
            }
        }
    }

    void LuaVar::move(LuaVar&& other) {
        stateIndex = other.stateIndex;
        numOfVar = other.numOfVar;
        vars = other.vars;

    	// Ĭ�������move�����ǲ�����ڴ�
    	// ����������ڴ�
        other.numOfVar = 0;
        other.vars = nullptr;
    }

    bool LuaVar::toProperty(UProperty* p,uint8* ptr) {

        auto func = LuaObject::getChecker(p);
        if(func) {
            // push var's value to top of stack
            auto L = getState();
            push(L);
            (*func)(L,p,ptr,lua_absindex(L,-1));
            lua_pop(L,1);
            return true;
        }
        
        return false;
    }
}

#ifdef _WIN32
#pragma warning (pop)
#endif
