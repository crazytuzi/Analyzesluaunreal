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
	// 各种类型的初始化
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

	// 自动识别类型
    LuaVar::LuaVar(lua_State* l,int p):LuaVar() {
        set(l,p);
    }

	// 自动识别类型
    void LuaVar::set(lua_State* l,int p) {
    	// 清除缓存
        free();

    	/*
    	 * int lua_type (lua_State *L, int index)
    	 * 返回给定有效索引处值的类型
    	 * 当索引无效（或无法访问）时则返回 LUA_TNONE
    	 * lua_type 返回的类型被编码为一些个在 lua.h 中定义的常量
    	 * LUA_TNIL， LUA_TNUMBER， LUA_TBOOLEAN， LUA_TSTRING
    	 * LUA_TTABLE， LUA_TFUNCTION， LUA_TUSERDATA， LUA_TTHREAD
    	 * LUA_TLIGHTUSERDATA
    	 */
    	
        int t = lua_type(l,p);
        LuaVar::Type type = LV_NIL;
        switch(t) {
            case LUA_TNUMBER:
                {
            		
            		/*
            		 * int lua_isinteger (lua_State *L, int index)
            		 * 当给定索引的值是一个整数
            		 * （其值是一个数字，且内部以整数储存）时，返回 1
            		 * 否则返回 0 。
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
    	// 初始化
        init(l,p,type);
    }

	// 手动指定类型
    LuaVar::LuaVar(lua_State* l,int p,LuaVar::Type type):LuaVar() {
        init(l,p,type);
    }

    // used to create number n of tuple
    // it used for return value from lua
    // don't call it to create n element of tuple
    // 用于收集返回值
    // 不要用于其他地方
    LuaVar::LuaVar(lua_State* l,size_t n):LuaVar() {
        init(l,n,LV_TUPLE);
    }

	// 获取当前类型所在上下文
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
        	 * 等价于调用 lua_tointegerx， 其参数 isnum 为 NULL
        	 */

        	/*
        	 * lua_Integer lua_tointegerx (lua_State *L, int index, int *isnum)
        	 * 将给定索引处的 Lua 值转换为带符号的整数类型 lua_Integer
        	 * 这个 Lua 值必须是一个整数，或是一个可以被转换为整数的数字或字符串
        	 * 否则，lua_tointegerx 返回 0
        	 * 如果 isnum 不是 NULL， *isnum 会被设为操作是否成功
        	 */
        	
            set(lua_tointeger(l,p));
            break;
        case LV_NUMBER:

        	/*
        	 * lua_Number lua_tonumber (lua_State *L, int index)
        	 * 等价于调用 lua_tonumberx， 其参数 isnum 为 NULL
        	 */

        	/*
        	 * lua_Number lua_tonumberx (lua_State *L, int index, int *isnum)
        	 * 把给定索引处的 Lua 值转换为 lua_Number 这样一个 C 类型
        	 * 这个 Lua 值必须是一个数字或是一个可转换为数字的字符串
        	 * 否则， lua_tonumberx 返回 0
        	 * 如果 isnum 不是 NULL， *isnum 会被设为操作是否成功
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
        	 * 把给定索引处的 Lua 值转换为一个 C 中的布尔量（ 0 或是 1 ）
        	 * 和 Lua 中做的所有测试一样， lua_toboolean 会把任何不同于 false 和 nil 的值当作真返回
        	 * 否则就返回假
        	 * （如果你想只接收真正的 boolean 值， 就需要使用 lua_isboolean 来测试值的类型
        	 */
        	
            set(!!lua_toboolean(l,p));
            break;
        case LV_LIGHTUD:
        	// 轻量用户数据 ，则指一个简单的 C 指针

        	/*
        	 * void *lua_touserdata (lua_State *L, int index)
        	 * 如果给定索引处的值是一个完全用户数据， 函数返回其内存块的地址
        	 * 如果值是一个轻量用户数据， 那么就返回它表示的指针
        	 * 否则，返回 NULL
        	 */
        	
            alloc(1);
            vars[0].ptr = lua_touserdata(l,p);
            vars[0].luatype = type;
            break;
        case LV_FUNCTION: 
        case LV_TABLE:
        case LV_USERDATA:
            alloc(1);
        	// 这里压栈的目的是为了
        	// RefRef构造函数中会调用luaL_ref
        	// 引用这个值
        	// 同时luaL_ref会弹出栈顶
        	// 所以这里push之后没有pop操作
            lua_pushvalue(l,p);
            vars[0].ref = new RefRef(l);
            vars[0].luatype=type;
            break;
        case LV_TUPLE:
        	// 元表,这个是自己定义的类型,不同于Lua元表概念
        	// 目的是为了收集返回值
            ensure(p>0 && lua_gettop(l)>=p);
            initTuple(l,p);
            break;
        default:
            break;
        }
    }

    void LuaVar::initTuple(lua_State* l,size_t n) {
    	// 确保栈顶个数大于n
        ensure(lua_gettop(l)>=n);
    	// 开辟空间
        alloc(n);
    	// 如果返回值是2个,调试测试lua_gettop(l)为2
    	// 故此时f为1
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

	// 析构函数里面释放
    LuaVar::~LuaVar() {
        free();
    }

    LuaVar::RefRef::RefRef(lua_State* l) 
        :LuaVar::Ref() 
    {
    	// 创建引用,最后会弹出栈顶对象
        ref=luaL_ref(l,LUA_REGISTRYINDEX);
        stateIndex = LuaState::get(l)->stateIndex();
    }

    LuaVar::RefRef::~RefRef() {
        if(LuaState::isValid(stateIndex)) {
            auto state = LuaState::get(stateIndex);
        	/*
        	 * void luaL_unref (lua_State *L, int t, int ref)
        	 * 释放索引 t 处表的 ref 引用对象
        	 * 此条目会从表中移除以让其引用的对象可被垃圾收集
        	 * 而引用 ref 也被回收再次使用
        	 * 如果 ref 为 LUA_NOREF 或 LUA_REFNIL， luaL_unref 什么也不做
        	 */

            luaL_unref(state->getLuaState(),LUA_REGISTRYINDEX,ref);
        }
    }

	// 释放
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
    	// 此时栈底=>栈顶 t
        push(L);
    	// 此时栈底=>栈顶 t key1
        key.push(L);

    	/*
    	 * int lua_next (lua_State *L, int index)
    	 * 从栈顶弹出一个键， 然后把索引指定的表中的一个键值对压栈
    	 * （弹出的键之后的 “下一” 对）
    	 * 如果表中以无更多元素， 那么 lua_next 将返回 0 （什么也不压栈）
    	 */

    	// lua_next成功后
    	// 先将key1弹出
    	// 然后将key1下一对键值对压栈key2,value2
		// 此时栈底=>栈顶 t key2,value2
        if(lua_next(L,-2)!=0) {
        	// 设置新的key
            key.set(L,-2);
        	// 设置新的value
            value.set(L,-1);
        	// 清空栈
            lua_pop(L,3);
            return true;
        }
        else {
            key.free();
            value.free();
        	// 因为lua_next已经将key1弹出,现在栈只剩下t
        	// 故弹出1个即可
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

	// 获取长度
	// 表调用lua_rawlen
	// 其他返回numOfVar
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
        if(isTable()) { // 表
            push(L); // push this table
            lua_geti(L,-1,index); // get by index
            LuaVar r(L,-1); // construct LuaVar
            lua_pop(L,2); // pop table and value
            return r;
        }
        else { // 元表
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
    	// 假定当前栈个数就是参数个数(假设不影响具体流程)
        auto L = getState();
        int top = lua_gettop(L); // top = argn
        top=top-argn+1; // top = 1
		// 栈底=>栈顶 n个参数 error函数
        LuaState::pushErrorHandler(L);
    	
    	/*
    	 * void lua_insert (lua_State *L, int index)
    	 * 把栈顶元素移动到指定的有效索引处， 依次移动这个索引之上的元素
    	 * 不要用伪索引来调用这个函数， 因为伪索引没有真正指向栈上的位置
    	 */

    	// 即把error函数行栈底
		// 栈底=>栈顶 error函数 n个参数
        lua_insert(L,top);
    	// 把函数指针压栈
    	// 栈底=>栈顶 error函数 n个参数 函数
        vars[0].ref->push(L);

		{
        	/*
        	 * int lua_pcallk (lua_State *L,
                int nargs,
                int nresults,
                int msgh,
                lua_KContext ctx,
                lua_KFunction k)
        	 * 这个函数的行为和 lua_pcall 完全一致，只不过它还允许被调用的函数让出
        	 */

        	/*
        	 * int lua_pcall (lua_State *L, int nargs, int nresults, int msgh)
        	 * 以保护模式调用一个函数
        	 * nargs 和 nresults 的含义与 lua_call 中的相同
        	 * 如果在调用过程中没有发生错误， lua_pcall 的行为和 lua_call 完全一致
        	 * 但是，如果有错误发生的话， lua_pcall 会捕获它， 然后把唯一的值（错误消息）压栈，然后返回错误码
        	 * 同 lua_call 一样， lua_pcall 总是把函数本身和它的参数从栈上移除
        	 * 如果 msgh 是 0 ， 返回在栈顶的错误消息就和原始错误消息完全一致
        	 * 否则， msgh 就被当成是 错误处理函数 在栈上的索引位置
        	 * 在当前的实现里，这个索引不能是伪索引
        	 * 在发生运行时错误时， 这个函数会被调用而参数就是错误消息
        	 * 错误处理函数的返回值将被 lua_pcall 作为错误消息返回在堆栈上
        	 * 典型的用法中，错误处理函数被用来给错误消息加上更多的调试信息， 比如栈跟踪信息
        	 * 这些信息在 lua_pcall 返回后， 由于栈已经展开，所以收集不到了
        	 * lua_pcall 函数会返回下列常数 （定义在 lua.h 内）中的一个：
        	 * LUA_OK (0): 成功
        	 * LUA_ERRRUN: 运行时错误
        	 * LUA_ERRMEM: 内存分配错误。对于这种错，Lua 不会调用错误处理函数
        	 * LUA_ERRERR: 在运行错误处理函数时发生的错误
        	 * LUA_ERRGCMM: 在运行 __gc 元方法时发生的错误。 （这个错误和被调用的函数无关。）
        	 */

        	/*
        	 * void lua_call (lua_State *L, int nargs, int nresults)
        	 * 调用一个函数
        	 * 要调用一个函数请遵循以下协议：
        	 * 首先，要调用的函数应该被压入栈
        	 * 接着，把需要传递给这个函数的参数按正序压栈
        	 * 这是指第一个参数首先压栈
        	 * 最后调用一下 lua_call
        	 * nargs 是你压入栈的参数个数
        	 * 当函数调用完毕后，所有的参数以及函数本身都会出栈
        	 * 而函数的返回值这时则被压栈
        	 * 返回值的个数将被调整为 nresults 个， 除非 nresults 被设置成 LUA_MULTRET
        	 * 在这种情况下，所有的返回值都被压入堆栈中
        	 * Lua 会保证返回值都放入栈空间中
        	 * 函数返回值将按正序压栈（第一个返回值首先压栈）， 因此在调用结束后，最后一个返回值将被放在栈顶
        	 * 被调用函数内发生的错误将（通过 longjmp ）一直上抛
        	 */
        	
        	// 防止函数调用时间过长,如死循环等
			LuaScriptCallGuard g(L);
        	// 把函数自身从栈顶移动到top+1的位置
        	// 函数调用的需要是函数先压栈,然后再参数压栈
        	// 栈底=>栈顶 error函数 函数 n个参数
			lua_insert(L, top + 1);
			// top is err handler
        	// top是errpr函数
        	// nresults==LUA_MULTRET，所有的返回值都会push进栈
			// nresults != LUA_MULTRET，返回值个数根据nresults来调整
			if (lua_pcallk(L, argn, LUA_MULTRET, top, NULL, NULL))
				// LUA_OK (0),即这种情况下,是发生了错误
				// 弹出错误码
				lua_pop(L, 1);
        	// 移除error函数
			lua_remove(L, top); // remove err handler;
		}
    	// 函数自身和参数都被弹出栈了
    	// 返回返回值个数
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

    	// 有没有返回值
        const bool bHasReturnParam = func->ReturnValueOffset != MAX_uint16;
    	// 没有参数和没有返回值的时候,就直接调用
        if(func->ParmsSize==0 && !bHasReturnParam) {
			int nArg = 0;
        	// 有没有self
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
    	// 将参数添加到luastate中
        for(TFieldIterator<UProperty> it(func);it && (it->PropertyFlags&CPF_Parm);++it) {
            UProperty* prop = *it;
            uint64 propflag = prop->GetPropertyFlags();
        	// 如果返回值和out参数跳过
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
        // 如果返回值个数大于0 && 有返回值
        // 这里少一种有返回值,却没有给返回值的情况,应该处理
        // C++只支持一个返回值,加上out参数
        // 故这里remain>0
        if(remain >0 && bHasReturnParam) {
        	// 获取返回值属性
            auto prop = func->GetReturnProperty();
        	// 检验
            auto checkder = prop?LuaObject::getChecker(prop):nullptr;

        	/*
        	 * int lua_absindex (lua_State *L, int idx)
        	 * 将一个可接受的索引 idx 转换为绝对索引 （即，一个不依赖栈顶在哪的值）
        	 */
        	
            if (checkder) {
                (*checkder)(L, prop, parms+prop->GetOffset_ForInternal(), lua_absindex(L, -remain));
            }
			remain--;
        }

		// fill lua return value to blueprint stack if argument is out param
    	// 收集out参数
		for (TFieldIterator<UProperty> it(func); remain >0 && it && (it->PropertyFlags&CPF_Parm); ++it) {
			UProperty* prop = *it;
			uint64 propflag = prop->GetPropertyFlags();
			// out参数
			if (IsRealOutParam(propflag))
			{
				auto checkder = prop ? LuaObject::getChecker(prop) : nullptr;
				uint8* outPamams = OutParms ? OutParms->PropAddr : parms + prop->GetOffset_ForInternal();
				if (checkder) {
					// checkder是一个函数指针,这里是在调用函数
					// 检查的同时,设置值
					(*checkder)(L, prop, outPamams, lua_absindex(L, -remain));
				}
				// 下一个out参数
				if(OutParms) OutParms = OutParms->NextOutParm;
				remain--;
			}
		}
        // pop returned value
    	// 将返回值弹出
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
        	// 引用计数+1
            tv.s->addRef();
            break;
        case LV_FUNCTION:
        case LV_TABLE:
        case LV_USERDATA:
            tv.ref = ov.ref;
        	// 引用计数+1
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

    	// 默认情况下move函数是不清空内存
    	// 这里清空了内存
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
