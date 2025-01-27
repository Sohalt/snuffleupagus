#ifdef PHP_WIN32
#include "win32/glob.h"
#else
#include <glob.h>
#endif

#include "php_snuffleupagus.h"

#ifndef ZEND_EXT_API
#define ZEND_EXT_API ZEND_DLEXPORT
#endif

static PHP_INI_MH(OnUpdateConfiguration);
static inline void sp_op_array_handler(zend_op_array *op);

#ifdef SP_DEBUG_STDERR
int sp_debug_stderr = STDERR_FILENO;
#endif

ZEND_EXTENSION();

// LCOV_EXCL_START
ZEND_DLEXPORT int sp_zend_startup(zend_extension *extension) {
  return zend_startup_module(&snuffleupagus_module_entry);
}
// LCOV_EXCL_END

static inline void sp_op_array_handler(zend_op_array *const op) {
  // We need a filename, and strict mode not already enabled on this op
  if (NULL == op->filename || op->fn_flags & ZEND_ACC_STRICT_TYPES) {
    return;
  } else {
    if (SPCFG(global_strict).enable) {
      op->fn_flags |= ZEND_ACC_STRICT_TYPES;
    }
  }
}

ZEND_DECLARE_MODULE_GLOBALS(snuffleupagus)

static PHP_INI_MH(StrictMode) {
  TSRMLS_FETCH();

  SPG(allow_broken_configuration) = false;
  if (new_value && zend_string_equals_literal(new_value, "1")) {
    SPG(allow_broken_configuration) = true;
  }
  return SUCCESS;
}

PHP_INI_BEGIN()
PHP_INI_ENTRY("sp.configuration_file", "", PHP_INI_SYSTEM, OnUpdateConfiguration)
PHP_INI_ENTRY("sp.allow_broken_configuration", "0", PHP_INI_SYSTEM, StrictMode)
PHP_INI_END()

ZEND_DLEXPORT zend_extension zend_extension_entry = {
    PHP_SNUFFLEUPAGUS_EXTNAME,
    PHP_SNUFFLEUPAGUS_VERSION,
    PHP_SNUFFLEUPAGUS_AUTHOR,
    PHP_SNUFFLEUPAGUS_URL,
    PHP_SNUFFLEUPAGUS_COPYRIGHT,
    sp_zend_startup,
    NULL,
    NULL,                /* activate_func_t */
    NULL,                /* deactivate_func_t */
    NULL,                /* message_handler_func_t */
    sp_op_array_handler, /* op_array_handler_func_t */
    NULL,                /* statement_handler_func_t */
    NULL,                /* fcall_begin_handler_func_t */
    NULL,                /* fcall_end_handler_func_t */
    NULL,                /* op_array_ctor_func_t */
    NULL,                /* op_array_dtor_func_t */
    STANDARD_ZEND_EXTENSION_PROPERTIES};

static void sp_load_other_modules() {
  // try to load other modules before initializing Snuffleupagus
  zend_module_entry *module;
  bool should_start = false;
  ZEND_HASH_FOREACH_PTR(&module_registry, module) {
    if (should_start) {
      sp_log_debug("attempting to start module '%s' early", module->name);
      if (zend_startup_module_ex(module) != SUCCESS) {
        // startup failed. let's try again later.
        module->module_started = 0;
      }
    }
    if (strcmp(module->name, PHP_SNUFFLEUPAGUS_EXTNAME) == 0) {
      should_start = true;
    }
  } ZEND_HASH_FOREACH_END();


}

