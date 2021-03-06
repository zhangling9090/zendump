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

void zendump_operand_value(zval *val, int column_width);
void zendump_znode_op_dump(znode_op *op, zend_uchar type, uint32_t flags, zend_op *opcode, zend_op_array *op_array, int column_width);
void zendump_zend_op_dump(zend_op *opcode, zend_op_array *op_array, int column_width);
void zendump_zend_op_array_dump(zend_op_array *op_array, int column_width);

void zendump_zend_function_dump(zend_function *function, int column_width)
{
	php_printf("%s(\"%s%s%s\")", ZEND_USER_CODE(function->type) ? "op_array" : "internal_function", (function->common.scope && function->common.scope->name) ? ZSTR_VAL(function->common.scope->name) : "", (function->common.scope && function->common.scope->name) ? "::" : "", function->common.function_name ? ZSTR_VAL(function->common.function_name) : "");

	zendump_zend_function_proto_dump(function, 1);

	php_printf(" flags(0x%I32x)", function->common.fn_flags);

	if(ZEND_USER_CODE(function->type)) {
		zendump_zend_op_array_dump(&function->op_array, column_width);
	} else if(function->type == ZEND_INTERNAL_FUNCTION) {
		php_printf(" handler(0x" ZEND_XLONG_FMT ")", function->internal_function.handler);
		if(function->internal_function.module) {
			php_printf(" module(%d,\"%s\",\"%s\")\n", function->internal_function.module->module_number, function->internal_function.module->name, function->internal_function.module->version);
		}
	}
}

void zendump_zend_op_array_dump(zend_op_array *op_array, int column_width)
{
	int idx;
	const char *columns[] = {"OPCODE", "OP1", "OP2", "RESULT", "EXTENDED"};

	if(op_array->refcount) {
		php_printf(" refcount(%u)", *op_array->refcount);
	}
	php_printf(" addr(0x" ZEND_XLONG_FMT ") vars(%u) T(%u)", op_array, op_array->last_var, op_array->T);
	if(op_array->filename) {
		php_printf(" filename(%s) line(%u,%u)\n", ZSTR_VAL(op_array->filename), op_array->line_start, op_array->line_end);
	}

	for(idx = 0; idx < ARRAY_LENGTH(columns); ++idx) {
		php_printf("%-*s", column_width, columns[idx]);
	}
	PUTS("\n");

	for(idx = 0; idx < op_array->last; ++idx) {
		zendump_zend_op_dump(op_array->opcodes + idx, op_array, column_width);
	}
}

void zendump_zend_function_proto_dump(zend_function *function, int level)
{
	uint32_t idx;
	uint32_t count = function->common.num_args;
	if(!function->common.function_name) {
		return;
	}
	if(function->common.fn_flags & ZEND_ACC_VARIADIC) {
		++count;
	}
	if(level > 0) {
		php_printf("%*c", level, ' ');
	}
	php_printf("%s%s(", (function->common.fn_flags & ZEND_ACC_RETURN_REFERENCE) ? "&" : "", ZSTR_VAL(function->common.function_name));
	for(idx = 0; idx < count; ++idx) {
		zend_arg_info *info = function->common.arg_info + idx;
		if(info->name) {
			const char *type = NULL;
			if(function->common.fn_flags & ZEND_ACC_HAS_TYPE_HINTS) {
#if PHP_API_VERSION >= 20170718
				if(ZEND_TYPE_IS_SET(info->type)) {
					if(ZEND_TYPE_IS_CODE(info->type)) {
						type = zendump_get_type_name(ZEND_TYPE_CODE(info->type));
					} else if(ZEND_TYPE_IS_CLASS(info->type)) {
						type = ZSTR_VAL(ZEND_TYPE_NAME(info->type));
					}
				}
#else
				if(info->type_hint == IS_OBJECT && info->class_name) {
					if(ZEND_USER_CODE(function->type)) {
						type = ZSTR_VAL(info->class_name);
					} else {
						type = (const char*)info->class_name;
					}
				} else if(info->type_hint != IS_UNDEF) {
					type = zendump_get_type_name(info->type_hint);
				}
#endif
			}
			php_printf("%s%s%s%s%s$%s", idx ? ", " : "", info->is_variadic ? "..." : "", type ? type : "", type ? " " : "", info->pass_by_reference ? "&" : "", (ZEND_USER_CODE(function->type) ? ZSTR_VAL(info->name) : (const char*)info->name));
		}
	}
	PUTS(")");
}

