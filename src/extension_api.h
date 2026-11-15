#pragma once

#include <stddef.h>
#include <stdint.h>

#define NS_EXTENSION_ABI_VERSION 1u
#define NS_EXTENSION_ENTRY_SYMBOL "ns_extension_entry"

#if defined(__cplusplus)
#define NS_EXTERN_C extern "C"
#else
#define NS_EXTERN_C
#endif

#if defined(_WIN32)
#define NS_EXTENSION_EXPORT NS_EXTERN_C __declspec(dllexport)
#else
#define NS_EXTENSION_EXPORT NS_EXTERN_C __attribute__((visibility("default")))
#endif

typedef struct NsStringView {
    const char *data;
    size_t size;
} NsStringView;

typedef void *NsExtensionHandle;

typedef enum NsLogLevel {
    NS_LOG_DEBUG = 0,
    NS_LOG_INFO = 1,
    NS_LOG_WARNING = 2,
    NS_LOG_ERROR = 3
} NsLogLevel;

typedef struct NsCommandContext {
    uint32_t struct_size;
    NsStringView active_file_path;
    NsStringView project_root;
    uint32_t document_modified;
} NsCommandContext;

typedef void (*NsCommandCallback)(void *user_data, const NsCommandContext *context);

#define NS_EVENT_DOCUMENT_WILL_SAVE 1u

typedef struct NsEventContext {
    uint32_t struct_size;
    uint32_t event_type;
    NsStringView active_file_path;
    NsStringView document_text;
} NsEventContext;

typedef void (*NsEventCallback)(void *user_data, const NsEventContext *context);

typedef enum NsSettingType {
    NS_SETTING_BOOLEAN = 1,
    NS_SETTING_INTEGER = 2,
    NS_SETTING_TEXT = 3,
    NS_SETTING_CHOICE = 4
} NsSettingType;

typedef struct NsSettingChoice {
    uint32_t struct_size;
    NsStringView value;
    NsStringView label;
} NsSettingChoice;

typedef struct NsSettingSpec {
    uint32_t struct_size;
    NsStringView key;
    NsStringView label;
    NsStringView description;
    uint32_t type;
    NsStringView default_value;
    int64_t minimum;
    int64_t maximum;
    const NsSettingChoice *choices;
    size_t choice_count;
} NsSettingSpec;

typedef struct NsSettingsPageSpec {
    uint32_t struct_size;
    NsStringView title;
    NsStringView description;
    NsStringView keywords;
    const NsSettingSpec *settings;
    size_t setting_count;
} NsSettingsPageSpec;

typedef struct NsCommandSpec {
    uint32_t struct_size;
    NsStringView id;
    NsStringView title;
    NsStringView category;
    NsStringView default_shortcut;
    NsCommandCallback callback;
    void *user_data;
} NsCommandSpec;

typedef struct NsHostApi {
    uint32_t struct_size;
    uint32_t abi_version;
    void *host_context;

    int32_t (*register_command)(void *host_context, NsExtensionHandle extension,
                                const NsCommandSpec *command);
    void (*show_message)(void *host_context, NsStringView message, uint32_t timeout_ms);
    void (*log)(void *host_context, NsLogLevel level, NsStringView message);
    int32_t (*open_file)(void *host_context, NsStringView path);
    int32_t (*insert_text)(void *host_context, NsStringView text);
    int32_t (*replace_document_text)(void *host_context, NsStringView text);
    int32_t (*run_process)(void *host_context, NsStringView program,
                           const NsStringView *arguments, size_t argument_count,
                           NsStringView working_directory);
    int32_t (*register_event_listener)(void *host_context, NsExtensionHandle extension,
                                       uint32_t event_type, NsEventCallback callback,
                                       void *user_data);
    size_t (*get_active_document_text)(void *host_context, char *buffer, size_t capacity);
    int32_t (*register_settings_page)(void *host_context, NsExtensionHandle extension,
                                      const NsSettingsPageSpec *page);
    size_t (*get_extension_setting)(void *host_context, NsExtensionHandle extension,
                                    NsStringView key, char *buffer, size_t capacity);
    int32_t (*show_view)(void *host_context, NsStringView view_id);
} NsHostApi;

typedef int32_t (*NsActivateCallback)(void *extension_context, const NsHostApi *host,
                                      NsExtensionHandle extension);
typedef void (*NsDeactivateCallback)(void *extension_context);

typedef struct NsExtensionDescriptor {
    uint32_t struct_size;
    uint32_t abi_version;
    NsStringView id;
    NsStringView name;
    NsStringView version;
    void *extension_context;
    NsActivateCallback activate;
    NsDeactivateCallback deactivate;
} NsExtensionDescriptor;

typedef const NsExtensionDescriptor *(*NsExtensionEntryFunction)(void);

