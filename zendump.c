/*
  +----------------------------------------------------------------------+
  | PHP Version 7                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2018 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author: Youlin Feng <fengyoulin@php7th.com>                          |
  +----------------------------------------------------------------------+
*/

/* $Id$ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "php_zendump.h"

ZEND_DECLARE_MODULE_GLOBALS(zendump)

/* True global resources - no need for thread safety here */
// static int le_zendump;

/* {{{ PHP_INI
 */
PHP_INI_BEGIN()
	STD_PHP_INI_ENTRY("zendump.enable_trace",  "false",  PHP_INI_ALL, OnUpdateBool,   enable_trace,  zend_zendump_globals, zendump_globals)
	STD_PHP_INI_ENTRY("zendump.global_value",  "0",      PHP_INI_ALL, OnUpdateLong,   global_value,  zend_zendump_globals, zendump_globals)
	STD_PHP_INI_ENTRY("zendump.global_string", "foobar", PHP_INI_ALL, OnUpdateString, global_string, zend_zendump_globals, zendump_globals)
PHP_INI_END()
/* }}} */

void zendump_string_to_lower(char *buf, int len);

zend_string *zendump_unescape_zend_string(zend_string *org, int persistent)
{
	zend_string *str = org;
	int first = -1;
	int nrt0 = 0;
	int idx;
	for(idx = 0; idx < ZSTR_LEN(str); ++idx) {
		if('\n' == ZSTR_VAL(str)[idx] || '\r' == ZSTR_VAL(str)[idx] || '\t' == ZSTR_VAL(str)[idx] || '\0' == ZSTR_VAL(str)[idx]) {
			if(first == -1) {
				first = idx;
			}
			++nrt0;
		}
	}
	if(nrt0) {
		if(first > 0) {
			str = zend_string_dup(str, persistent);
			str = zend_string_extend(str, ZSTR_LEN(str) + nrt0, persistent);
		} else {
			str = zend_string_alloc(ZSTR_LEN(str) + nrt0, persistent);
		}
		ZSTR_LEN(str) = ZSTR_LEN(org) + nrt0;
		const char *src = ZSTR_VAL(org);
		char *dst = ZSTR_VAL(str) + first;
		for(idx = first; idx < ZSTR_LEN(org); ++idx) {
			if('\n' == src[idx]) {
				*dst++ = '\\';
				*dst++ = 'n';
			} else if('\r' == src[idx]) {
				*dst++ = '\\';
				*dst++ = 'r';
			} else if('\t' == src[idx]) {
				*dst++ = '\\';
				*dst++ = 't';
			} else if('\0' == src[idx]) {
				*dst++ = '\\';
				*dst++ = '0';
			} else {
				*dst++ = src[idx];
			}
		}
		*dst = '\0';
	}
	return str;
}

const char *zend_type_names[] = {"undefined", "null", "false", "true", "long", "double", "string", "array", "object", "resource", "reference", "constant", "constant ast", "bool", "callable", "indirect", NULL, "pointer", "void", "iterable", "error"};
const char *zendump_get_type_name(uint32_t type)
{
	if(type < ARRAY_LENGTH(zend_type_names)) {
		return zend_type_names[type];
	}
	return NULL;
}