#define DUMP_EVAL_KEYWORD_BY_ENTENDED_VALUE(extended_value, column_width) do{\
	const char* names[] = {NULL, "eval", "include", "include_once", "require", "require_once"};\
	uint32_t idx = 0, value = (extended_value);\
	while(value) {\
		++idx;\
		value >>= 1;\
	}\
	if(idx > 5) {\
		idx = 0;\
	}\
	php_printf("%-*s", (column_width), names[idx]);\
} while(0)

void zendump_zend_op_dump(zend_op *opcode, zend_op_array *op_array, int column_width)
{
	uint32_t flags = zend_get_opcode_flags(opcode->opcode);
	const char *op_str = zend_get_opcode_name(opcode->opcode);
	php_printf("%-*s", column_width, op_str);

	zendump_znode_op_dump(&opcode->op1, opcode->op1_type, ZEND_VM_OP1_FLAGS(flags), opcode, op_array, column_width);
	zendump_znode_op_dump(&opcode->op2, opcode->op2_type, ZEND_VM_OP2_FLAGS(flags), opcode, op_array, column_width);
	zendump_znode_op_dump(&opcode->result, opcode->result_type, 0, opcode, op_array, column_width);

#if PHP_API_VERSION >= 20160303
	if(flags & ZEND_VM_EXT_MASK) {
		switch(flags & ZEND_VM_EXT_MASK) {
			case ZEND_VM_EXT_NUM:
				php_printf("%-*d", column_width, opcode->extended_value);
				break;
			case ZEND_VM_EXT_JMP_ADDR:
				php_printf("%-*d", column_width, opcode->extended_value / sizeof(zend_op));
				break;
			case ZEND_VM_EXT_DIM_OBJ:
				break;
			case ZEND_VM_EXT_CLASS_FETCH:
				break;
			case ZEND_VM_EXT_CONST_FETCH:
				break;
			case ZEND_VM_EXT_TYPE:
				php_printf("%-*s", column_width, zendump_get_type_name(opcode->extended_value));
				break;
			case ZEND_VM_EXT_EVAL: {
				DUMP_EVAL_KEYWORD_BY_ENTENDED_VALUE(opcode->extended_value, column_width);
				break;
			}
			case ZEND_VM_EXT_SRC:
				if(opcode->extended_value == ZEND_RETURNS_VALUE) {
					php_printf("%-*s", column_width, "value");
				} else if(opcode->extended_value == ZEND_RETURNS_FUNCTION) {
					php_printf("%-*s", column_width, "function");
				} else {
					php_printf("%*c", column_width, ' ');
				}
				break;
			default: {
				php_printf("%*c", column_width, ' ');
			}
		}
	} else {
		if(flags & ZEND_VM_EXT_ISSET) {
			if(opcode->extended_value & ZEND_ISSET) {
				php_printf("isset%*c", column_width - 5, ' ');
			} else /* if (opline->extended_value & ZEND_ISEMPTY) */ {
				php_printf("empty%*c", column_width - 5, ' ');
			}
		} else {
			if((flags & ZEND_VM_EXT_VAR_FETCH) || (flags & ZEND_VM_EXT_ARG_NUM) || (flags & ZEND_VM_EXT_ARRAY_INIT) || (flags & ZEND_VM_EXT_REF)) {
			}
			php_printf("%*c", column_width, ' ');
		}
	}
#else
	if(opcode->opcode == ZEND_INCLUDE_OR_EVAL) {
		DUMP_EVAL_KEYWORD_BY_ENTENDED_VALUE(opcode->extended_value, column_width);
	} else if(opcode->opcode == ZEND_CAST || opcode->opcode == ZEND_TYPE_CHECK) {
		php_printf("%-*s", column_width, zendump_get_type_name(opcode->extended_value));
	} else if(opcode->opcode == ZEND_JMPZNZ ||
		opcode->opcode == ZEND_CATCH ||
		opcode->opcode == ZEND_FE_FETCH_R ||
		opcode->opcode == ZEND_FE_FETCH_RW ||
		opcode->opcode == ZEND_DECLARE_ANON_CLASS ||
		opcode->opcode == ZEND_DECLARE_ANON_INHERITED_CLASS) { // ZEND_SWITCH_LONG ZEND_SWITCH_STRING
		php_printf("%-*d", column_width, opcode->extended_value / sizeof(zend_op));
	} else if(opcode->opcode == ZEND_FETCH_DIM_FUNC_ARG ||
		opcode->opcode == ZEND_FETCH_OBJ_FUNC_ARG ||
		opcode->opcode == ZEND_ROPE_INIT ||
		opcode->opcode == ZEND_ROPE_ADD ||
		opcode->opcode == ZEND_ROPE_END ||
		opcode->opcode == ZEND_INIT_METHOD_CALL ||
		opcode->opcode == ZEND_INIT_STATIC_METHOD_CALL ||
		opcode->opcode == ZEND_INIT_FCALL_BY_NAME ||
		opcode->opcode == ZEND_INIT_DYNAMIC_CALL ||
		opcode->opcode == ZEND_INIT_USER_CALL ||
		opcode->opcode == ZEND_INIT_NS_FCALL_BY_NAME ||
		opcode->opcode == ZEND_INIT_FCALL ||
		opcode->opcode == ZEND_SEND_ARRAY ||
		opcode->opcode == ZEND_NEW ||
		opcode->opcode == ZEND_TICKS) { // ZEND_FETCH_STATIC_PROP_FUNC_ARG ZEND_IN_ARRAY
		php_printf("%-*d", column_width, opcode->extended_value);
	} else if(opcode->opcode == ZEND_ASSIGN_REF ||
		opcode->opcode == ZEND_RETURN_BY_REF ||
		opcode->opcode == ZEND_YIELD) {
		if(opcode->extended_value == ZEND_RETURNS_VALUE) {
			php_printf("%-*s", column_width, "value");
		} else if(opcode->extended_value == ZEND_RETURNS_FUNCTION) {
			php_printf("%-*s", column_width, "function");
		} else {
			php_printf("%*c", column_width, ' ');
		}
	} else if(opcode->opcode == ZEND_ISSET_ISEMPTY_VAR ||
		opcode->opcode == ZEND_ISSET_ISEMPTY_DIM_OBJ ||
		opcode->opcode == ZEND_ISSET_ISEMPTY_PROP_OBJ) {
		if(opcode->extended_value & ZEND_ISSET) {
			php_printf("isset%*c", column_width - 5, ' ');
		} else /* if (opline->extended_value & ZEND_ISEMPTY) */ {
			php_printf("empty%*c", column_width - 5, ' ');
		}
	} else {
		php_printf("%*c", column_width, ' ');
	}
#endif
	PUTS("\n");
}

