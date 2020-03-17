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
#include "LuaObject.h"
#include "LuaCppBinding.h"
#include "Log.h"
#include <string>
#include <exception>
#include <utility>
#include <cstddef>

#ifdef _WIN32
#define strdup _strdup
#endif

namespace NS_SLUA {

    class SLUA_UNREAL_API LuaVar {
    public:
        enum Type {LV_NIL,LV_INT,LV_NUMBER,LV_BOOL,
            LV_STRING,LV_FUNCTION,LV_USERDATA,LV_LIGHTUD,LV_TABLE,LV_TUPLE};
        LuaVar();
        // copy construct for simple type
    	// 基本数据类型
        LuaVar(lua_Integer v);
        LuaVar(int v);
        LuaVar(size_t v);
        LuaVar(lua_Number v);
		LuaVar(const char* v);
		LuaVar(const char* v, size_t len);
        LuaVar(bool v);

        LuaVar(lua_State* L,int p);
        LuaVar(lua_State* L,int p,Type t);

    	// 从其他类型拷贝
        LuaVar(const LuaVar& other):LuaVar() {
            clone(other);
        }

		// 移动构造函数
        LuaVar(LuaVar&& other):LuaVar() {
            move(std::move(other));
        }

    	// 拷贝赋值
        void operator=(const LuaVar& other) {
        	// 清空现在的缓存
            free();
            clone(other);
        }
    	
    	// 移动赋值
        void operator=(LuaVar&& other) {
			// 清空现在的缓存
            free();
            move(std::move(other));
        }

        virtual ~LuaVar();

        void set(lua_State* L,int p);
        void set(lua_Integer v);
        void set(int v);
        void set(lua_Number v);
		void set(const char* v, size_t len);
		void set(const LuaLString& lstr);
        void set(bool b);
    	// 清空缓存
		void free();

        // push luavar to lua state, 
        // if l is null, push luavar to L
        // 将值压栈到对应的环境,如果参数为空,则添加到全局
        // 返回压栈个数
        int push(lua_State *l=nullptr) const;

    	// 判断类型
        bool isNil() const;
        bool isFunction() const;
        bool isTuple() const;
        bool isTable() const;
        bool isInt() const;
        bool isNumber() const;
        bool isString() const;
        bool isBool() const;
        bool isUserdata(const char* t) const;
        bool isLightUserdata() const;
    	// 有效性检测
        bool isValid() const;
    	// 获取类型
        Type type() const;

    	// 类型转换
        int asInt() const;
        int64 asInt64() const;
        float asFloat() const;
        double asDouble() const;
        const char* asString(size_t* outlen=nullptr) const;
		LuaLString asLString() const;
        bool asBool() const;
        void* asLightUD() const;
        template<typename T>
        T* asUserdata(const char* t) const {
            auto L = getState();
            push(L);
        	
        	/*
        	 * void *luaL_testudata (lua_State *L, int arg, const char *tname)
        	 * 此函数和 luaL_checkudata 类似
        	 * 但它在测试失败时会返回 NULL 而不是抛出错误
        	 */

        	/*
        	 * void *luaL_checkudata (lua_State *L, int arg, const char *tname)
        	 * 检查函数的第 arg 个参数是否是一个类型为 tname 的用户数据
        	 * 它会返回该用户数据的地址
        	 */
        	
            UserData<T*>* ud = reinterpret_cast<UserData<T*>*>(luaL_testudata(L, -1, t));
            lua_pop(L,1);
            return ud?ud->ud:nullptr;
        }

        // iterate LuaVar if it's table
        // return true if has next item
        // 表的迭代器
        bool next(LuaVar& key,LuaVar& value);

        // return desc string for luavar, call luaL_tolstring
        const char* toString();

		// Cast
        template<class R>
        inline R castTo() {
            auto L = getState();
            push(L);

        	/*
        	 * remove_cr
        	 * 提供与 T 相同的成员 typedef type ，除了其最顶层 cv 限定符被移除
        	 */
            R r = ArgOperatorOpt::readArg<typename remove_cr<R>::type>(L,-1);
            lua_pop(L,1);
            return std::move(r);
        }

