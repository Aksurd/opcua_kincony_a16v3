#ifndef UA_ACCESSCONTROL_CUSTOM_H
#define UA_ACCESSCONTROL_CUSTOM_H

#ifdef __cplusplus
extern "C" {
#endif

// Включаем основной заголовок open62541
#include <open62541.h>

// Определяем константы прав доступа, если они не были определены
#ifndef UA_ACCESSLEVELMASK_BROWSE
    #define UA_ACCESSLEVELMASK_BROWSE (0x1)
#endif

#ifndef UA_ACCESSLEVELMASK_READ
    #define UA_ACCESSLEVELMASK_READ (0x2)
#endif

#ifndef UA_ACCESSLEVELMASK_WRITE
    #define UA_ACCESSLEVELMASK_WRITE (0x4)
#endif

#ifndef UA_ACCESSLEVELMASK_CALL
    #define UA_ACCESSLEVELMASK_CALL (0x8)
#endif

#ifndef UA_ACCESSLEVELMASK_READHISTORY
    #define UA_ACCESSLEVELMASK_READHISTORY UA_ACCESSLEVELMASK_READ
#endif

#ifndef UA_ACCESSLEVELMASK_WRITEHISTORY
    #define UA_ACCESSLEVELMASK_WRITEHISTORY UA_ACCESSLEVELMASK_WRITE
#endif

// Если типы не определены, объявляем минимальный набор
#ifndef UA_ENABLE_ACCESS_CONTROL
    typedef struct UA_AccessControl UA_AccessControl;
    typedef struct UA_AccessControlConfig UA_AccessControlConfig;
#endif

typedef struct UA_ServerConfig UA_ServerConfig;
// UA_ByteString уже определен в open62541.h как typedef UA_String UA_ByteString;
// НЕ переопределяем его!

UA_AccessControl* UA_AccessControl_custom(const UA_AccessControlConfig *config);

// Добавляем прототип функции инициализации
UA_StatusCode UA_AccessControl_custom_init(UA_ServerConfig *config,
                                          UA_Boolean allowAnonymous,
                                          const UA_ByteString *userTokenPolicyUri);

#ifdef __cplusplus
}
#endif

#endif /* UA_ACCESSCONTROL_CUSTOM_H */