void zendump_znode_op_dump(znode_op *op, zend_uchar type, uint32_t flags, zend_op *opcode, zend_op_array *op_array, int column_width)
{
	switch(type) {
		case IS_CONST: {
#if SIZEOF_SIZE_T == 8
			zval *val = (zval*)((char*)op_array->literals + op->constant);
#elif SIZEOF_SIZE_T == 4
			zval *val = op->zv;
#else
# error "Unknown SIZEOF_SIZE_T"
#endif
			zendump_operand_value(val, column_width);
			break;
		}
		case IS_CV: {
			int index = EX_OFFSET_TO_VAR_IDX(op->var);
			if(index < op_array->last_var) {
				zend_string *var = op_array->vars[index];
				php_printf("$%-*s", column_width - 1, ZSTR_VAL(var));
			} else {
				php_printf("%*c", column_width, ' ');
			}
			break;
		}
		case IS_TMP_VAR: {
			int index = EX_OFFSET_TO_VAR_IDX(op->var) - op_array->last_var;
			php_printf("#tmp%-*d", column_width - 4, index);
			break;
		}
		case IS_VAR: {
			int index = EX_OFFSET_TO_VAR_IDX(op->var) - op_array->last_var;
			php_printf("#var%-*d", column_width - 4, index);
			break;
		}
		case IS_UNUSED:
			break;
		default:
			php_printf("%*c", column_width, ' ');
	}
	if(type == IS_UNUSED) {
#if PHP_API_VERSION >= 20160303
		int padding = 0;
		uint32_t flagh = flags & ZEND_VM_OP_MASK;
		switch(flagh) {
			case ZEND_VM_OP_NUM:
				php_printf("%-*d", column_width, op->num);
				break;
			case ZEND_VM_OP_JMP_ADDR:
				php_printf("%-*d", column_width, OP_JMP_ADDR(opcode, *op) - opcode - 1);
				break;
			case ZEND_VM_OP_TRY_CATCH:
			case ZEND_VM_OP_LIVE_RANGE:
			case ZEND_VM_OP_THIS:
			case ZEND_VM_OP_NEXT:
				padding = column_width;
				break;
			case ZEND_VM_OP_CLASS_FETCH: {
				const char *fetch_type[] = {"default", "self", "parent", "static", "auto", "interface", "trait"};
				php_printf("%-*s", column_width, fetch_type[op->num & ZEND_FETCH_CLASS_MASK]);
				break;
			}
			case ZEND_VM_OP_CONSTRUCTOR:
			default:
				padding = column_width;
		}
		if(padding) {
			php_printf("%*c", padding, ' ');
		}
#else
		if((op == &opcode->op1 && (opcode->opcode == ZEND_JMP ||
				opcode->opcode == ZEND_FAST_CALL)) ||
			(op == &opcode->op2 && (opcode->opcode == ZEND_JMPZ ||
				opcode->opcode == ZEND_JMPNZ ||
				opcode->opcode == ZEND_JMPZNZ ||
				opcode->opcode == ZEND_JMPZ_EX ||
				opcode->opcode == ZEND_JMPNZ_EX ||
				opcode->opcode == ZEND_JMP_SET ||
				opcode->opcode == ZEND_FE_RESET_R ||
				opcode->opcode == ZEND_FE_RESET_RW ||
				opcode->opcode == ZEND_COALESCE ||
				opcode->opcode == ZEND_ASSERT_CHECK))) {
			php_printf("%-*d", column_width, OP_JMP_ADDR(opcode, *op) - opcode - 1);
		} else if((op == &opcode->op1 && (opcode->opcode == ZEND_RECV ||
				opcode->opcode == ZEND_RECV_INIT ||
				opcode->opcode == ZEND_RECV_VARIADIC ||
				opcode->opcode == ZEND_INIT_FCALL)) ||
			(op == &opcode->op2 && (opcode->opcode == ZEND_SEND_VAL_EX ||
				opcode->opcode == ZEND_SEND_VAR_EX ||
				opcode->opcode == ZEND_SEND_VAL ||
				opcode->opcode == ZEND_SEND_VAR ||
				opcode->opcode == ZEND_SEND_VAR_NO_REF ||
				opcode->opcode == ZEND_SEND_REF ||
				opcode->opcode == ZEND_SEND_USER))) { // ZEND_SEND_VAR_NO_REF_EX
			php_printf("%-*d", column_width, op->num);
		} else {
			php_printf("%*c", column_width, ' ');
		}
#endif
	}
}