void zendump_zval_dump(zval *val, int level)
{
	if(level > 0) {
		php_printf("%*c", level, ' ');
	}
again:
	php_printf("zval(0x" ZEND_XLONG_FMT ") ", val);
	switch(Z_TYPE_P(val)) {
		case IS_UNDEF:
			PHPWRITE(": undefined\n", 12);
			break;
		case IS_NULL:
			PHPWRITE(": null\n", 7);
			break;
		case IS_FALSE:
			PHPWRITE(": false\n", 8);
			break;
		case IS_TRUE:
			PHPWRITE(": true\n", 7);
			break;
		case IS_LONG:
			php_printf(": long(" ZEND_LONG_FMT ")\n", Z_LVAL_P(val));
			break;
		case IS_DOUBLE: {
#if SIZEOF_SIZE_T == 8
			php_printf(": double(%.*G) hex(" ZEND_XLONG_FMT ")\n", (int) EG(precision), Z_DVAL_P(val), Z_LVAL_P(val));
#elif SIZEOF_SIZE_T == 4
			uint64_t *dval = (uint64_t*)&Z_DVAL_P(val);
			uint32_t high = (*dval) >> 32, low = (uint32_t)(*dval);
			php_printf(": double(%.*G) hex(" ZEND_XLONG_FMT ZEND_XLONG_FMT ")\n", (int) EG(precision), Z_DVAL_P(val), high, low);
#else
# error "Unknown SIZEOF_SIZE_T"
#endif
			break;
		}
		case IS_STRING: {
			zend_string *str = zendump_unescape_zend_string(Z_STR_P(val), 0);
			php_printf("-> string(%zd,\"", Z_STRLEN_P(val));
			PHPWRITE(ZSTR_VAL(str), ZSTR_LEN(str));
			php_printf("\") addr(0x" ZEND_XLONG_FMT ")", Z_STR_P(val));
			if(IS_INTERNED(Z_STR_P(val))) {
				PUTS(" interned\n");
			} else {
				php_printf(" refcount(%u)\n", Z_REFCOUNTED_P(val) ? Z_REFCOUNT_P(val) : 1);
			}
			if(str != Z_STR_P(val)) {
				zend_string_release(str);
			}
			break;
		}
		case IS_ARRAY: {
			zend_array *arr = val->value.arr;
			uint32_t hashSize = -(int32_t)arr->nTableMask;
			uint32_t hashUsed = 0;
			uint32_t *hash = (uint32_t*)arr->arData - hashSize;
			uint32_t idx;

			if (level > 0 && ZEND_HASH_APPLY_PROTECTION(arr) && ++arr->u.v.nApplyCount > 1) {
				PUTS("*RECURSION*\n");
				--arr->u.v.nApplyCount;
				return;
			}

			for(idx = 0; idx < hashSize; ++idx) {
				if(hash[idx] != HT_INVALID_IDX) {
					++hashUsed;
				}
			}

			php_printf("-> array(%u) addr(0x" ZEND_XLONG_FMT ") refcount(%u) hash(%u,%u) bucket(%u,%u) data(0x" ZEND_XLONG_FMT ")\n", arr->nNumOfElements, arr, arr->gc.refcount, hashSize, hashUsed, arr->nTableSize, arr->nNumUsed, arr->arData);
			if(arr->nNumOfElements == 0) {
				break;
			}
			if(level > 0) {
				php_printf("%*c", level, ' ');
			}
			PUTS("{\n");

			zendump_zend_array_dump(arr, level + INDENT_SIZE);

			if (level > 0 && ZEND_HASH_APPLY_PROTECTION(arr)) {
				--arr->u.v.nApplyCount;
			}

			if(level > 0) {
				php_printf("%*c", level, ' ');
			}
			PUTS("}\n");
			break;
		}
		case IS_OBJECT: {
			zend_string *class_name = NULL;
			if (Z_OBJ_APPLY_COUNT_P(val) > 0) {
				PUTS("*RECURSION*\n");
				return;
			}
			Z_OBJ_INC_APPLY_COUNT_P(val);

			class_name = Z_OBJ_HANDLER_P(val, get_class_name)(Z_OBJ_P(val));
			php_printf("-> object(%s) addr(0x" ZEND_XLONG_FMT ") refcount(%u)", ZSTR_VAL(class_name), Z_OBJ_P(val), Z_REFCOUNT_P(val));
			zend_string_release(class_name);
			if((Z_OBJ_P(val)->ce && (Z_OBJ_P(val)->ce->default_properties_count || Z_OBJ_P(val)->ce->default_static_members_count)) || (Z_OBJ_P(val)->properties && Z_OBJ_P(val)->properties->nNumOfElements)) {
				PUTS(" {\n");
				if(Z_OBJ_P(val)->ce) {
					zendump_properties_dump(Z_OBJ_P(val), level);
					zendump_static_properties_dump(Z_OBJ_P(val)->ce, level);
				}
				if(Z_OBJ_P(val)->properties && Z_OBJ_P(val)->properties->nNumOfElements) {
					php_printf("%*cproperties(%u) {\n", level + INDENT_SIZE, ' ', Z_OBJ_P(val)->properties->nNumOfElements);
					zendump_zend_array_dump(Z_OBJ_P(val)->properties, level + (INDENT_SIZE << 1));
					php_printf("%*c}\n", level + INDENT_SIZE, ' ');
				}
				if(level > 0) {
					php_printf("%*c", level, ' ');
				}
				PUTS("}");
			}
			PUTS("\n");
			Z_OBJ_DEC_APPLY_COUNT_P(val);
			break;
		}
		case IS_RESOURCE: {
			const char *type_name = zend_rsrc_list_get_rsrc_type(Z_RES_P(val));
			php_printf("-> resource addr(0x" ZEND_XLONG_FMT ") data(0x" ZEND_XLONG_FMT ") type (%s) refcount(%u)\n", Z_RES_P(val), Z_RES_VAL_P(val), type_name ? type_name : "unknown", Z_REFCOUNT_P(val));
			break;
		}
		case IS_REFERENCE:
			php_printf("-> reference(%u) addr(0x" ZEND_XLONG_FMT ") ", Z_REFCOUNT_P(val), Z_REF_P(val));
			val = Z_REFVAL_P(val);
			goto again;
		case IS_INDIRECT:
			PHPWRITE("-> ", 3);
			val = Z_INDIRECT_P(val);
			goto again;
		default:
			php_printf(": unknown type(%u)\n", Z_TYPE_P(val));
			break;
	}
}

