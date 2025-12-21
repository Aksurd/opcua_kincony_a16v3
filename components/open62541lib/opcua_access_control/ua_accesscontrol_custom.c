/* Custom OPC UA Access Control Plugin for ESP32
 * Integrates with system configuration (config.h/config.c)
 */

#include "ua_accesscontrol_custom.h"
#include "../../../main/config.h"  /* Include your system config */

// Пробуем включить accesscontrol по разным путям
#ifdef UA_ENABLE_ACCESS_CONTROL
    #ifdef __has_include
        #if __has_include(<open62541/plugin/accesscontrol_default.h>)
            #include <open62541/plugin/accesscontrol_default.h>
        #elif __has_include(<open62541/accesscontrol_default.h>)
            #include <open62541/accesscontrol_default.h>
        #else
            // Определяем минимальные структуры если файл не найден
            typedef struct UA_Server UA_Server;
            typedef struct UA_EndpointDescription UA_EndpointDescription;
            typedef struct UA_ExtensionObject UA_ExtensionObject;
            typedef struct UA_NodeId UA_NodeId;
            typedef struct UA_AddNodesItem UA_AddNodesItem;
            typedef struct UA_AddReferencesItem UA_AddReferencesItem;
            typedef struct UA_DeleteNodesItem UA_DeleteNodesItem;
            typedef struct UA_DeleteReferencesItem UA_DeleteReferencesItem;
            typedef struct UA_DataValue UA_DataValue;
            #define UA_ENABLE_SUBSCRIPTIONS
            #define UA_ENABLE_HISTORIZING
        #endif
    #else
        // Для компиляторов без __has_include
        #include <open62541/plugin/accesscontrol_default.h>
    #endif
#endif

/* Policy strings */
#define ANONYMOUS_POLICY "open62541-anonymous-policy"
#define USERNAME_POLICY "open62541-username-policy"
static const UA_String anonymous_policy = UA_STRING_STATIC(ANONYMOUS_POLICY);
static const UA_String username_policy = UA_STRING_STATIC(USERNAME_POLICY);

/* Context structure */
typedef struct {
    UA_Boolean allowAnonymous;
} AccessControlContext;

/************************/
/* Access Control Logic */
/************************/

