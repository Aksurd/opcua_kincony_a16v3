#ifndef UA_ACCESSCONTROL_CUSTOM_H
#define UA_ACCESSCONTROL_CUSTOM_H

#ifdef __cplusplus
extern "C" {
#endif

// 1. Включаем основной заголовок open62541, где определены базовые типы и константы
#include <open62541.h>

// 2. Определяем константы прав доступа, если они не были определены в open62541.h
//    (Актуально для некоторых версий библиотеки)
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

// Эти могут отсутствовать в старых версиях. Если их нет, используем базовые.
#ifndef UA_ACCESSLEVELMASK_READHISTORY
    #define UA_ACCESSLEVELMASK_READHISTORY UA_ACCESSLEVELMASK_READ
#endif

#ifndef UA_ACCESSLEVELMASK_WRITEHISTORY
    #define UA_ACCESSLEVELMASK_WRITEHISTORY UA_ACCESSLEVELMASK_WRITE
#endif

// Минимальные объявления для нашей кастомной реализации
typedef struct UA_AccessControl UA_AccessControl;
typedef struct UA_AccessControlConfig UA_AccessControlConfig;

UA_AccessControl* UA_AccessControl_custom(const UA_AccessControlConfig *config);

#ifdef __cplusplus
}
#endif

#endif /* UA_ACCESSCONTROL_CUSTOM_H */