// Tencent is pleased to support the open source community by making sluaunreal available.

// Copyright (C) 2018 THL A29 Limited, a Tencent company. All rights reserved.
// Licensed under the BSD 3-Clause License (the "License"); 
// you may not use this file except in compliance with the License. You may obtain a copy of the License at

// https://opensource.org/licenses/BSD-3-Clause

// Unless required by applicable law or agreed to in writing, 
// software distributed under the License is distributed on an "AS IS" BASIS, 
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. 
// See the License for the specific language governing permissions and limitations under the License.

#include "LuaReference.h"

namespace NS_SLUA {
	namespace LuaReference {

		void addRefByStruct(FReferenceCollector& collector, UStruct* us, void* base, bool container) {
			// 遍历UProperty
			for (TFieldIterator<const UProperty> it(us); it; ++it)
				addRefByProperty(collector, *it, base, container);
		}

		void addRefByDelegate(FReferenceCollector& collector, const FScriptDelegate&, bool container = true) {
			// TODO
		}

		void addRefByMulticastDelegate(FReferenceCollector& collector, const FMulticastScriptDelegate&, bool container = true) {
			// TODO
		}


		bool addRef(const USetProperty* p, void* base, FReferenceCollector& collector, bool container = true)
		{
			bool ret = false;
			// ArrayDim Property维度, 例如: int  a, 维度1;  int  a[20], 维度20
			for (int32 n = 0; n < p->ArrayDim; ++n)
			{
				bool valuesChanged = false;
				FScriptSetHelper helper(p, p->ContainerPtrToValuePtr<void>(base, n));

				for (int32 index = 0; index < helper.GetMaxIndex(); ++index)
				{
					if (helper.IsValidIndex(index))
					{
						valuesChanged |= addRefByProperty(collector,
							helper.GetElementProperty(), helper.GetElementPtr(index));
					}
				}

				if (valuesChanged)
				{
					ret = true;
					// 重新散列
					helper.Rehash();
				}
			}
			return ret;
		}

		bool addRef(const UMapProperty* p, void* base, FReferenceCollector& collector, bool container = true)
		{
			bool ret = false;
			for (int n = 0; n < p->ArrayDim; ++n)
			{
				bool keyChanged = false;
				bool valuesChanged = false;
				FScriptMapHelper helper(p, p->ContainerPtrToValuePtr<void>(base, n));

				for (int index = 0; index < helper.GetMaxIndex(); ++index)
				{
					if (helper.IsValidIndex(index))
					{
						keyChanged |= addRefByProperty(collector, helper.GetKeyProperty(), helper.GetKeyPtr(index));
						valuesChanged |= addRefByProperty(collector, helper.GetValueProperty(), helper.GetValuePtr(index));
					}
				}

				if (keyChanged || valuesChanged)
				{
					ret = true;
					if (keyChanged)
					{
						helper.Rehash();
					}
				}
			}
			return ret;
		}

		bool addRef(const UObjectProperty* p, void* base, FReferenceCollector &collector, bool container = true)
		{
			bool ret = false;
			for (int n = 0; n < p->ArrayDim; ++n)
			{
				void* value = container?p->ContainerPtrToValuePtr<void>(base, n):base;
				UObject* obj = p->GetObjectPropertyValue(value);
				if (obj && obj->IsValidLowLevel())
				{
					UObject* newobj = obj;
					// 添加到引用,引用传参,里面会改变这个值
					collector.AddReferencedObject(newobj);

					// 如果这个值与传入的不一样,那当前对象已经被改变了,需要重新赋值
					// 并且返回true,如果外面是Set或者Map,需要Rehash
					if (newobj != obj)
					{
						ret = true;
						p->SetObjectPropertyValue(value, newobj);
					}
				}
			}
			return ret;
		}

		bool addRef(const UArrayProperty* p, void* base, FReferenceCollector& collector, bool container = true)
		{
			bool ret = false;
			for (int n = 0; n < p->ArrayDim; ++n)
			{
				FScriptArrayHelper helper(p, p->ContainerPtrToValuePtr<void>(base, n));
				// 遍历数组
				for (int32 index = 0; index < helper.Num(); ++index)
				{
					ret |= addRefByProperty(collector, p->Inner, helper.GetRawPtr(index));
				}
			}
			return ret;
		}

		// 多播 TODO
		bool addRef(const UMulticastDelegateProperty* p, void* base, FReferenceCollector& collector, bool container = true)
		{
			for (int n = 0; n < p->ArrayDim; ++n)
			{
// #if (ENGINE_MINOR_VERSION>=23) && (ENGINE_MAJOR_VERSION>=4)
#if true
				FMulticastScriptDelegate* Value = const_cast<FMulticastScriptDelegate*>(p->GetMulticastDelegate(container ? p->ContainerPtrToValuePtr<void>(base, n) : base));
#else
				FMulticastScriptDelegate* Value = p->GetPropertyValuePtr(container?p->ContainerPtrToValuePtr<void>(base, n):base);
#endif
				addRefByMulticastDelegate(collector, *Value);
			}
			return false;
		}

		bool addRef(const UStructProperty* p, void* base, FReferenceCollector& collector, bool container = true)
		{
			for (int n = 0; n < p->ArrayDim; ++n) {
				addRefByStruct(collector, p->Struct, container?p->ContainerPtrToValuePtr<void>(base, n):base);
			}
			return false;
		}

		// 单播 TODO
		bool addRef(const UDelegateProperty* p, void* base, FReferenceCollector& collector, bool container = true)
		{
			for (int n = 0; n < p->ArrayDim; ++n) {
				FScriptDelegate* value = p->GetPropertyValuePtr(container?p->ContainerPtrToValuePtr<void>(base, n):base);
				addRefByDelegate(collector, *value);
			}
			return false;
		}

		bool addRefByProperty(FReferenceCollector& collector, const UProperty* prop, void* base, bool container) {
			// 不同类型,添加引用方式不一样
			if (auto p = Cast<UObjectProperty>(prop)) {
				return addRef(p, base, collector, container);
			}
			if (auto p = Cast<UArrayProperty>(prop))
			{
				return addRef(p, base, collector, container);
			}
			if (auto p = Cast<UStructProperty>(prop)) {
				return addRef(p, base, collector, container);
			}
			if (auto p = Cast<UDelegateProperty>(prop)) {
				return addRef(p, base, collector, container);
			}
			if (auto p = Cast<UMapProperty>(prop))
			{
				return addRef(p, base, collector, container);
			}
			if (auto p = Cast<USetProperty>(prop))
			{
				return addRef(p, base, collector, container);
			}
			if (auto p = Cast<UMulticastDelegateProperty>(prop))
			{
				return addRef(p, base, collector, container);
			}
			return false;
		}
	}
}