void zendump_operand_value(zval *val, int column_width)
{
	switch(Z_TYPE_P(val)) {
		case IS_UNDEF:
			php_printf("%-*s", column_width, "undefined");
			break;
		case IS_NULL:
			php_printf("%-*s", column_width, "null");
			break;
		case IS_FALSE:
			php_printf("%-*s", column_width, "false");
			break;
		case IS_TRUE:
			php_printf("%-*s", column_width, "true");
			break;
		case IS_LONG:
			php_printf("%-*" ZEND_LONG_FMT_SPEC, column_width, Z_LVAL_P(val));
			break;
		case IS_DOUBLE:
			php_printf("%-*.*G", column_width, (int) EG(precision), Z_DVAL_P(val));
			break;
		case IS_STRING: {
			zend_string *str = zendump_unescape_zend_string(Z_STR_P(val), 0);
			PUTS("\"");
			PHPWRITE(ZSTR_VAL(str), ZSTR_LEN(str));
			PUTS("\"");
			if(column_width > ZSTR_LEN(str) + 2) {
				php_printf("%*c", column_width - 2 - ZSTR_LEN(str), ' ');
			}
			if(str != Z_STR_P(val)) {
				zend_string_release(str);
			}
			break;
		}
		case IS_ARRAY:
			php_printf("array:0x%-*" ZEND_XLONG_FMT_SPEC, column_width - 8, Z_ARRVAL_P(val));
			break;
		case IS_OBJECT:
			php_printf("object:0x%-*" ZEND_XLONG_FMT_SPEC, column_width - 9, Z_OBJ_P(val));
			break;
		case IS_RESOURCE:
			php_printf("resource:0x%-*" ZEND_XLONG_FMT_SPEC, column_width - 11, Z_RES_P(val));
			break;
		case IS_REFERENCE:
			php_printf("reference:0x%-*" ZEND_XLONG_FMT_SPEC, column_width - 12, Z_REF_P(val));
			break;
		case IS_INDIRECT:
			zendump_operand_value(Z_INDIRECT_P(val), column_width);
			break;
		default:
			php_printf("unknown:0x%-*" ZEND_XLONG_FMT_SPEC, column_width - 10, val);
			break;
	}
}
