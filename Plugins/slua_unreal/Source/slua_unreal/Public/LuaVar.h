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
    	// ������������
        LuaVar(lua_Integer v);
        LuaVar(int v);
        LuaVar(size_t v);
        LuaVar(lua_Number v);
		LuaVar(const char* v);
		LuaVar(const char* v, size_t len);
        LuaVar(bool v);

        LuaVar(lua_State* L,int p);
        LuaVar(lua_State* L,int p,Type t);

    	// ���������Ϳ���
        LuaVar(const LuaVar& other):LuaVar() {
            clone(other);
        }

		// �ƶ����캯��
        LuaVar(LuaVar&& other):LuaVar() {
            move(std::move(other));
        }

    	// ������ֵ
        void operator=(const LuaVar& other) {
        	// ������ڵĻ���
            free();
            clone(other);
        }
    	
    	// �ƶ���ֵ
        void operator=(LuaVar&& other) {
			// ������ڵĻ���
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
    	// ��ջ���
		void free();

        // push luavar to lua state, 
        // if l is null, push luavar to L
        // ��ֵѹջ����Ӧ�Ļ���,�������Ϊ��,����ӵ�ȫ��
        // ����ѹջ����
        int push(lua_State *l=nullptr) const;

    	// �ж�����
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
    	// ��Ч�Լ��
        bool isValid() const;
    	// ��ȡ����
        Type type() const;

    	// ����ת��
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
        	 * �˺����� luaL_checkudata ����
        	 * �����ڲ���ʧ��ʱ�᷵�� NULL �������׳�����
        	 */

        	/*
        	 * void *luaL_checkudata (lua_State *L, int arg, const char *tname)
        	 * ��麯���ĵ� arg �������Ƿ���һ������Ϊ tname ���û�����
        	 * ���᷵�ظ��û����ݵĵ�ַ
        	 */
        	
            UserData<T*>* ud = reinterpret_cast<UserData<T*>*>(luaL_testudata(L, -1, t));
            lua_pop(L,1);
            return ud?ud->ud:nullptr;
        }

        // iterate LuaVar if it's table
        // return true if has next item
        // ��ĵ�����
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
        	 * �ṩ�� T ��ͬ�ĳ�Ա typedef type ����������� cv �޶������Ƴ�
        	 */
            R r = ArgOperatorOpt::readArg<typename remove_cr<R>::type>(L,-1);
            lua_pop(L,1);
            return std::move(r);
        }

		// Cast ���Ҹ�ֵ��target
		template<typename R>
		inline void castTo(R& target) {
			if (isValid())
			{
				target = castTo<R>();
			}
		}

        // return count of luavar if it's table or tuple, 
        // otherwise it's return 1
        // ���س���,���Ԫ���س���
        // ��������1
        size_t count() const;

        // get at element by index if luavar is table or tuple
    	// ���Ԫ��,ͨ���±��ȡֵ
        LuaVar getAt(size_t index) const;

        // template function for return value
        template<typename R>
        R getAt(size_t index) const {
            return getAt(index).castTo<R>();
        }

        // if pos==-1 setAt push var to back of table,
        // otherwise push var to given position at pos
        // ���ֵ������λ��
        // ���pos<=0 �����õ�ĩβ
        template<typename T>
        void setAt(T v,int pos=-1) {
            ensure(isTable());
            auto L = getState();
            push(L);

        	/*
        	 * size_t lua_rawlen (lua_State *L, int index)
        	 * ���ظ���������ֵ�Ĺ��С����ȡ�
        	 * �����ַ�������ָ�ַ����ĳ���
        	 * ���ڱ���ָ������Ԫ�����������ȡ���Ȳ�����'#'��Ӧ�õ���ֵ
        	 * �����û����ݣ���ָΪ���û����ݷ�����ڴ��Ĵ�С
        	 * ��������ֵ����Ϊ 0
        	 */
        	
            if(pos<=0) pos = lua_rawlen(L,-1)+1;
            LuaObject::push(L,v);
            lua_seti(L,-2,pos);
            lua_pop(L,1);
        }

        // get table by key, if luavar is table
    	// �ӱ��ж�ȡ����,�����Ƿ񴥷�Ԫ����
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
		// �ӱ��ж�ȡ����,����ֵ��ֵ����������,�����Ƿ񴥷�Ԫ����
		void getFromTable(T key, R& target) const {
			target = getFromTable<R>(key);
		}

        // set table by key and value
    	// ����ֵ,�ᴥ��Ԫ����
        template<typename K,typename V>
        void setToTable(K k,V v) {
            ensure(isTable());
            auto L = getState();
        	// �����push(L)ָ���ǽ�����push��L��
        	// ����ʱ��-3��Ӧ�ľ�������
            push(L);
            LuaObject::push(L,k);
            LuaObject::push(L,v);
            lua_settable(L,-3);
            lua_pop(L,1);
        }

        // call luavar if it's funciton
        // args is arguments passed to lua
        // �����һ������,���÷���
        template<class ...ARGS>
        LuaVar call(ARGS&& ...args) const {
        	// �����ж�
            if(!isFunction()) {
                Log::Error("LuaVar is not a function, can't be called");
                return LuaVar();
            }
        	// ��Ч�Լ��
            if(!isValid()) {
                Log::Error("State of lua function is invalid");
                return LuaVar();
            }
            auto L = getState();
        	// �ռ���������
            int n = pushArg(std::forward<ARGS>(args)...);
        	// call����
            int nret = docall(n);
        	// ����ֵ�ռ�
            auto ret = LuaVar::wrapReturn(L,nret);
        	// ��������ֵ
        	// docall�Ὣ��������,���Դ�ʱջֻʣ����ֵ
        	// �ʵ�������ֵ����
            lua_pop(L,nret);
            return ret;
        }

        template<class RET,class ...ARGS>
        RET call(ARGS&& ...args) const {
            LuaVar ret = call(std::forward<ARGS>(args)...);
            return ret.castTo<RET>();
        }

    	// ��������Ǳ�,Ȼ��call�����һ������
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
    	// call����,����n,nΪԤ��push�Ĳ�������
        inline LuaVar callWithNArg(int n) {
            auto L = getState();
            int nret = docall(n);
            auto ret = LuaVar::wrapReturn(L,nret);
            lua_pop(L,nret);
            return ret;
        }

		// ��Lua��ֵ���õ�Property
        bool toProperty(UProperty* p,uint8* ptr);
		// ����UFunction
        bool callByUFunction(UFunction* ufunc,uint8* parms,LuaVar* pSelf = nullptr,FOutParmRec* OutParms = nullptr);

		// get associate state
		lua_State* getState() const;
    private:
        friend class LuaState;

        // used to create number n of tuple
    	// ����n��Ԫ��
        LuaVar(lua_State* L,size_t n);

    	// ��ʼ������
        void init(lua_State* L,int p,Type t);
		// ��ʼ��Ԫ��
    	void initTuple(lua_State* L,size_t n);

    	// ���ռ�,ʵ��ָ�����鳤��
        void alloc(int n);

    	// ����
        struct Ref {
            Ref():refCount(1) {}
            virtual ~Ref() {}
            void addRef() {
				refCount++;
            }
            void release() {
                ensure(refCount >0);
            	// ÿ��--,����һ������,�����ô���Ϊ0��ʱ��,�ͷ�
                if(--refCount ==0) {
                    delete this;
                }
            }
            int refCount;
        };

    	// �����ַ���
    	// �ַ���ʹ����Ҫÿ�ο���һ����ͬ��С�Ŀռ�
        struct RefStr : public Ref {
			RefStr(const char* s, size_t len)
				:Ref()
            {
				if (len == 0) len = strlen(s);
				// alloc extra space for '\0'
				// Ϊ'\0' �࿪��һ���ռ�
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

    	// ���ø�������
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
        	// ������,�������ܴ��ڵ����Ͷ�Ӧ��ֵ
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

    	// ������������,�Բ���������������ͳ��
        template<class F,class ...ARGS>
        int pushArg(F f,ARGS&& ...args) const {
            auto L = getState();
            LuaObject::push(L,f);
            return 1+pushArg(std::forward<ARGS>(args)...);
        }

        int pushArg() const {
            return 0;
        }

    	// �ռ�����ֵ��һ����������
        static LuaVar wrapReturn(lua_State* L,int n) {
            ensure(n>=0);
            if(n==0)
                return LuaVar();
            else if (n==1)
                return LuaVar(L,-1);
            else
                return LuaVar(L,(size_t) n);
        }

    	// ��������
        int docall(int argn) const;
    	// ��UFunction��������LuaState
        int pushArgByParms(UProperty* prop,uint8* parms);

    	// �������캯��
        void clone(const LuaVar& other);
    	// �ƶ���������
        void move(LuaVar&& other);
    	// ֵ����
        void varClone(lua_var& tv,const lua_var& ov) const;
    	// ��ֵѹջ
        void pushVar(lua_State* l,const lua_var& ov) const;
    };

	// ����ֵ
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