static PHP_GINIT_FUNCTION(snuffleupagus) {
#ifdef SP_DEBUG_STDERR
  if (getenv("SP_NODEBUG")) {
    sp_debug_stderr = -1;
  } else {
    sp_debug_stderr = dup(STDERR_FILENO);
  }
#endif
  sp_log_debug("(GINIT)");
  sp_load_other_modules();
  snuffleupagus_globals->is_config_valid = SP_CONFIG_NONE;
  snuffleupagus_globals->in_eval = 0;

#define SP_INIT_HT(F)                                                          \
  snuffleupagus_globals->F = pemalloc(sizeof(*(snuffleupagus_globals->F)), 1); \
    zend_hash_init(snuffleupagus_globals->F, 10, NULL, NULL, 1);
  SP_INIT_HT(disabled_functions_hook);
  SP_INIT_HT(sp_internal_functions_hook);
  SP_INIT_HT(sp_eval_blacklist_functions_hook);
  SP_INIT_HT(config_disabled_functions);
  SP_INIT_HT(config_disabled_functions_hooked);
  SP_INIT_HT(config_disabled_functions_ret);
  SP_INIT_HT(config_disabled_functions_ret_hooked);
  SP_INIT_HT(config_ini.entries);
#undef SP_INIT_HT

#define SP_INIT_NULL(F) snuffleupagus_globals->F = NULL;
  SP_INIT_NULL(config_encryption_key);
  SP_INIT_NULL(config_cookies_env_var);
  SP_INIT_NULL(config_disabled_functions_reg.disabled_functions);
  SP_INIT_NULL(config_disabled_functions_reg_ret.disabled_functions);
  SP_INIT_NULL(config_cookie.cookies);
  SP_INIT_NULL(config_eval.blacklist);
  SP_INIT_NULL(config_eval.whitelist);
  SP_INIT_NULL(config_wrapper.whitelist);
#undef SP_INIT_NULL
}