static UA_StatusCode
activateSession_custom(UA_Server *server, UA_AccessControl *ac,
                       const UA_EndpointDescription *endpointDescription,
                       const UA_ByteString *secureChannelRemoteCertificate,
                       const UA_NodeId *sessionId,
                       const UA_ExtensionObject *userIdentityToken,
                       void **sessionContext) {
    AccessControlContext *context = (AccessControlContext*)ac->context;

    /* The empty token is interpreted as anonymous */
    if(userIdentityToken->encoding == UA_EXTENSIONOBJECT_ENCODED_NOBODY) {
        if(!context->allowAnonymous)
            return UA_STATUSCODE_BADIDENTITYTOKENINVALID;

        /* No userdata for anonymous */
        *sessionContext = NULL;
        return UA_STATUSCODE_GOOD;
    }

    /* Could the token be decoded? */
    if(userIdentityToken->encoding < UA_EXTENSIONOBJECT_DECODED)
        return UA_STATUSCODE_BADIDENTITYTOKENINVALID;

    /* Anonymous login token */
    if(userIdentityToken->content.decoded.type == &UA_TYPES[UA_TYPES_ANONYMOUSIDENTITYTOKEN]) {
        if(!context->allowAnonymous)
            return UA_STATUSCODE_BADIDENTITYTOKENINVALID;

        const UA_AnonymousIdentityToken *token = (UA_AnonymousIdentityToken*)
            userIdentityToken->content.decoded.data;

        /* Compatibility: empty policyId == ANONYMOUS_POLICY */
        if(token->policyId.data && !UA_String_equal(&token->policyId, &anonymous_policy))
            return UA_STATUSCODE_BADIDENTITYTOKENINVALID;

        /* No userdata for anonymous */
        *sessionContext = NULL;
        return UA_STATUSCODE_GOOD;
    }

    /* Username and password token */
    if(userIdentityToken->content.decoded.type == &UA_TYPES[UA_TYPES_USERNAMEIDENTITYTOKEN]) {
        const UA_UserNameIdentityToken *userToken =
            (UA_UserNameIdentityToken*)userIdentityToken->content.decoded.data;

        if(!UA_String_equal(&userToken->policyId, &username_policy))
            return UA_STATUSCODE_BADIDENTITYTOKENINVALID;

        /* Empty username and password */
        if(userToken->userName.length == 0 && userToken->password.length == 0)
            return UA_STATUSCODE_BADIDENTITYTOKENINVALID;

        /* Convert UA_String to C strings for comparison */
        char username[32] = {0};
        char password[32] = {0};
        
        if(userToken->userName.length > 0 && userToken->userName.length < sizeof(username))
            memcpy(username, userToken->userName.data, userToken->userName.length);
        
        if(userToken->password.length > 0 && userToken->password.length < sizeof(password))
            memcpy(password, userToken->password.data, userToken->password.length);
        
        /* Check authentication using your config system */
        opcua_user_t *user = config_find_opcua_user(username);
        if(!user || !user->enabled) {
            UA_LOG_DEBUG(UA_Log_Stdout, UA_LOGCATEGORY_SERVER,
                        "AccessControl: User '%s' not found or disabled", username);
            return UA_STATUSCODE_BADUSERACCESSDENIED;
        }
        
        if(!config_check_opcua_password(user, password)) {
            UA_LOG_DEBUG(UA_Log_Stdout, UA_LOGCATEGORY_SERVER,
                        "AccessControl: Invalid password for user '%s'", username);
            return UA_STATUSCODE_BADUSERACCESSDENIED;
        }
        
        /* Store user rights in session context */
        uint16_t *userRights = (uint16_t*)UA_malloc(sizeof(uint16_t));
        if(!userRights)
            return UA_STATUSCODE_BADOUTOFMEMORY;
        
        *userRights = user->rights;
        *sessionContext = userRights;
        
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER,
                   "AccessControl: User '%s' logged in successfully (rights: 0x%04X)",
                   username, user->rights);
        
        return UA_STATUSCODE_GOOD;
    }

    /* Unsupported token type */
    return UA_STATUSCODE_BADIDENTITYTOKENINVALID;
}

static void
closeSession_custom(UA_Server *server, UA_AccessControl *ac,
                    const UA_NodeId *sessionId, void *sessionContext) {
    if(sessionContext)
        UA_free(sessionContext);
}

static UA_UInt32
getUserRightsMask_custom(UA_Server *server, UA_AccessControl *ac,
                         const UA_NodeId *sessionId, void *sessionContext,
                         const UA_NodeId *nodeId, void *nodeContext) {
    /* If auth is disabled, allow everything */
    if(!g_config.opcua_auth_enable)
        return 0xFFFFFFFF;
    
    /* Anonymous sessions have no rights */
    if(!sessionContext)
        return 0;
    
    /* Map your rights to OPC UA rights mask */
    uint16_t rights = *(uint16_t*)sessionContext;
    UA_UInt32 uaRights = 0;
    
    if((rights & OPCUA_RIGHT_BROWSE) || (rights & OPCUA_RIGHT_ADMIN))
        uaRights |= UA_ACCESSLEVELMASK_BROWSE;
    
    if((rights & OPCUA_RIGHT_READ) || (rights & OPCUA_RIGHT_ADMIN))
        uaRights |= UA_ACCESSLEVELMASK_READ;
    
    if((rights & OPCUA_RIGHT_WRITE) || (rights & OPCUA_RIGHT_ADMIN))
        uaRights |= UA_ACCESSLEVELMASK_WRITE;
    
    if((rights & OPCUA_RIGHT_CALL) || (rights & OPCUA_RIGHT_ADMIN))
        uaRights |= UA_ACCESSLEVELMASK_CALL;
    
    return uaRights;
}

