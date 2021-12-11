/*
 * Copyright (c) 2021 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "distributed_objectstore.h"
#include "native_api.h"
#include "native_node_api.h"
#include "objectstore_errors.h"
#include <js_object_wrapper.h>
#include <js_distributedobject.h>
#include <js_distributedobjectstore.h>


namespace OHOS::ObjectStore {
constexpr size_t SESSION_ID_SIZE = 32;
constexpr size_t TYPE_SIZE = 10;
struct DistributedObjectContext {
    DistributedObjectStore *objectInfo = nullptr;
};
struct CreateObjectAsyncContext {
    napi_env env = nullptr;
    napi_async_work work = nullptr;
    napi_deferred deferred = nullptr;
    napi_ref callbackRef = nullptr;
    char sessionId[SESSION_ID_SIZE] = { 0 };
    size_t sessionIdLen = 0;
    int32_t status = 0;
    DistributedObjectStore *objectStore;
    DistributedObject *object;

};
napi_value JSDistributedObjectStore::JSConstructor(napi_env env, napi_callback_info info)
{
    napi_value thisVar = nullptr;
    void* data = nullptr;
    napi_get_cb_info(env, info, nullptr, nullptr, &thisVar, &data);

    DistributedObjectStore *objectInfo = DistributedObjectStore::GetInstance();
    DistributedObjectContext *context = new DistributedObjectContext();
    context->objectInfo = objectInfo;
    napi_wrap(
            env, thisVar, context,
            [](napi_env env, void* data, void* hint) {
                auto context = (DistributedObjectContext*)data;
                if (context != nullptr) {
                    delete context;
                }
            },
            nullptr, nullptr);

    return thisVar;
}

// function createObject(sessionId: string, callback: AsyncCallback<DistributedObject>): void;
// function createObject(sessionId: string): Promise<DistributedObject>;
napi_value JSDistributedObjectStore::JSCreateObject(napi_env env, napi_callback_info info)
{
    size_t requireArgc = 1;
    size_t argc = 3;
    napi_value argv[3] = { 0 };
    napi_value thisVar = nullptr;
    void* data = nullptr;
    napi_get_cb_info(env, info, &argc, argv, &thisVar, &data);
    NAPI_ASSERT(env, argc >= requireArgc, "requires 1 parameter");
    auto asyncContext = new CreateObjectAsyncContext();
    asyncContext->env = env;

    for (size_t i = 0; i < argc; i++) {
        napi_valuetype valueType = napi_undefined;
        napi_typeof(env, argv[i], &valueType);
        if ((i == 0) && (valueType == napi_string)) {
            napi_get_value_string_utf8(env, argv[i], asyncContext->sessionId, SESSION_ID_SIZE, &asyncContext->sessionIdLen);
        } else if (valueType == napi_function) {
            napi_create_reference(env, argv[i], 1, &asyncContext->callbackRef);
            break;
        } else {
            NAPI_ASSERT(env, false, "type mismatch");
        }
    }

    napi_value result = nullptr;
    if (asyncContext->callbackRef == nullptr) {
        napi_create_promise(env, &asyncContext->deferred, &result);
    } else {
        napi_get_undefined(env, &result);
    }
    DistributedObjectContext *context = nullptr;
    napi_unwrap(env, thisVar, (void**)&context);
    NAPI_ASSERT(env, context != nullptr, "context is null");
    asyncContext->objectStore = context->objectInfo;
    napi_value resource = nullptr;
    napi_create_string_utf8(env, "JSCreateObject", NAPI_AUTO_LENGTH, &resource);

    napi_create_async_work(
            env, nullptr, resource,
            [](napi_env env, void* data) {
                CreateObjectAsyncContext* asyncContext = (CreateObjectAsyncContext*)data;
                if (asyncContext->objectStore == nullptr) {
                    asyncContext->status = ERR_STORE_MANAGER_NULL;
                    return;
                }
                asyncContext->object = asyncContext->objectStore->CreateObject(asyncContext->sessionId);
                asyncContext->status = asyncContext->object != nullptr ? SUCCESS : ERR_CREATE_FAIL;
            },
            [](napi_env env, napi_status status, void* data) {
                CreateObjectAsyncContext* asyncContext = (CreateObjectAsyncContext*)data;
                napi_value result[2] = { 0 };
                napi_create_int32(env, asyncContext->status, &result[0]);
                if (asyncContext->status == SUCCESS) {
                    JSObjectWrapper *objectWrapper = new JSObjectWrapper(asyncContext->objectStore, asyncContext->object);
                    napi_wrap( env, result[1], objectWrapper,
                               [](napi_env env, void* data, void* hint) {
                                   auto objectWrapper = (JSObjectWrapper*)data;
                                   if (objectWrapper != nullptr) {
                                       delete objectWrapper;
                                   }
                               },
                               nullptr, nullptr);
                } else {
                    napi_get_undefined(env, &result[1]);
                    // todo callback create fail
                }
                if (asyncContext->deferred) {
                    if (asyncContext->status == SUCCESS) {
                        napi_resolve_deferred(env, asyncContext->deferred, result[1]);
                    } else {
                        napi_reject_deferred(env, asyncContext->deferred, result[0]);
                    }
                } else {
                    napi_value callback = nullptr;
                    napi_get_reference_value(env, asyncContext->callbackRef, &callback);
                    napi_call_function(env, nullptr, callback, sizeof(result) / sizeof(result[0]), result, nullptr);
                    napi_delete_reference(env, asyncContext->callbackRef);
                }
                napi_delete_async_work(env, asyncContext->work);
                delete asyncContext;
            },
            (void*)asyncContext, &asyncContext->work);
    napi_queue_async_work(env, asyncContext->work);
    return result;
}

//function createObjectSync(sessionId: string): DistributedObject;
napi_value JSDistributedObjectStore::JSCreateObjectSync(napi_env env, napi_callback_info info)
{
    size_t requireArgc = 1;
    size_t argc = 2;
    napi_value argv[2] = { 0 };
    napi_value thisVar = nullptr;
    void* data = nullptr;
    char sessionId[SESSION_ID_SIZE] = { 0 };
    size_t sessionIdLen = 0;
    napi_get_cb_info(env, info, &argc, argv, &thisVar, &data);

    NAPI_ASSERT(env, argc >= requireArgc, "requires 1 parameter");
    for (size_t i = 0; i < argc; i++) {
        napi_valuetype valueType = napi_undefined;
        napi_typeof(env, argv[i], &valueType);

        if (i == 0 && valueType == napi_string) {
            napi_get_value_string_utf8(env, argv[i], sessionId, SESSION_ID_SIZE, &sessionIdLen);
        } else {
            NAPI_ASSERT(env, false, "type mismatch");
        }
    }
    DistributedObjectContext *context = nullptr;
    napi_unwrap(env, thisVar, (void**)&context);
    NAPI_ASSERT(env, context != nullptr, "context is null");
    DistributedObjectStore* objectInfo = context->objectInfo;
    NAPI_ASSERT(env, objectInfo != nullptr, "objectInfo not null");
    DistributedObject *object = objectInfo->CreateObject(sessionId);
    napi_value result = nullptr;
    if (object != nullptr) {
        JSObjectWrapper *objectWrapper = new JSObjectWrapper(objectInfo, object);
        napi_wrap( env, result, objectWrapper,
                   [](napi_env env, void* data, void* hint) {
                       auto objectWrapper = (JSObjectWrapper*)data;
                       if (objectWrapper != nullptr) {
                           delete objectWrapper;
                       }
                   },
                   nullptr, nullptr);
    } else {
        // create fail
        //todo callback
        napi_get_undefined(env, &result);
    }
    return result;
}

// function destroyObject(sessionId: string, callback: AsyncCallback<void>): void;
// function destroyObject(sessionId: string): Promise<void>;
napi_value JSDistributedObjectStore::JSDestroyObject(napi_env env, napi_callback_info info)
{
    size_t requireArgc = 1;
    size_t argc = 3;
    napi_value argv[3] = { 0 };
    napi_value thisVar = nullptr;
    void* data = nullptr;
    napi_get_cb_info(env, info, &argc, argv, &thisVar, &data);
    NAPI_ASSERT(env, argc >= requireArgc, "requires 1 parameter");
    auto asyncContext = new CreateObjectAsyncContext();
    asyncContext->env = env;

    for (size_t i = 0; i < argc; i++) {
        napi_valuetype valueType = napi_undefined;
        napi_typeof(env, argv[i], &valueType);
        if ((i == 0) && (valueType == napi_string)) {
            napi_get_value_string_utf8(env, argv[i], asyncContext->sessionId, SESSION_ID_SIZE, &asyncContext->sessionIdLen);
        } else if (valueType == napi_function) {
            napi_create_reference(env, argv[i], 1, &asyncContext->callbackRef);
            break;
        } else {
            NAPI_ASSERT(env, false, "type mismatch");
        }
    }

    napi_value result = nullptr;
    if (asyncContext->callbackRef == nullptr) {
        napi_create_promise(env, &asyncContext->deferred, &result);
    } else {
        napi_get_undefined(env, &result);
    }
    DistributedObjectContext *context = nullptr;
    napi_unwrap(env, thisVar, (void**)&context);
    NAPI_ASSERT(env, context != nullptr, "context is null");
    asyncContext->objectStore = context->objectInfo;
    napi_value resource = nullptr;
    napi_create_string_utf8(env, "JSDestroyObject", NAPI_AUTO_LENGTH, &resource);

    napi_create_async_work(
            env, nullptr, resource,
            [](napi_env env, void* data) {
                CreateObjectAsyncContext* asyncContext = (CreateObjectAsyncContext*)data;
                if (asyncContext->objectStore == nullptr) {
                    asyncContext->status = ERR_STORE_MANAGER_NULL;
                    return;
                }
                asyncContext->status = asyncContext->objectStore->DeleteObject(asyncContext->sessionId);
            },
            [](napi_env env, napi_status status, void* data) {
                CreateObjectAsyncContext* asyncContext = (CreateObjectAsyncContext*)data;
                napi_value result;
                napi_create_int32(env, asyncContext->status, &result);
                if (asyncContext->deferred) {
                    if (asyncContext->status == SUCCESS) {
                        napi_resolve_deferred(env, asyncContext->deferred, result);
                    } else {
                        napi_reject_deferred(env, asyncContext->deferred, result);
                    }
                } else {
                    napi_value callback = nullptr;
                    napi_get_reference_value(env, asyncContext->callbackRef, &callback);
                    napi_call_function(env, nullptr, callback, 1, &result, nullptr);
                    napi_delete_reference(env, asyncContext->callbackRef);
                }
                napi_delete_async_work(env, asyncContext->work);
                delete asyncContext;
            },
            (void*)asyncContext, &asyncContext->work);
    napi_queue_async_work(env, asyncContext->work);
    return result;
}

//function destroyObjectSync(sessionId: string): number;
napi_value JSDistributedObjectStore::JSDestroyObjectSync(napi_env env, napi_callback_info info)
{
    size_t requireArgc = 1;
    size_t argc = 2;
    napi_value argv[2] = { 0 };
    napi_value thisVar = nullptr;
    void* data = nullptr;
    char sessionId[SESSION_ID_SIZE] = { 0 };
    size_t sessionIdLen = 0;
    napi_get_cb_info(env, info, &argc, argv, &thisVar, &data);

    NAPI_ASSERT(env, argc >= requireArgc, "requires 1 parameter");
    for (size_t i = 0; i < argc; i++) {
        napi_valuetype valueType = napi_undefined;
        napi_typeof(env, argv[i], &valueType);

        if (i == 0 && valueType == napi_string) {
            napi_get_value_string_utf8(env, argv[i], sessionId, SESSION_ID_SIZE, &sessionIdLen);
        } else {
            NAPI_ASSERT(env, false, "type mismatch");
        }
    }
    DistributedObjectContext *context = nullptr;
    napi_unwrap(env, thisVar, (void**)&context);
    NAPI_ASSERT(env, context != nullptr, "context is null");
    DistributedObjectStore* objectInfo = context->objectInfo;
    NAPI_ASSERT(env, objectInfo != nullptr, "objectInfo not null");
    uint32_t ret = objectInfo->DeleteObject(sessionId);
    napi_value result = nullptr;
    napi_create_int32(env, ret, &result);
    return result;
}

//function sync(object_: DistributedObject): number;
napi_value JSDistributedObjectStore::JSSync(napi_env env, napi_callback_info info)
{
    size_t requireArgc = 1;
    size_t argc = 2;
    napi_value argv[2] = { 0 };
    napi_value thisVar = nullptr;
    void* data = nullptr;
    JSObjectWrapper *objectWrapper = nullptr;;
    napi_get_cb_info(env, info, &argc, argv, &thisVar, &data);

    NAPI_ASSERT(env, argc >= requireArgc, "requires 1 parameter");
    napi_unwrap(env, argv[0], (void**)&objectWrapper);
    NAPI_ASSERT(env, objectWrapper != nullptr, "objectWrapper not null");
    DistributedObjectContext *context = nullptr;
    napi_unwrap(env, thisVar, (void**)&context);
    NAPI_ASSERT(env, context != nullptr, "context is null");
    DistributedObjectStore *objectInfo = context->objectInfo;
    NAPI_ASSERT(env, objectInfo != nullptr, "objectInfo not null");
    uint32_t ret = objectInfo->Sync(objectWrapper->GetObject());
    napi_value result = nullptr;
    napi_create_int32(env, ret, &result);
    return result;
}

// function on(type: 'change', object: DistributedObject, callback: Callback<ChangedDataObserver>): void;
// function on(type: 'status', object: DistributedObject, callback: Callback<ObjectStatusObserver>): void;
napi_value JSDistributedObjectStore::JSOn(napi_env env, napi_callback_info info)
{
    size_t requireArgc = 3;
    size_t argc = 0;
    napi_value argv[3] = { 0 };
    napi_value thisVar = nullptr;
    void* data = nullptr;
    napi_get_cb_info(env, info, &argc, argv, &thisVar, &data);

    NAPI_ASSERT(env, argc >= requireArgc, "requires 3 parameters");

    char type[TYPE_SIZE] = { 0 };
    size_t eventTypeLen = 0;
    napi_valuetype eventValueType = napi_undefined;
    napi_typeof(env, argv[0], &eventValueType);
    NAPI_ASSERT(env, eventValueType == napi_string, "parameter 1 type mismatch");
    napi_get_value_string_utf8(env, argv[0], type, TYPE_SIZE, &eventTypeLen);

    napi_valuetype objectType = napi_undefined;
    napi_typeof(env, argv[1], &objectType);
    NAPI_ASSERT(env, objectType == napi_object, "parameter 2 type mismatch");

    napi_valuetype callbackType = napi_undefined;
    napi_typeof(env, argv[2], &callbackType);
    NAPI_ASSERT(env, callbackType == napi_function, "parameter 3 type mismatch");

    JSObjectWrapper *wrapper = nullptr;
    napi_unwrap(env, argv[1], (void**)&wrapper);
    NAPI_ASSERT(env, wrapper != nullptr, "object wrapper is null");
    wrapper->AddWatch(env, type, argv[1]);
    napi_value result = nullptr;
    napi_get_undefined(env, &result);
    return result;
}
// function off(type: 'change', object: DistributedObject, callback?: Callback<ChangedDataObserver>): void;
// function off(type: 'status', object: DistributedObject, callback?: Callback<ObjectStatusObserver>): void;
napi_value JSDistributedObjectStore::JSOff(napi_env env, napi_callback_info info)
{
    size_t requireArgc = 2;
    size_t argc = 0;
    napi_value argv[2] = { 0 };
    napi_value thisVar = nullptr;
    void* data = nullptr;
    char type[TYPE_SIZE] = { 0 };
    size_t typeLen = 0;
    napi_get_cb_info(env, info, &argc, argv, &thisVar, &data);

    NAPI_ASSERT(env, argc >= requireArgc, "requires 1 parameter");
    for (size_t i = 0; i < argc; i++) {
        napi_valuetype valueType = napi_undefined;
        napi_typeof(env, argv[i], &valueType);

        if (i == 0 && valueType == napi_string) {
            napi_get_value_string_utf8(env, argv[i], type, TYPE_SIZE, &typeLen);
        } else if (i == 1 && valueType == napi_object) {
            continue;
        } else if (i == 2 && (valueType == napi_function || valueType == napi_undefined)) {
            continue;
        } else {
            NAPI_ASSERT(env, false, "type mismatch");
        }
    }
    JSObjectWrapper *wrapper = nullptr;
    napi_unwrap(env, argv[1], (void**)&wrapper);
    NAPI_ASSERT(env, wrapper != nullptr, "object wrapper is null");
    if (argc == requireArgc) {
        wrapper->DeleteWatch(env, type);
    } else {
        wrapper->DeleteWatch(env, type, argv[2]);
    }
    napi_value result = nullptr;
    napi_get_undefined(env, &result);
    return result;
}
}