PHP_MINIT_FUNCTION(snuffleupagus) {
  sp_log_debug("(MINIT)");
  REGISTER_INI_ENTRIES();

  return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(snuffleupagus) {
  sp_log_debug("(MSHUTDOWN)");
  unhook_functions(SPG(sp_internal_functions_hook));
  unhook_functions(SPG(disabled_functions_hook));
  unhook_functions(SPG(sp_eval_blacklist_functions_hook));
  if (SPCFG(ini).enable) { sp_unhook_ini(); }
  UNREGISTER_INI_ENTRIES();

  return SUCCESS;
}

static inline void free_disabled_functions_hashtable(HashTable *const ht) {
  void *ptr = NULL;
  ZEND_HASH_FOREACH_PTR(ht, ptr) { sp_list_free(ptr, sp_free_disabled_function); }
  ZEND_HASH_FOREACH_END();
}

static inline void free_config_ini_entries(HashTable *const ht) {
  void *ptr = NULL;
  ZEND_HASH_FOREACH_PTR(ht, ptr) { sp_free_ini_entry(ptr); pefree(ptr, 1); }
  ZEND_HASH_FOREACH_END();
}

static PHP_GSHUTDOWN_FUNCTION(snuffleupagus) {
  sp_log_debug("(GSHUTDOWN)");
#define FREE_HT(F)                       \
  zend_hash_destroy(snuffleupagus_globals->F); \
  pefree(snuffleupagus_globals->F, 1);
  FREE_HT(disabled_functions_hook);
  FREE_HT(sp_eval_blacklist_functions_hook);

#define FREE_HT_LIST(F)                                         \
  free_disabled_functions_hashtable(snuffleupagus_globals->F); \
  FREE_HT(F);
  FREE_HT_LIST(config_disabled_functions);
  FREE_HT_LIST(config_disabled_functions_hooked);
  FREE_HT_LIST(config_disabled_functions_ret);
  FREE_HT_LIST(config_disabled_functions_ret_hooked);
#undef FREE_HT_LIST

  free_config_ini_entries(snuffleupagus_globals->config_ini.entries);
  FREE_HT(config_ini.entries);
#undef FREE_HT

  sp_list_free(snuffleupagus_globals->config_disabled_functions_reg.disabled_functions, sp_free_disabled_function);
  sp_list_free(snuffleupagus_globals->config_disabled_functions_reg_ret.disabled_functions, sp_free_disabled_function);
  sp_list_free(snuffleupagus_globals->config_cookie.cookies, sp_free_cookie);

#define FREE_LST(L) sp_list_free(snuffleupagus_globals->L, sp_free_zstr);
  FREE_LST(config_eval.blacklist);
  FREE_LST(config_eval.whitelist);
  FREE_LST(config_wrapper.whitelist);
#undef FREE_LST


// #define FREE_CFG(C) pefree(snuffleupagus_globals->config.C, 1);
#define FREE_CFG_ZSTR(C) sp_free_zstr(snuffleupagus_globals->C);
  FREE_CFG_ZSTR(config_unserialize.dump);
  FREE_CFG_ZSTR(config_unserialize.textual_representation);
  FREE_CFG_ZSTR(config_upload_validation.script);
  FREE_CFG_ZSTR(config_eval.dump);
  FREE_CFG_ZSTR(config_eval.textual_representation);
// #undef FREE_CFG
#undef FREE_CFG_ZSTR

#ifdef SP_DEBUG_STDERR
  if (sp_debug_stderr >= 0) {
    close(sp_debug_stderr);
    sp_debug_stderr = STDERR_FILENO;
  }
#endif
}

PHP_RINIT_FUNCTION(snuffleupagus) {
  SPG(execution_depth) = 0;
  SPG(in_eval) = 0;

  const sp_config_wrapper *const config_wrapper = &(SPCFG(wrapper));
#if defined(COMPILE_DL_SNUFFLEUPAGUS) && defined(ZTS)
  ZEND_TSRMLS_CACHE_UPDATE();
#endif

  if (!SPG(allow_broken_configuration)) {
    if (SPG(is_config_valid) == SP_CONFIG_INVALID) {
      sp_log_err("config", "Invalid configuration file");
    } else if (SPG(is_config_valid) == SP_CONFIG_NONE) {
      sp_log_warn("config",
                  "No configuration specificed via sp.configuration_file");
    }
  }

  // We need to disable wrappers loaded by extensions loaded after SNUFFLEUPAGUS.
  if (config_wrapper->enabled &&
      zend_hash_num_elements(php_stream_get_url_stream_wrappers_hash()) != config_wrapper->num_wrapper) {
    sp_disable_wrapper();
  }

  if (NULL != SPCFG(encryption_key)) {
    if (NULL != SPCFG(cookie).cookies) {
      zend_hash_apply_with_arguments(Z_ARRVAL(PG(http_globals)[TRACK_VARS_COOKIE]), decrypt_cookie, 0);
    }
  }
  return SUCCESS;
}

PHP_RSHUTDOWN_FUNCTION(snuffleupagus) { return SUCCESS; }

PHP_MINFO_FUNCTION(snuffleupagus) {
  const char *valid_config;
  switch (SPG(is_config_valid)) {
    case SP_CONFIG_VALID:
      valid_config = "yes";
      break;
    case SP_CONFIG_INVALID:
      valid_config = "invalid";
      break;
    case SP_CONFIG_NONE:
    default:
      valid_config = "no";
  }
  php_info_print_table_start();
  php_info_print_table_row(
      2, "snuffleupagus support",
      SPG(is_config_valid) ? "enabled" : "disabled");
  php_info_print_table_row(2, "Version", PHP_SNUFFLEUPAGUS_VERSION "-sng (with Suhosin-NG patches)");
  php_info_print_table_row(2, "Valid config", valid_config);
  php_info_print_table_end();
  DISPLAY_INI_ENTRIES();
}

static PHP_INI_MH(OnUpdateConfiguration) {
  sp_log_debug("(OnUpdateConfiguration)");

  TSRMLS_FETCH();

  if (!new_value || !new_value->len) {
    return FAILURE;
  }

  char *str = new_value->val;

  while (1) {
    // We don't care about overwriting new_value->val
    const char *config_file = strsep(&str, ",");
    if (config_file == NULL) break;

    glob_t globbuf;
    if (0 != glob(config_file, GLOB_NOCHECK, NULL, &globbuf)) {
      SPG(is_config_valid) = SP_CONFIG_INVALID;
      globfree(&globbuf);
      return FAILURE;
    }

    for (size_t i = 0; globbuf.gl_pathv[i]; i++) {
      if (sp_parse_config(globbuf.gl_pathv[i]) != SUCCESS) {
        SPG(is_config_valid) = SP_CONFIG_INVALID;
        globfree(&globbuf);
        return FAILURE;
      }
    }
    globfree(&globbuf);
  }

  SPG(is_config_valid) = SP_CONFIG_VALID;

  if (SPCFG(sloppy).enable) {
    hook_sloppy();
  }

  if (SPCFG(random).enable) {
    hook_rand();
  }

  if (SPCFG(upload_validation).enable) {
    hook_upload();
  }

  if (SPCFG(disable_xxe).enable == 0) {
    hook_libxml_disable_entity_loader();
  }

  if (SPCFG(wrapper).enabled) {
    hook_stream_wrappers();
  }

  if (SPCFG(session).encrypt || SPCFG(session).sid_min_length || SPCFG(session).sid_max_length) {
    hook_session();
  }

  if (NULL != SPCFG(encryption_key)) {
    if (SPCFG(unserialize).enable) {
      hook_serialize();
    }
  }

  hook_disabled_functions();
  hook_execute();
  hook_cookies();

  if (SPCFG(ini).enable) {
    sp_hook_ini();
  }

  sp_hook_register_server_variables();

  if (SPCFG(global_strict).enable) {
    if (!zend_get_extension(PHP_SNUFFLEUPAGUS_EXTNAME)) {
      zend_extension_entry.startup = NULL;
      zend_register_extension(&zend_extension_entry, NULL);
    }
    // This is needed to implement the global strict mode
    CG(compiler_options) |= ZEND_COMPILE_HANDLE_OP_ARRAY;
  }

  // If `zend_write_default` is not NULL it is already hooked.
  if ((zend_hash_str_find(SPCFG(disabled_functions_hooked), ZEND_STRL("echo")) ||
       zend_hash_str_find(SPCFG(disabled_functions_ret_hooked), ZEND_STRL("echo"))) &&
      NULL == zend_write_default && zend_write != hook_echo) {
    zend_write_default = zend_write;
    zend_write = hook_echo;
  }

  SPG(hook_execute) = SPCFG(max_execution_depth) > 0 ||
    SPCFG(disabled_functions_reg).disabled_functions ||
    SPCFG(disabled_functions_reg_ret).disabled_functions ||
    (SPCFG(disabled_functions) && zend_hash_num_elements(SPCFG(disabled_functions))) ||
    (SPCFG(disabled_functions) && zend_hash_num_elements(SPCFG(disabled_functions_ret)));

  return SUCCESS;
}

const zend_function_entry snuffleupagus_functions[] = {PHP_FE_END};

zend_module_entry snuffleupagus_module_entry = {
    STANDARD_MODULE_HEADER,
    PHP_SNUFFLEUPAGUS_EXTNAME,
    snuffleupagus_functions,
    PHP_MINIT(snuffleupagus),
    PHP_MSHUTDOWN(snuffleupagus),
    PHP_RINIT(snuffleupagus),
    PHP_RSHUTDOWN(snuffleupagus),
    PHP_MINFO(snuffleupagus),
    PHP_SNUFFLEUPAGUS_VERSION,
    PHP_MODULE_GLOBALS(snuffleupagus),
    PHP_GINIT(snuffleupagus),
    PHP_GSHUTDOWN(snuffleupagus),
    NULL,
    STANDARD_MODULE_PROPERTIES_EX};

#ifdef COMPILE_DL_SNUFFLEUPAGUS
#ifdef ZTS
ZEND_TSRMLS_CACHE_DEFINE()
#endif
ZEND_GET_MODULE(snuffleupagus)
#endif