static UA_Byte
getUserAccessLevel_custom(UA_Server *server, UA_AccessControl *ac,
                          const UA_NodeId *sessionId, void *sessionContext,
                          const UA_NodeId *nodeId, void *nodeContext) {
    /* If auth is disabled, allow everything */
    if(!g_config.opcua_auth_enable)
        return 0xFF;
    
    /* Anonymous sessions have limited access */
    if(!sessionContext)
        return UA_ACCESSLEVELMASK_BROWSE | UA_ACCESSLEVELMASK_READ;
    
    /* Map your rights to OPC UA access level */
    uint16_t rights = *(uint16_t*)sessionContext;
    UA_Byte accessLevel = 0;
    
    if((rights & OPCUA_RIGHT_BROWSE) || (rights & OPCUA_RIGHT_ADMIN))
        accessLevel |= UA_ACCESSLEVELMASK_BROWSE;
    
    if((rights & OPCUA_RIGHT_READ) || (rights & OPCUA_RIGHT_ADMIN))
        accessLevel |= UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_READHISTORY;
    
    if((rights & OPCUA_RIGHT_WRITE) || (rights & OPCUA_RIGHT_ADMIN))
        accessLevel |= UA_ACCESSLEVELMASK_WRITE | UA_ACCESSLEVELMASK_WRITEHISTORY;
    
    return accessLevel;
}

static UA_Boolean
getUserExecutable_custom(UA_Server *server, UA_AccessControl *ac,
                         const UA_NodeId *sessionId, void *sessionContext,
                         const UA_NodeId *methodId, void *methodContext) {
    /* If auth is disabled, allow everything */
    if(!g_config.opcua_auth_enable)
        return true;
    
    /* Anonymous cannot execute methods */
    if(!sessionContext)
        return false;
    
    /* Check if user has CALL right */
    uint16_t rights = *(uint16_t*)sessionContext;
    return ((rights & OPCUA_RIGHT_CALL) || (rights & OPCUA_RIGHT_ADMIN));
}

static UA_Boolean
getUserExecutableOnObject_custom(UA_Server *server, UA_AccessControl *ac,
                                 const UA_NodeId *sessionId, void *sessionContext,
                                 const UA_NodeId *methodId, void *methodContext,
                                 const UA_NodeId *objectId, void *objectContext) {
    return getUserExecutable_custom(server, ac, sessionId, sessionContext, methodId, methodContext);
}

/* Default implementations for other operations */
static UA_Boolean
allowAddNode_custom(UA_Server *server, UA_AccessControl *ac,
                    const UA_NodeId *sessionId, void *sessionContext,
                    const UA_AddNodesItem *item) {
    /* Only admins can add nodes */
    if(!sessionContext)
        return false;
    
    uint16_t rights = *(uint16_t*)sessionContext;
    return ((rights & OPCUA_RIGHT_ADMIN) != 0);
}

static UA_Boolean
allowAddReference_custom(UA_Server *server, UA_AccessControl *ac,
                         const UA_NodeId *sessionId, void *sessionContext,
                         const UA_AddReferencesItem *item) {
    /* Only admins can add references */
    if(!sessionContext)
        return false;
    
    uint16_t rights = *(uint16_t*)sessionContext;
    return ((rights & OPCUA_RIGHT_ADMIN) != 0);
}

static UA_Boolean
allowDeleteNode_custom(UA_Server *server, UA_AccessControl *ac,
                       const UA_NodeId *sessionId, void *sessionContext,
                       const UA_DeleteNodesItem *item) {
    /* Only admins can delete nodes */
    if(!sessionContext)
        return false;
    
    uint16_t rights = *(uint16_t*)sessionContext;
    return ((rights & OPCUA_RIGHT_ADMIN) != 0);
}