void zendump_zend_array_dump(zend_array *arr, int level)
{
	int idx;
	for(idx = 0; idx < arr->nNumUsed; ++idx) {
		Bucket *bucket = arr->arData + idx;
		if(Z_TYPE(bucket->val) != IS_UNDEF) {
			if(bucket->key) {
				zend_string *str = zendump_unescape_zend_string(bucket->key, 0);
				php_printf("%*c[\"%s\"] len(%zd) addr(0x" ZEND_XLONG_FMT ")", level, ' ', ZSTR_VAL(str), ZSTR_LEN(bucket->key), bucket->key);
				if(IS_INTERNED(bucket->key)) {
					PUTS(" interned =>\n");
				} else {
					php_printf(" refcount(%u) =>\n", GC_REFCOUNT(bucket->key));
				}
				if(str != bucket->key) {
					zend_string_release(str);
				}
			} else {
				php_printf("%*c[%d] =>\n", level, ' ', bucket->h);
			}
			zendump_zval_dump(&bucket->val, level);
		}
	}
}

void zendump_string_to_lower(char *buf, int len)
{
	int idx;
	for(idx = 0; idx < len; ++idx) {
		if(buf[idx] >= 'A' && buf[idx] <= 'Z') {
			buf[idx] = buf[idx] + 'a' - 'A';
		}
	}
}

PHP_FUNCTION(zendump)
{
	zval *args;
	int argc;
	int i;

	ZEND_PARSE_PARAMETERS_START(1, -1)
		Z_PARAM_VARIADIC('+', args, argc)
	ZEND_PARSE_PARAMETERS_END();

	for (i = 0; i < argc; i++) {
		zendump_zval_dump(&args[i], 0);
	}
}

PHP_FUNCTION(zendump_symbols)
{
	zend_execute_data *prev = EX(prev_execute_data);

	if(!prev) {
		return;
	}

	if(!prev->symbol_table) {
		PHPWRITE("null\n", 5);
		return;
	}

	php_printf("symbols(%d): {\n", prev->symbol_table->nNumOfElements);
	zendump_zend_array_dump(prev->symbol_table, INDENT_SIZE);
	PUTS("}\n");
}

PHP_FUNCTION(zendump_statics)
{
	zend_execute_data *prev = EX(prev_execute_data);

	if(!prev || !prev->func || prev->func->type != ZEND_USER_FUNCTION) {
		return;
	}

	if(!prev->func->op_array.static_variables) {
		PHPWRITE("null\n", 5);
		return;
	}

	php_printf("statics(%d): {\n", prev->func->op_array.static_variables->nNumOfElements);
	zendump_zend_array_dump(prev->func->op_array.static_variables, INDENT_SIZE);
	PUTS("}\n");
}

PHP_FUNCTION(zendump_vars)
{
	int idx;
	zend_execute_data *prev = EX(prev_execute_data);

	if(!prev || !prev->func || prev->func->type != ZEND_USER_FUNCTION) {
		return;
	}

	php_printf("vars(%d): {\n", prev->func->op_array.last_var);

	for(idx = 0; idx < prev->func->op_array.last_var; ++idx) {
		zend_string *var = prev->func->op_array.vars[idx];
		php_printf("%*c$", INDENT_SIZE, ' ');
		PHPWRITE(ZSTR_VAL(var), ZSTR_LEN(var));
		PUTS(" ->\n");

		zval *val = ZEND_CALL_VAR_NUM(prev, idx);
		zendump_zval_dump(val, INDENT_SIZE);
	}

	PUTS("}\n");
}

