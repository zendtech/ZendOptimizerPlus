if (ZEND_OPTIMIZER_PASS_4 & OPTIMIZATION_LEVEL) {
	zend_op *opline;
	zend_op *end = op_array->opcodes + op_array->last;

	opline = op_array->opcodes;
	while (opline < end) {
		switch (opline->opcode) {
			case ZEND_DO_FCALL:
				if (opline->extended_value == 1 && (opline - 1)->opcode == ZEND_SEND_VAL &&
					ZEND_OP1_TYPE(opline - 1) == IS_CONST && ZEND_OP1_LITERAL(opline - 1).type == IS_STRING &&
					ZEND_OP1_TYPE(opline) == IS_CONST && ZEND_OP1_LITERAL(opline).type == IS_STRING) {
					if (!memcmp(Z_STRVAL(ZEND_OP1_LITERAL(opline)), "function_exists", sizeof("function_exists"))) {
						zend_internal_function *func;
						char *lc_name = zend_str_tolower_dup(
								Z_STRVAL(ZEND_OP1_LITERAL(opline - 1)), Z_STRLEN(ZEND_OP1_LITERAL(opline - 1)));
						if (zend_hash_find(EG(function_table), lc_name, Z_STRLEN(ZEND_OP1_LITERAL(opline - 1)) + 1,
									(void *)&func) == SUCCESS &&
								func->type == ZEND_INTERNAL_FUNCTION &&
								func->module->type == MODULE_PERSISTENT) {
							zval t;
							ZVAL_BOOL(&t, 1);
							if (replace_var_by_const(op_array, opline + 1, ZEND_RESULT(opline).var, &t TSRMLS_CC)) {
								literal_dtor(&ZEND_OP1_LITERAL(opline - 1));
								MAKE_NOP((opline - 1));
								literal_dtor(&ZEND_OP1_LITERAL(opline));
								MAKE_NOP(opline);
							}
						}
						efree(lc_name);
					} else if (!memcmp(Z_STRVAL(ZEND_OP1_LITERAL(opline)),
								"extension_loaded", sizeof("extension_loaded"))) {
						zval t;
						zend_op *next;
						zend_module_entry *m;
						char *lc_name = zend_str_tolower_dup(
								Z_STRVAL(ZEND_OP1_LITERAL(opline - 1)), Z_STRLEN(ZEND_OP1_LITERAL(opline - 1)));
						if (zend_hash_find(&module_registry,
								lc_name, Z_STRLEN(ZEND_OP1_LITERAL(opline - 1)) + 1, (void *)&m) == FAILURE) {
							if (!PG(enable_dl)) {
								break;
							} else {
								ZVAL_BOOL(&t, 0);
							}
						} else {
							if (m->type == MODULE_PERSISTENT) {
								ZVAL_BOOL(&t, 1);
							} else {
								break;
							}
						} 
					
						if (replace_var_by_const(op_array, opline + 1, ZEND_RESULT(opline).var, &t TSRMLS_CC)) {
							literal_dtor(&ZEND_OP1_LITERAL(opline - 1));
							MAKE_NOP((opline - 1));
							literal_dtor(&ZEND_OP1_LITERAL(opline));
							MAKE_NOP(opline);
						}
						efree(lc_name);
					} else if (!memcmp(Z_STRVAL(ZEND_OP1_LITERAL(opline)), "defined", sizeof("defined"))) {
						zval t;
						if (zend_get_persistent_constant(Z_STRVAL(ZEND_OP1_LITERAL(opline - 1)),
							Z_STRLEN(ZEND_OP1_LITERAL(opline - 1)), &t, 0 TSRMLS_CC)) {

							ZVAL_BOOL(&t, 1);
							if (replace_var_by_const(op_array, opline + 1, ZEND_RESULT(opline).var, &t TSRMLS_CC)) {
								literal_dtor(&ZEND_OP1_LITERAL(opline - 1));
								MAKE_NOP((opline - 1));
								literal_dtor(&ZEND_OP1_LITERAL(opline));
								MAKE_NOP(opline);
							}
						}
					} else if (!memcmp(Z_STRVAL(ZEND_OP1_LITERAL(opline)), "strlen", sizeof("strlen"))) {
						zval t;
						ZVAL_LONG(&t, Z_STRLEN(ZEND_OP1_LITERAL(opline - 1)));
						if (replace_var_by_const(op_array, opline + 1, ZEND_RESULT(opline).var, &t TSRMLS_CC)) {
							literal_dtor(&ZEND_OP1_LITERAL(opline - 1));
							MAKE_NOP((opline - 1));
							literal_dtor(&ZEND_OP1_LITERAL(opline));
							MAKE_NOP(opline);
						}
					}
				}
				break;
			default:
				break;
		}
		opline++;
	}
}