static UA_Boolean
allowDeleteReference_custom(UA_Server *server, UA_AccessControl *ac,
                            const UA_NodeId *sessionId, void *sessionContext,
                            const UA_DeleteReferencesItem *item) {
    /* Only admins can delete references */
    if(!sessionContext)
        return false;
    
    uint16_t rights = *(uint16_t*)sessionContext;
    return ((rights & OPCUA_RIGHT_ADMIN) != 0);
}

static UA_Boolean
allowBrowseNode_custom(UA_Server *server, UA_AccessControl *ac,
                       const UA_NodeId *sessionId, void *sessionContext,
                       const UA_NodeId *nodeId, void *nodeContext) {
    /* Everyone can browse (if authentication is enabled, anonymous can browse too) */
    return true;
}

#ifdef UA_ENABLE_SUBSCRIPTIONS
static UA_Boolean
allowTransferSubscription_custom(UA_Server *server, UA_AccessControl *ac,
                                 const UA_NodeId *oldSessionId, void *oldSessionContext,
                                 const UA_NodeId *newSessionId, void *newSessionContext) {
    /* Transfer only allowed for same user */
    if(oldSessionContext == newSessionContext)
        return true;
    
    if(oldSessionContext && newSessionContext) {
        uint16_t oldRights = *(uint16_t*)oldSessionContext;
        uint16_t newRights = *(uint16_t*)newSessionContext;
        return (oldRights == newRights);
    }
    
    return false;
}
#endif

#ifdef UA_ENABLE_HISTORIZING
static UA_Boolean
allowHistoryUpdateUpdateData_custom(UA_Server *server, UA_AccessControl *ac,
                                    const UA_NodeId *sessionId, void *sessionContext,
                                    const UA_NodeId *nodeId,
                                    UA_PerformUpdateType performInsertReplace,
                                    const UA_DataValue *value) {
    /* Only admins can update history */
    if(!sessionContext)
        return false;
    
    uint16_t rights = *(uint16_t*)sessionContext;
    return ((rights & OPCUA_RIGHT_ADMIN) != 0);
}

static UA_Boolean
allowHistoryUpdateDeleteRawModified_custom(UA_Server *server, UA_AccessControl *ac,
                                           const UA_NodeId *sessionId, void *sessionContext,
                                           const UA_NodeId *nodeId,
                                           UA_DateTime startTimestamp,
                                           UA_DateTime endTimestamp,
                                           bool isDeleteModified) {
    /* Only admins can delete history */
    if(!sessionContext)
        return false;
    
    uint16_t rights = *(uint16_t*)sessionContext;
    return ((rights & OPCUA_RIGHT_ADMIN) != 0);
}
#endif

/***************************************/
/* Create Delete Access Control Plugin */
/***************************************/

static void clear_custom(UA_AccessControl *ac) {
    UA_Array_delete((void*)(uintptr_t)ac->userTokenPolicies,
                    ac->userTokenPoliciesSize,
                    &UA_TYPES[UA_TYPES_USERTOKENPOLICY]);
    ac->userTokenPolicies = NULL;
    ac->userTokenPoliciesSize = 0;

    AccessControlContext *context = (AccessControlContext*)ac->context;
    if(context) {
        UA_free(ac->context);
        ac->context = NULL;
    }
}

UA_AccessControl* UA_AccessControl_custom(const UA_AccessControlConfig *config) {
    (void)config;
    
    // Возвращаем NULL, так как твоя реализация требует UA_ServerConfig
    // Реальная функция создается через UA_AccessControl_custom с UA_ServerConfig
    return NULL;
}