PHP_FUNCTION(zendump_args)
{
	int idx;
	zend_execute_data *prev = EX(prev_execute_data);

	if(!prev || !prev->func || prev->func->type != ZEND_USER_FUNCTION) {
		return;
	}

	php_printf("args(%d): {\n", ZEND_CALL_NUM_ARGS(prev));

	for(idx = 0; idx < prev->func->op_array.num_args; ++idx) {
		zval *val = ZEND_CALL_VAR_NUM(prev, idx);
		zendump_zval_dump(val, INDENT_SIZE);
	}

	for(idx = 0; idx < ZEND_CALL_NUM_ARGS(prev) - prev->func->op_array.num_args; ++idx) {
		zval *val = ZEND_CALL_VAR_NUM(prev, prev->func->op_array.last_var + prev->func->op_array.T + idx);
		zendump_zval_dump(val, INDENT_SIZE);
	}

	PUTS("}\n");
}

PHP_FUNCTION(zendump_literals)
{
	int idx;
	zend_execute_data *prev = EX(prev_execute_data);

	if(!prev || !prev->func || prev->func->type != ZEND_USER_FUNCTION) {
		return;
	}

	php_printf("literals(%d): {\n", prev->func->op_array.last_literal);
	for(idx = 0; idx < prev->func->op_array.last_literal; ++idx) {
		zval *val = prev->func->op_array.literals + idx;
		zendump_zval_dump(val, INDENT_SIZE);
	}

	PUTS("}\n");
}

PHP_FUNCTION(zendump_opcodes)
{
	zend_long column_width = 35;
	zend_execute_data *prev = EX(prev_execute_data);

	ZEND_PARSE_PARAMETERS_START(0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_LONG(column_width)
	ZEND_PARSE_PARAMETERS_END();

	if(!prev || !prev->func || prev->func->type != ZEND_USER_FUNCTION) {
		return;
	}

	zendump_zend_function_dump(prev->func, column_width);
}

PHP_FUNCTION(zendump_function)
{
	zval *val = NULL;
	char *buf = NULL;
	size_t buf_len;
	zend_long column_width = 35;

	ZEND_PARSE_PARAMETERS_START(1, 2)
		Z_PARAM_STRING(buf, buf_len)
		Z_PARAM_OPTIONAL
		Z_PARAM_LONG(column_width)
	ZEND_PARSE_PARAMETERS_END();

	zendump_string_to_lower(buf, buf_len);

	val = zend_hash_str_find(EG(function_table), buf, buf_len);
	if(!val || !Z_FUNC_P(val)) {
		return;
	}

	zendump_zend_function_dump(Z_FUNC_P(val), column_width);
}

PHP_FUNCTION(zendump_class)
{
	zval *val = NULL;
	char *buf = NULL;
	size_t buf_len;
	zend_long column_width = 35;
	zend_long show_magic_functions = 0;

	ZEND_PARSE_PARAMETERS_START(1, 3)
		Z_PARAM_STRING(buf, buf_len)
		Z_PARAM_OPTIONAL
		Z_PARAM_LONG(show_magic_functions)
		Z_PARAM_LONG(column_width)
	ZEND_PARSE_PARAMETERS_END();

	zendump_string_to_lower(buf, buf_len);

	val = zend_hash_str_find(EG(class_table), buf, buf_len);
	if(!val || !Z_CE_P(val)) {
		return;
	}

	zendump_zend_class_entry_dump(Z_CE_P(val), show_magic_functions, column_width);
}

PHP_FUNCTION(zendump_method)
{
	zval *val = NULL;
	char *klass = NULL, *method = NULL;
	size_t klass_len, method_len;
	zend_long column_width = 35;

	ZEND_PARSE_PARAMETERS_START(2, 3)
		Z_PARAM_STRING(klass, klass_len)
		Z_PARAM_STRING(method, method_len)
		Z_PARAM_OPTIONAL
		Z_PARAM_LONG(column_width)
	ZEND_PARSE_PARAMETERS_END();

	zendump_string_to_lower(klass, klass_len);
	zendump_string_to_lower(method, method_len);

	val = zend_hash_str_find(EG(class_table), klass, klass_len);
	if(!val || !Z_CE_P(val)) {
		return;
	}

	val = zend_hash_str_find(&Z_CE_P(val)->function_table, method, method_len);
	if(!val || !Z_FUNC_P(val)) {
		return;
	}

	zendump_zend_function_dump(Z_FUNC_P(val), column_width);
}

/* {{{ php_zendump_init_globals
*/
static void php_zendump_init_globals()
{
	ZENDUMP_G(enable_trace) = 0;
	ZENDUMP_G(global_value) = 0;
	ZENDUMP_G(global_string) = NULL;
}
/* }}} */

/* {{{ PHP_MINIT_FUNCTION
 */
PHP_MINIT_FUNCTION(zendump)
{
	php_zendump_init_globals();

	REGISTER_INI_ENTRIES();

	if (ZENDUMP_G(enable_trace))
	{
		ZENDUMP_G(origin_execute) = zend_execute_ex;
		zend_execute_ex = zendump_execute;
	}

	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MSHUTDOWN_FUNCTION
 */
PHP_MSHUTDOWN_FUNCTION(zendump)
{
	if (ZENDUMP_G(enable_trace))
	{
		if (zend_execute_ex == zendump_execute && ZENDUMP_G(origin_execute) != NULL)
		{
			zend_execute_ex = ZENDUMP_G(origin_execute);
			ZENDUMP_G(origin_execute) = NULL;
		}
	}

	UNREGISTER_INI_ENTRIES();

	return SUCCESS;
}
/* }}} */

/* Remove if there's nothing to do at request start */
/* {{{ PHP_RINIT_FUNCTION
 */
PHP_RINIT_FUNCTION(zendump)
{
#if defined(COMPILE_DL_ZENDUMP) && defined(ZTS)
	ZEND_TSRMLS_CACHE_UPDATE();
#endif
	return SUCCESS;
}
/* }}} */

/* Remove if there's nothing to do at request end */
/* {{{ PHP_RSHUTDOWN_FUNCTION
 */
PHP_RSHUTDOWN_FUNCTION(zendump)
{
	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MINFO_FUNCTION
 */
PHP_MINFO_FUNCTION(zendump)
{
	php_info_print_table_start();
	php_info_print_table_header(2, "zendump support", "enabled");
	php_info_print_table_end();

	DISPLAY_INI_ENTRIES();
}
/* }}} */

ZEND_BEGIN_ARG_INFO_EX(arginfo_zendump, 0, 0, 1)
	ZEND_ARG_VARIADIC_INFO(0, vars)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_zendump_vars, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_zendump_args, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_zendump_symbols, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_zendump_statics, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_zendump_literals, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_zendump_opcodes, 0)
	ZEND_ARG_INFO(0, column_width)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_zendump_function, 0)
	ZEND_ARG_INFO(0, name)
	ZEND_ARG_INFO(0, column_width)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_zendump_class, 0)
	ZEND_ARG_INFO(0, name)
	ZEND_ARG_INFO(0, show_magic_functions)
	ZEND_ARG_INFO(0, column_width)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_zendump_method, 0)
	ZEND_ARG_INFO(0, class_name)
	ZEND_ARG_INFO(0, method_name)
	ZEND_ARG_INFO(0, column_width)