		// Cast 并且赋值给target
		template<typename R>
		inline void castTo(R& target) {
			if (isValid())
			{
				target = castTo<R>();
			}
		}

        // return count of luavar if it's table or tuple, 
        // otherwise it's return 1
        // 返回长度,表和元表返回长度
        // 其他返回1
        size_t count() const;

        // get at element by index if luavar is table or tuple
    	// 表和元表,通过下标获取值
        LuaVar getAt(size_t index) const;

        // template function for return value
        template<typename R>
        R getAt(size_t index) const {
            return getAt(index).castTo<R>();
        }

        // if pos==-1 setAt push var to back of table,
        // otherwise push var to given position at pos
        // 如果值到给定位置
        // 如果pos<=0 则设置到末尾
        template<typename T>
        void setAt(T v,int pos=-1) {
            ensure(isTable());
            auto L = getState();
            push(L);

        	/*
        	 * size_t lua_rawlen (lua_State *L, int index)
        	 * 返回给定索引处值的固有“长度”
        	 * 对于字符串，它指字符串的长度
        	 * 对于表；它指不触发元方法的情况下取长度操作（'#'）应得到的值
        	 * 对于用户数据，它指为该用户数据分配的内存块的大小
        	 * 对于其它值，它为 0
        	 */
        	
            if(pos<=0) pos = lua_rawlen(L,-1)+1;
            LuaObject::push(L,v);
            lua_seti(L,-2,pos);
            lua_pop(L,1);
        }

        // get table by key, if luavar is table
    	// 从表中读取数据,参数是否触发元方法
        template<typename R,typename T>
        R getFromTable(T key,bool rawget=false) const {
            ensure(isTable());
            auto L = getState();
			if (!L) return R();
            AutoStack as(L);
            push(L);
            LuaObject::push(L,key);
			if (rawget) lua_rawget(L, -2);
			else lua_gettable(L,-2);
            return ArgOperatorOpt::readArg<typename remove_cr<R>::type>(L,-1);
        }

		template<typename R, typename T>
		// 从表中读取数据,并将值赋值给传入引用,参数是否触发元方法
		void getFromTable(T key, R& target) const {
			target = getFromTable<R>(key);
		}

        // set table by key and value
    	// 设置值,会触发元方法
        template<typename K,typename V>
        void setToTable(K k,V v) {
            ensure(isTable());
            auto L = getState();
        	// 这里的push(L)指的是将自身push到L中
        	// 即此时的-3对应的就是自身
            push(L);
            LuaObject::push(L,k);
            LuaObject::push(L,v);
            lua_settable(L,-3);
            lua_pop(L,1);
        }

        // call luavar if it's funciton
        // args is arguments passed to lua
        // 如果是一个函数,调用方法
        template<class ...ARGS>
        LuaVar call(ARGS&& ...args) const {
        	// 类型判断
            if(!isFunction()) {
                Log::Error("LuaVar is not a function, can't be called");
                return LuaVar();
            }
        	// 有效性检查
            if(!isValid()) {
                Log::Error("State of lua function is invalid");
                return LuaVar();
            }
            auto L = getState();
        	// 收集参数个数
            int n = pushArg(std::forward<ARGS>(args)...);
        	// call函数
            int nret = docall(n);
        	// 返回值收集
            auto ret = LuaVar::wrapReturn(L,nret);
        	// 弹出返回值
        	// docall会将参数弹出,所以此时栈只剩返回值
        	// 故弹出返回值即可
            lua_pop(L,nret);
            return ret;
        }

        template<class RET,class ...ARGS>
        RET call(ARGS&& ...args) const {
            LuaVar ret = call(std::forward<ARGS>(args)...);
            return ret.castTo<RET>();
        }

    	// 如果自身是表,然后call下面的一个函数
    	// a.b(...)
		template<class ...ARGS>
		LuaVar callField(const char* field, ARGS&& ...args) const {
			if (!isTable()) {
				Log::Error("LuaVar is not a table, can't call field");
				return LuaVar();
			}
			if (!isValid()) {
				Log::Error("State of lua function is invalid");
				return LuaVar();
			}
			LuaVar ret = getFromTable<LuaVar>(field);
			return ret.call(std::forward<ARGS>(args)...);
		}