// Твоя настоящая функция с правильной сигнатурой
UA_StatusCode UA_AccessControl_custom_init(UA_ServerConfig *config,
                        UA_Boolean allowAnonymous,
                        const UA_ByteString *userTokenPolicyUri) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER,
               "AccessControl: Initializing custom access control");
    
    UA_AccessControl *ac = &config->accessControl;
    ac->clear = clear_custom;
    ac->activateSession = activateSession_custom;
    ac->closeSession = closeSession_custom;
    ac->getUserRightsMask = getUserRightsMask_custom;
    ac->getUserAccessLevel = getUserAccessLevel_custom;
    ac->getUserExecutable = getUserExecutable_custom;
    ac->getUserExecutableOnObject = getUserExecutableOnObject_custom;
    ac->allowAddNode = allowAddNode_custom;
    ac->allowAddReference = allowAddReference_custom;
    ac->allowBrowseNode = allowBrowseNode_custom;

#ifdef UA_ENABLE_SUBSCRIPTIONS
    ac->allowTransferSubscription = allowTransferSubscription_custom;
#endif

#ifdef UA_ENABLE_HISTORIZING
    ac->allowHistoryUpdateUpdateData = allowHistoryUpdateUpdateData_custom;
    ac->allowHistoryUpdateDeleteRawModified = allowHistoryUpdateDeleteRawModified_custom;
#endif

    ac->allowDeleteNode = allowDeleteNode_custom;
    ac->allowDeleteReference = allowDeleteReference_custom;

    AccessControlContext *context = (AccessControlContext*)
            UA_malloc(sizeof(AccessControlContext));
    if(!context)
        return UA_STATUSCODE_BADOUTOFMEMORY;
    
    memset(context, 0, sizeof(AccessControlContext));
    ac->context = context;

    /* Allow anonymous? Use config setting */
    context->allowAnonymous = (allowAnonymous && g_config.opcua_auth_enable);
    if(context->allowAnonymous) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER,
                   "AccessControl: Anonymous login is enabled");
    }

    /* Set the allowed policies */
    size_t policies = 0;
    if(context->allowAnonymous)
        policies++;
    
    /* Always allow username/password if auth is enabled */
    if(g_config.opcua_auth_enable)
        policies++;
    
    ac->userTokenPoliciesSize = 0;
    ac->userTokenPolicies = (UA_UserTokenPolicy *)
        UA_Array_new(policies, &UA_TYPES[UA_TYPES_USERTOKENPOLICY]);
    if(!ac->userTokenPolicies)
        return UA_STATUSCODE_BADOUTOFMEMORY;
    
    ac->userTokenPoliciesSize = policies;

    policies = 0;
    if(context->allowAnonymous) {
        ac->userTokenPolicies[policies].tokenType = UA_USERTOKENTYPE_ANONYMOUS;
        ac->userTokenPolicies[policies].policyId = UA_STRING_ALLOC(ANONYMOUS_POLICY);
        if (!ac->userTokenPolicies[policies].policyId.data)
            return UA_STATUSCODE_BADOUTOFMEMORY;
        policies++;
    }

    if(g_config.opcua_auth_enable) {
        ac->userTokenPolicies[policies].tokenType = UA_USERTOKENTYPE_USERNAME;
        ac->userTokenPolicies[policies].policyId = UA_STRING_ALLOC(USERNAME_POLICY);
        if(!ac->userTokenPolicies[policies].policyId.data)
            return UA_STATUSCODE_BADOUTOFMEMORY;

        /* Check security policy warning */
#if UA_LOGLEVEL <= 400
        const UA_String noneUri = UA_STRING("http://opcfoundation.org/UA/SecurityPolicy#None");
        if(UA_ByteString_equal(userTokenPolicyUri, &noneUri)) {
            UA_LOG_WARNING(UA_Log_Stdout, UA_LOGCATEGORY_SERVER,
                          "Username/Password configured, but no encrypting SecurityPolicy. "
                          "This can leak credentials on the network.");
        }
#endif
        return UA_ByteString_copy(userTokenPolicyUri,
                                  &ac->userTokenPolicies[policies].securityPolicyUri);
    }
    
    return UA_STATUSCODE_GOOD;
}