ZEND_END_ARG_INFO()

/* {{{ zendump_functions[]
 *
 * Every user visible function must have an entry in zendump_functions[].
 */
const zend_function_entry zendump_functions[] = {
	PHP_FE(zendump,          arginfo_zendump)
	PHP_FE(zendump_vars,     arginfo_zendump_vars)
	PHP_FE(zendump_args,     arginfo_zendump_args)
	PHP_FE(zendump_symbols,  arginfo_zendump_symbols)
	PHP_FE(zendump_statics,  arginfo_zendump_statics)
	PHP_FE(zendump_literals, arginfo_zendump_literals)
	PHP_FE(zendump_opcodes,  arginfo_zendump_opcodes)
	PHP_FE(zendump_function, arginfo_zendump_function)
	PHP_FE(zendump_class,    arginfo_zendump_class)
	PHP_FE(zendump_method,   arginfo_zendump_method)
	PHP_FE_END /* Must be the last line in zendump_functions[] */
};
/* }}} */

/* {{{ zendump_module_entry
 */
zend_module_entry zendump_module_entry = {
	STANDARD_MODULE_HEADER,
	"zendump",
	zendump_functions,
	PHP_MINIT(zendump),
	PHP_MSHUTDOWN(zendump),
	NULL, // PHP_RINIT(zendump),		/* Replace with NULL if there's nothing to do at request start */
	NULL, // PHP_RSHUTDOWN(zendump),	/* Replace with NULL if there's nothing to do at request end */
	PHP_MINFO(zendump),
	PHP_ZENDUMP_VERSION,
	STANDARD_MODULE_PROPERTIES
};
/* }}} */

#ifdef COMPILE_DL_ZENDUMP
#ifdef ZTS
ZEND_TSRMLS_CACHE_DEFINE()
#endif
ZEND_GET_MODULE(zendump)
#endif

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