        // call function with pre-pushed n args
    	// call函数,传入n,n为预先push的参数个数
        inline LuaVar callWithNArg(int n) {
            auto L = getState();
            int nret = docall(n);
            auto ret = LuaVar::wrapReturn(L,nret);
            lua_pop(L,nret);
            return ret;
        }

		// 将Lua的值设置到Property
        bool toProperty(UProperty* p,uint8* ptr);
		// 调用UFunction
        bool callByUFunction(UFunction* ufunc,uint8* parms,LuaVar* pSelf = nullptr,FOutParmRec* OutParms = nullptr);

		// get associate state
		lua_State* getState() const;
    private:
        friend class LuaState;

        // used to create number n of tuple
    	// 创建n个元表
        LuaVar(lua_State* L,size_t n);

    	// 初始化自身
        void init(lua_State* L,int p,Type t);
		// 初始化元表
    	void initTuple(lua_State* L,size_t n);

    	// 开空间,实质指定数组长度
        void alloc(int n);

    	// 引用
        struct Ref {
            Ref():refCount(1) {}
            virtual ~Ref() {}
            void addRef() {
				refCount++;
            }
            void release() {
                ensure(refCount >0);
            	// 每次--,减少一次引用,当引用次数为0的时候,释放
                if(--refCount ==0) {
                    delete this;
                }
            }
            int refCount;
        };

    	// 引用字符串
    	// 字符串使用需要每次开辟一段相同大小的空间
        struct RefStr : public Ref {
			RefStr(const char* s, size_t len)
				:Ref()
            {
				if (len == 0) len = strlen(s);
				// alloc extra space for '\0'
				// 为'\0' 多开辟一个空间
				buf = (char*) FMemory::Malloc(len+1);
				FMemory::Memcpy(buf, s, len);
				buf[len] = 0;
				length = len;
			}
            virtual ~RefStr() {
                FMemory::Free(buf);
            }
            char* buf;
			size_t length;
        };

    	// 引用复杂类型
        struct RefRef: public Ref {
            RefRef(lua_State* l);
            virtual ~RefRef();
            bool isValid() {
                return ref != LUA_NOREF;
            }
            void push(lua_State* l) {
                lua_geti(l,LUA_REGISTRYINDEX,ref);
            }
            int ref;
            int stateIndex;
        };

        int stateIndex;

        typedef struct {
        	// 联合体,包含可能存在的类型对应的值
            union {
                RefRef* ref;
                lua_Integer i;
                lua_Number d;
                RefStr* s;
                void* ptr;
                bool b;
            };
            Type luatype;
        } lua_var;

        lua_var* vars;
        size_t numOfVar;

    	// 迭代不定参数,对不定参数个数进行统计
        template<class F,class ...ARGS>
        int pushArg(F f,ARGS&& ...args) const {
            auto L = getState();
            LuaObject::push(L,f);
            return 1+pushArg(std::forward<ARGS>(args)...);
        }

        int pushArg() const {
            return 0;
        }

    	// 收集返回值到一个变量里面
        static LuaVar wrapReturn(lua_State* L,int n) {
            ensure(n>=0);
            if(n==0)
                return LuaVar();
            else if (n==1)
                return LuaVar(L,-1);
            else
                return LuaVar(L,(size_t) n);
        }

    	// 函数调用
        int docall(int argn) const;
    	// 把UFunction参数传入LuaState
        int pushArgByParms(UProperty* prop,uint8* parms);

    	// 拷贝构造函数
        void clone(const LuaVar& other);
    	// 移动拷贝函数
        void move(LuaVar&& other);
    	// 值拷贝
        void varClone(lua_var& tv,const lua_var& ov) const;
    	// 将值压栈
        void pushVar(lua_State* l,const lua_var& ov) const;
    };

	// 设置值
    template<>
    inline LuaVar LuaObject::checkValue(lua_State* L, int p) {
        return LuaVar(L,p);
    }

	template<>
	inline void LuaVar::castTo()
	{
		return;
	}
    
}