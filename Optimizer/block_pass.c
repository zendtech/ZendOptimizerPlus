#define DEBUG_BLOCKPASS 0

/* Checks if a constant (like "true") may be replaced by its value */
static int zend_get_persistent_constant(char *name, uint name_len, zval *result, int copy TSRMLS_DC ELS_DC)
{
	zend_constant *c;
	char *lookup_name;
	int retval = 1;
	ALLOCA_FLAG(use_heap);

	if (zend_hash_find(EG(zend_constants), name, name_len + 1, (void **) &c) == FAILURE) {
		lookup_name = DO_ALLOCA(name_len + 1);
		memcpy(lookup_name, name, name_len + 1);
		zend_str_tolower(lookup_name, name_len);

		if (zend_hash_find(EG(zend_constants), lookup_name, name_len + 1, (void **) &c) == SUCCESS) {
			if (!(c->flags & CONST_CT_SUBST) || (c->flags & CONST_CS)) {
				retval = 0;
			}
		} else {
			retval = 0;
		}
		FREE_ALLOCA(lookup_name);
	}

	if (retval) {
		if (c->flags & CONST_PERSISTENT) {
			*result = c->value;
			if (copy) {
				zval_copy_ctor(result);
			}
		} else {
			retval = 0;
		}
	}

	return retval;
}

#if DEBUG_BLOCKPASS
# define BLOCK_REF(b) b?op_array->opcodes-b->start_opline:-1

static inline void print_block(zend_code_block *block, zend_op *opcodes, char *txt)
{
	fprintf(stderr, "%sBlock: %d-%d (%d)", txt, block->start_opline - opcodes, block->start_opline - opcodes + block->len - 1, block->len);
	if (!block->access) {
		fprintf(stderr, " unused");
	}
	if (block->op1_to) {
		fprintf(stderr, " 1: %d", block->op1_to->start_opline - opcodes);
	}
	if (block->op2_to) {
		fprintf(stderr, " 2: %d", block->op2_to->start_opline - opcodes);
	}
	if (block->ext_to) {
		fprintf(stderr, " e: %d", block->ext_to->start_opline - opcodes);
	}
	if (block->follow_to) {
		fprintf(stderr, " f: %d", block->follow_to->start_opline - opcodes);
	}

	if (block->sources) {
		zend_block_source *bs = block->sources;
		fprintf(stderr, " s:");
		while (bs) {
			fprintf(stderr, " %d", bs->from->start_opline - opcodes);
			bs = bs->next;
		}
	}

	fprintf(stderr, "\n");
	fflush(stderr);
}
#else
#define print_block(a,b,c)
#endif

#define START_BLOCK_OP(opno) blocks[opno].start_opline = &op_array->opcodes[opno]; blocks[opno].start_opline_no = opno; blocks[opno].access = 1

/* find code blocks in op_array
   code block is a set of opcodes with single flow of control, i.e. without jmps,
   branches, etc. */
static int find_code_blocks(zend_op_array *op_array, zend_cfg *cfg)
{
	zend_op *opline;
	zend_op *end = op_array->opcodes + op_array->last;
	zend_code_block *blocks, *cur_block;
	zend_uint opno = 0;

	memset(cfg, 0, sizeof(zend_cfg));
	blocks = cfg->blocks = ecalloc(op_array->last + 2, sizeof(zend_code_block));
	opline = op_array->opcodes;
	blocks[0].start_opline = opline;
	blocks[0].start_opline_no = 0;
	while (opline < end) {
		switch((unsigned)opline->opcode) {
			case ZEND_BRK:
			case ZEND_CONT:
#if ZEND_EXTENSION_API_NO >= PHP_5_3_X_API_NO
			case ZEND_GOTO:
#endif
				/* would not optimize non-optimized BRK/CONTs - we cannot
				 really know where it jumps, so these optimizations are
				too dangerous */
				efree(blocks);
				return 0;
#if ZEND_EXTENSION_API_NO > PHP_5_4_X_API_NO
			case ZEND_FAST_CALL:
				START_BLOCK_OP(ZEND_OP1(opline).opline_num);
				break;
#endif
			case ZEND_JMP:
				START_BLOCK_OP(ZEND_OP1(opline).opline_num);
				/* break missing intentionally */
			case ZEND_RETURN:
#if ZEND_EXTENSION_API_NO > PHP_5_3_X_API_NO
			case ZEND_RETURN_BY_REF:
#endif
#if ZEND_EXTENSION_API_NO > PHP_5_4_X_API_NO
			case ZEND_FAST_RET:
#endif
			case ZEND_EXIT:
			case ZEND_THROW:
				/* start new block from this+1 */
				START_BLOCK_OP(opno + 1);
				break;
				/* TODO: if conditional jmp depends on constant,
						 don't start block that won't be executed */
			case ZEND_CATCH:
				START_BLOCK_OP(opline->extended_value);
				START_BLOCK_OP(opno + 1);
				break;
			case ZEND_JMPZNZ:
				START_BLOCK_OP(opline->extended_value);
			case ZEND_JMPZ:
			case ZEND_JMPNZ:
			case ZEND_JMPZ_EX:
			case ZEND_JMPNZ_EX:
			case ZEND_FE_FETCH:
			case ZEND_FE_RESET:
			case ZEND_NEW:
#if ZEND_EXTENSION_API_NO >= PHP_5_3_X_API_NO
			case ZEND_JMP_SET:
#endif
#if ZEND_EXTENSION_API_NO > PHP_5_3_X_API_NO
			case ZEND_JMP_SET_VAR:
#endif
				START_BLOCK_OP(ZEND_OP2(opline).opline_num);
				START_BLOCK_OP(opno + 1);
				break;

		}
		opno++;
		opline++;
	}

	/* first find block start points */
	if (op_array->last_try_catch) {
		int i;
		cfg->try = ecalloc(op_array->last_try_catch, sizeof(zend_code_block *));
		cfg->catch = ecalloc(op_array->last_try_catch, sizeof(zend_code_block *));
		for (i = 0; i< op_array->last_try_catch; i++) {
			cfg->try[i] = &blocks[op_array->try_catch_array[i].try_op];
			cfg->catch[i] = &blocks[op_array->try_catch_array[i].catch_op];
			START_BLOCK_OP(op_array->try_catch_array[i].try_op);
			START_BLOCK_OP(op_array->try_catch_array[i].catch_op);
			blocks[op_array->try_catch_array[i].try_op].protected = 1;
		}
	}
	/* Currently, we don't optimize op_arrays with BRK/CONT/GOTO opcodes,
	 * but, we have to keep brk_cont_array to avoid memory leaks during
	 * exception handling */
	if (op_array->last_brk_cont) {
		int i, j;

		j = 0;
		for (i = 0; i< op_array->last_brk_cont; i++) {
			if (op_array->brk_cont_array[i].start >= 0 &&
			    (op_array->opcodes[op_array->brk_cont_array[i].brk].opcode == ZEND_FREE ||
			     op_array->opcodes[op_array->brk_cont_array[i].brk].opcode == ZEND_SWITCH_FREE)) {
				int parent = op_array->brk_cont_array[i].parent;

				while (parent >= 0 &&
				       op_array->brk_cont_array[parent].start < 0 &&
				       op_array->opcodes[op_array->brk_cont_array[parent].brk].opcode != ZEND_FREE &&
				       op_array->opcodes[op_array->brk_cont_array[parent].brk].opcode != ZEND_SWITCH_FREE) {
					parent = op_array->brk_cont_array[parent].parent;
				}
				op_array->brk_cont_array[i].parent = parent;
				j++;
			}
		}
		if (j) {
			cfg->loop_start = ecalloc(op_array->last_brk_cont, sizeof(zend_code_block *));
			cfg->loop_cont  = ecalloc(op_array->last_brk_cont, sizeof(zend_code_block *));
			cfg->loop_brk   = ecalloc(op_array->last_brk_cont, sizeof(zend_code_block *));
			j = 0;
			for (i = 0; i< op_array->last_brk_cont; i++) {
				if (op_array->brk_cont_array[i].start >= 0 &&
				    (op_array->opcodes[op_array->brk_cont_array[i].brk].opcode == ZEND_FREE ||
				     op_array->opcodes[op_array->brk_cont_array[i].brk].opcode == ZEND_SWITCH_FREE)) {
					if (i != j) {
						op_array->brk_cont_array[j] = op_array->brk_cont_array[i];
					}
					cfg->loop_start[j] = &blocks[op_array->brk_cont_array[j].start];
					cfg->loop_cont[j]  = &blocks[op_array->brk_cont_array[j].cont];
					cfg->loop_brk[j]   = &blocks[op_array->brk_cont_array[j].brk];
					START_BLOCK_OP(op_array->brk_cont_array[j].start);
					START_BLOCK_OP(op_array->brk_cont_array[j].cont);
					START_BLOCK_OP(op_array->brk_cont_array[j].brk);
					blocks[op_array->brk_cont_array[j].start].protected = 1;
					blocks[op_array->brk_cont_array[j].brk].protected = 1;
					j++;
				}
			}
			op_array->last_brk_cont = j;
		} else {
			efree(op_array->brk_cont_array);
			op_array->brk_cont_array = NULL;
			op_array->last_brk_cont = 0;
		}
	}

	/* Build CFG (Control Flow Graph) */
	cur_block = blocks;
	for (opno = 1; opno < op_array->last; opno++) {
		if (blocks[opno].start_opline) {
			/* found new block start */
			cur_block->len = blocks[opno].start_opline - cur_block->start_opline;
			cur_block->next = &blocks[opno];
			/* what is the last OP of previous block? */
			opline = blocks[opno].start_opline - 1;
			switch((unsigned)opline->opcode) {
				case ZEND_RETURN:
#if ZEND_EXTENSION_API_NO > PHP_5_3_X_API_NO
				case ZEND_RETURN_BY_REF:
#endif
#if ZEND_EXTENSION_API_NO > PHP_5_4_X_API_NO
				case ZEND_FAST_RET:
#endif
				case ZEND_EXIT:
				case ZEND_THROW:
					break;
#if ZEND_EXTENSION_API_NO > PHP_5_4_X_API_NO
				case ZEND_FAST_CALL:
#endif
				case ZEND_JMP:
					cur_block->op1_to = &blocks[ZEND_OP1(opline).opline_num];
					break;
				case ZEND_JMPZNZ:
					cur_block->op2_to = &blocks[ZEND_OP2(opline).opline_num];
					cur_block->ext_to = &blocks[opline->extended_value];
					break;
				case ZEND_CATCH:
					cur_block->ext_to = &blocks[opline->extended_value];
					cur_block->follow_to = &blocks[opno];
					break;
				case ZEND_JMPZ:
				case ZEND_JMPNZ:
				case ZEND_JMPZ_EX:
				case ZEND_JMPNZ_EX:
				case ZEND_FE_RESET:
				case ZEND_NEW:
#if ZEND_EXTENSION_API_NO >= PHP_5_3_X_API_NO
				case ZEND_JMP_SET:
#endif
#if ZEND_EXTENSION_API_NO > PHP_5_3_X_API_NO
				case ZEND_JMP_SET_VAR:
#endif
				case ZEND_FE_FETCH:
					cur_block->op2_to = &blocks[ZEND_OP2(opline).opline_num];
					/* break missing intentionally */
			  default:
				  /* next block follows this */
					cur_block->follow_to = &blocks[opno];
					break;
			}
			print_block(cur_block, op_array->opcodes, "");
			cur_block = cur_block->next;
		}
	}
	cur_block->len = end - cur_block->start_opline;
	cur_block->next = &blocks[op_array->last + 1];
	print_block(cur_block, op_array->opcodes, "");

	return 1;
}

/* CFG back references management */

#define ADD_SOURCE(fromb, tob) { \
	zend_block_source *__s = tob->sources; \
    while (__s && __s->from != fromb) __s = __s->next; \
	if (__s == NULL) { \
		zend_block_source *__t = emalloc(sizeof(zend_block_source)); \
		__t->next = tob->sources; \
		tob->sources = __t; \
		__t->from = fromb; \
	} \
}

#define DEL_SOURCE(cs) { \
	zend_block_source *__ns = (*cs)->next; \
	efree(*cs); \
	*cs = __ns; \
}


static inline void replace_source(zend_block_source *list, zend_code_block *old, zend_code_block *new)
{
	/* replace all references to 'old' in 'list' with 'new' */
	zend_block_source **cs;
	int found = 0;

	for (cs = &list; *cs; cs = &((*cs)->next)) {
		if ((*cs)->from == new) {
			if (found) {
				DEL_SOURCE(cs);
			} else {
				found = 1;
			}
		}

		if ((*cs)->from == old) {
			if (found) {
				DEL_SOURCE(cs);
			} else {
				(*cs)->from = new;
				found = 1;
			}
		}
	}
}

static inline void del_source(zend_code_block *from, zend_code_block *to)
{
	/* delete source 'from' from 'to'-s sources list */
	zend_block_source **cs = &to->sources;

	if (to->sources == NULL) {
		to->access = 0;
		return;
	}

	while (*cs) {
		if ((*cs)->from == from) {
		 	DEL_SOURCE(cs);
			break;
		}
		cs = &((*cs)->next);
	}

	if (to->sources == NULL) {
		/* 'to' has no more sources - it's unused, will be stripped */
		to->access = 0;
		return;
	}

	if (to->sources->next == NULL) {
		/* source to only one block */
		zend_code_block *from_block = to->sources->from;

		if (from_block->access && from_block->follow_to == to &&
		    from_block->op1_to == NULL &&
		    from_block->op2_to == NULL &&
		    from_block->ext_to == NULL) {
			/* this block follows it's only predecessor - we can join them */
			zend_op *new_to = from_block->start_opline + from_block->len;
			if (new_to != to->start_opline) {
				/* move block to new location */
				memmove(new_to, to->start_opline, sizeof(zend_op)*to->len);
			}
			/* join blocks' lengths */
			from_block->len += to->len;
			/* move 'to'`s references to 'from' */
			to->start_opline = NULL;
			to->access = 0;
			efree(to->sources);
			to->sources = NULL;
			from_block->follow_to = to->follow_to;
			if (to->op1_to) {
				from_block->op1_to = to->op1_to;
				replace_source(to->op1_to->sources, to, from_block);
			}
			if (to->op2_to) {
				from_block->op2_to = to->op2_to;
				replace_source(to->op2_to->sources, to, from_block);
			}
			if (to->ext_to) {
				from_block->ext_to = to->ext_to;
				replace_source(to->ext_to->sources, to, from_block);
			}
			if (to->follow_to) {
				replace_source(to->follow_to->sources, to, from_block);
			}
			/* remove "to" from list */
		}
	}
}

static void delete_code_block(zend_code_block *block)
{
	if (block->protected) {
		return;
	}
	if (block->follow_to) {
		zend_block_source *bs = block->sources;
		while (bs) {
			zend_code_block *from_block = bs->from;
			zend_code_block *to = block->follow_to;
			if (from_block->op1_to == block) {
				from_block->op1_to = to;
				ADD_SOURCE(from_block, to);
			}
			if (from_block->op2_to == block) {
				from_block->op2_to = to;
				ADD_SOURCE(from_block, to);
			}
			if (from_block->ext_to == block) {
				from_block->ext_to = to;
				ADD_SOURCE(from_block, to);
			}
			if (from_block->follow_to == block) {
				from_block->follow_to = to;
				ADD_SOURCE(from_block, to);
			}
			bs = bs->next;
		}
	}
	block->access = 0;
}

static void zend_access_path(zend_code_block *block)
{
	if (block->access) {
		return;
	}

	block->access = 1;
	if (block->op1_to) {
		zend_access_path(block->op1_to);
		ADD_SOURCE(block, block->op1_to);
	}
	if (block->op2_to) {
		zend_access_path(block->op2_to);
		ADD_SOURCE(block, block->op2_to);
	}
	if (block->ext_to) {
		zend_access_path(block->ext_to);
		ADD_SOURCE(block, block->ext_to);
	}
	if (block->follow_to) {
		zend_access_path(block->follow_to);
		ADD_SOURCE(block, block->follow_to);
	}
}

/* Traverse CFG, mark reachable basic blocks and build back references */
static void zend_rebuild_access_path(zend_cfg *cfg, zend_op_array *op_array, int find_start)
{
	zend_code_block *blocks = cfg->blocks;
	zend_code_block *start = find_start? NULL : blocks;
	zend_code_block *b;

	/* Mark all blocks as unaccessible and destroy back references */
	b = blocks;
	while (b != NULL) {
		zend_block_source *cs;
		if (!start && b->access) {
			start = b;
		}
		b->access = 0;
		cs = b->sources;
		while (cs) {
			zend_block_source *n = cs->next;
			efree(cs);
			cs = n;
		}
		b->sources = NULL;
		b = b->next;
	}

	/* Walk thorough all paths */
	zend_access_path(start);

	/* Add brk/cont paths */
	if (op_array->last_brk_cont) {
		int i;
		for (i=0; i< op_array->last_brk_cont; i++) {
			zend_access_path(cfg->loop_start[i]);
			zend_access_path(cfg->loop_cont[i]);
			zend_access_path(cfg->loop_brk[i]);
		}
	}

	/* Add exception paths */
	if (op_array->last_try_catch) {
		int i;
		for (i=0; i< op_array->last_try_catch; i++) {
			if (!cfg->catch[i]->access) {
				zend_access_path(cfg->catch[i]);
			}
		}
	}
}

/* Data dependencies macros */

#if ZEND_EXTENSION_API_NO > PHP_5_3_X_API_NO

# define VAR_NUM_EX(op) ((op ## _type & (IS_TMP_VAR|IS_VAR))?VAR_NUM((op).var):(op).var)

# define VAR_SOURCE(op) Tsource[VAR_NUM(op.var)]
# define SET_VAR_SOURCE(opline) Tsource[VAR_NUM(opline->result.var)] = opline

# define VAR_UNSET(op) do { if (op ## _type & (IS_TMP_VAR|IS_VAR)) {VAR_SOURCE(op) = NULL;}} while (0)

#else

# define VAR_NUM_EX(op) ((op).op_type == IS_TMP_VAR || (op).op_type == IS_VAR? VAR_NUM((op).u.var) : (op).u.var)

# define VAR_SOURCE(op) Tsource[VAR_NUM(op.u.var)]
# define SET_VAR_SOURCE(opline) Tsource[VAR_NUM(ZEND_RESULT(opline).var)] = opline

# define VAR_UNSET(op) do { if ((op).op_type == IS_TMP_VAR || (op).op_type == IS_VAR) {VAR_SOURCE(op) = NULL;}} while (0)

#endif

#define convert_to_string_safe(v) \
	if (Z_TYPE_P((v)) == IS_NULL) { \
		ZVAL_STRINGL((v), "", 0, 1); \
	} else { \
		convert_to_string((v)); \
	}

static void zend_optimize_block(zend_code_block *block, zend_op_array *op_array, char *used_ext TSRMLS_DC)
{
	zend_op *opline = block->start_opline;
	zend_op *end, *last_op = NULL;
	zend_op **Tsource = NULL;

	print_block(block, op_array->opcodes, "Opt ");

	/* remove leading NOPs */
	while (block->len > 0 && block->start_opline->opcode == ZEND_NOP) {
		if (block->len == 1) {
			/* this block is all NOPs, join with following block */
			if (block->follow_to) {
				delete_code_block(block);
			}
			return;
		}
		block->start_opline++;
		block->start_opline_no++;
		block->len--;
	}

	/* we track data dependencies only insight a single basic block */
	if (op_array->T) {
		Tsource = ecalloc(op_array->T, sizeof(zend_op *));
	}
	opline = block->start_opline;
	end = opline + block->len;
	while ((op_array->T) && (opline < end)) {
		/* strip X = QM_ASSIGN(const) */
		if (ZEND_OP1_TYPE(opline) == IS_TMP_VAR &&
			VAR_SOURCE(opline->op1) &&
			VAR_SOURCE(opline->op1)->opcode == ZEND_QM_ASSIGN &&
			ZEND_OP1_TYPE(VAR_SOURCE(opline->op1)) == IS_CONST &&
			opline->opcode != ZEND_CASE &&         /* CASE _always_ expects variable */
			opline->opcode != ZEND_FETCH_DIM_TMP_VAR &&   /* in 5.1, FETCH_DIM_TMP_VAR expects T */
			opline->opcode != ZEND_FE_RESET &&
			opline->opcode != ZEND_FREE
			) {
			zend_op *src = VAR_SOURCE(opline->op1);
			VAR_UNSET(opline->op1);
			COPY_NODE(opline->op1, src->op1);
			MAKE_NOP(src);
		}

		/* T = QM_ASSIGN(C), F(T) => NOP, F(C) */
		if (ZEND_OP2_TYPE(opline) == IS_TMP_VAR &&
			VAR_SOURCE(opline->op2) &&
			VAR_SOURCE(opline->op2)->opcode == ZEND_QM_ASSIGN &&
			ZEND_OP1_TYPE(VAR_SOURCE(opline->op2)) == IS_CONST) {
			zend_op *src = VAR_SOURCE(opline->op2);
			VAR_UNSET(opline->op2);
			COPY_NODE(opline->op2, src->op1);
			MAKE_NOP(src);
		}

		/* T = PRINT(X), F(T) => ECHO(X), F(1) */
		if (ZEND_OP1_TYPE(opline) == IS_TMP_VAR &&
			VAR_SOURCE(opline->op1) &&
			VAR_SOURCE(opline->op1)->opcode == ZEND_PRINT &&
			opline->opcode != ZEND_CASE && opline->opcode != ZEND_FREE) {
			ZEND_OP1_TYPE(opline) = IS_CONST;
			LITERAL_LONG(opline->op1, 1);
		}

		if (ZEND_OP2_TYPE(opline) == IS_TMP_VAR &&
			VAR_SOURCE(opline->op2) &&
			VAR_SOURCE(opline->op2)->opcode == ZEND_PRINT) {
			ZEND_OP2_TYPE(opline) = IS_CONST;
			LITERAL_LONG(opline->op2, 1);
		}

		/* T = CAST(X, String), ECHO(T) => NOP, ECHO(X) */
		if ((opline->opcode == ZEND_ECHO || opline->opcode == ZEND_PRINT) &&
			ZEND_OP1_TYPE(opline) == IS_TMP_VAR &&
			VAR_SOURCE(opline->op1) &&
			VAR_SOURCE(opline->op1)->opcode == ZEND_CAST &&
			VAR_SOURCE(opline->op1)->extended_value == IS_STRING) {
			zend_op *src = VAR_SOURCE(opline->op1);
			COPY_NODE(opline->op1, src->op1);
			MAKE_NOP(src);
		}

		/* T = PRINT(X), FREE(T) => ECHO(X) */
		if (opline->opcode == ZEND_FREE &&
			ZEND_OP1_TYPE(opline) == IS_TMP_VAR &&
			VAR_SOURCE(opline->op1)) {
			zend_op *src = VAR_SOURCE(opline->op1);
			if (src->opcode == ZEND_PRINT) {
				src->opcode = ZEND_ECHO;
				ZEND_RESULT_TYPE(src) = IS_UNUSED;
				MAKE_NOP(opline);
			}
		}

       /* T = BOOL(X), FREE(T) => NOP */
		if (opline->opcode == ZEND_FREE &&
			ZEND_OP1_TYPE(opline) == IS_TMP_VAR &&
			VAR_SOURCE(opline->op1)) {
			zend_op *src = VAR_SOURCE(opline->op1);
			if (src->opcode == ZEND_BOOL) {
				if (ZEND_OP1_TYPE(src) == IS_CONST) {
					literal_dtor(&ZEND_OP1_LITERAL(src));
				}
				MAKE_NOP(src);
				MAKE_NOP(opline);
			}
		}

#if 0
		/* pre-evaluate functions:
		   constant(x)
		   defined(x)
		   function_exists(x)
		   extension_loaded(x)
		   BAD: interacts badly with Accelerator
		*/
		if((ZEND_OP1_TYPE(opline) & IS_VAR) &&
		   VAR_SOURCE(opline->op1) && VAR_SOURCE(opline->op1)->opcode == ZEND_DO_CF_FCALL &&
		   VAR_SOURCE(opline->op1)->extended_value == 1) {
			zend_op *fcall = VAR_SOURCE(opline->op1);
			zend_op *sv = fcall-1;
			if(sv >= block->start_opline && sv->opcode == ZEND_SEND_VAL &&
			   ZEND_OP1_TYPE(sv) == IS_CONST && Z_TYPE(OPLINE_OP1_LITERAL(sv)) == IS_STRING &&
			   Z_LVAL(OPLINE_OP2_LITERAL(sv)) == 1
			   ) {
				zval *arg = &OPLINE_OP1_LITERAL(sv);
				char *fname = FUNCTION_CACHE->funcs[Z_LVAL(ZEND_OP1_LITERAL(fcall))].function_name;
				int flen = FUNCTION_CACHE->funcs[Z_LVAL(ZEND_OP1_LITERAL(fcall))].name_len;
				if(flen == sizeof("defined")-1 && zend_binary_strcasecmp(fname, flen, "defined", sizeof("defined")-1) == 0) {
					zval c;
					if(zend_get_persistent_constant(Z_STRVAL_P(arg), Z_STRLEN_P(arg), &c, 0 TSRMLS_CC ELS_CC) != 0) {
						literal_dtor(arg);
						MAKE_NOP(sv);
						MAKE_NOP(fcall);
						LITERAL_BOOL(opline->op1, 1);
						ZEND_OP1_TYPE(opline) = IS_CONST;
					}
				} else if((flen == sizeof("function_exists")-1 && zend_binary_strcasecmp(fname, flen, "function_exists", sizeof("function_exists")-1) == 0) ||
						  (flen == sizeof("is_callable")-1 && zend_binary_strcasecmp(fname, flen, "is_callable", sizeof("is_callable")-1) == 0)
						  ) {
					zend_function *function;
					if(zend_hash_find(EG(function_table), Z_STRVAL_P(arg), Z_STRLEN_P(arg)+1, (void **)&function) == SUCCESS) {
						literal_dtor(arg);
						MAKE_NOP(sv);
						MAKE_NOP(fcall);
						LITERAL_BOOL(opline->op1, 1);
						ZEND_OP1_TYPE(opline) = IS_CONST;
					}
				} else if(flen == sizeof("constant")-1 && zend_binary_strcasecmp(fname, flen, "constant", sizeof("constant")-1) == 0) {
					zval c;
					if(zend_get_persistent_constant(Z_STRVAL_P(arg), Z_STRLEN_P(arg), &c, 1 TSRMLS_CC ELS_CC) != 0) {
						literal_dtor(arg);
						MAKE_NOP(sv);
						MAKE_NOP(fcall);
						ZEND_OP1_LITERAL(opline) = zend_optimizer_add_literal(op_array, &c TSRMLS_CC);
						/* no copy ctor - get already copied it */
						ZEND_OP1_TYPE(opline) = IS_CONST;
					}
				} else if(flen == sizeof("extension_loaded")-1 && zend_binary_strcasecmp(fname, flen, "extension_loaded", sizeof("extension_loaded")-1) == 0) {
					if(zend_hash_exists(&module_registry, Z_STRVAL_P(arg), Z_STRLEN_P(arg)+1)) {
						literal_dtor(arg);
						MAKE_NOP(sv);
						MAKE_NOP(fcall);
						LITERAL_BOOL(opline->op1, 1);
						ZEND_OP1_TYPE(opline) = IS_CONST;
					}
				}
			}
		}
#endif

        /* IS_EQ(TRUE, X)      => BOOL(X)
         * IS_EQ(FALSE, X)     => BOOL_NOT(X)
         * IS_NOT_EQ(TRUE, X)  => BOOL_NOT(X)
         * IS_NOT_EQ(FALSE, X) => BOOL(X)
         */
		if (opline->opcode == ZEND_IS_EQUAL ||
			opline->opcode == ZEND_IS_NOT_EQUAL) {
			if (ZEND_OP1_TYPE(opline) == IS_CONST &&
				Z_TYPE(ZEND_OP1_LITERAL(opline)) == IS_BOOL) {
				opline->opcode =
					((opline->opcode == ZEND_IS_EQUAL) == Z_LVAL(ZEND_OP1_LITERAL(opline)))?
					ZEND_BOOL : ZEND_BOOL_NOT;
				COPY_NODE(opline->op1, opline->op2);
				SET_UNUSED(opline->op2);
			} else if (ZEND_OP2_TYPE(opline) == IS_CONST &&
					   Z_TYPE(ZEND_OP2_LITERAL(opline)) == IS_BOOL) {
				opline->opcode =
					((opline->opcode == ZEND_IS_EQUAL) == Z_LVAL(ZEND_OP2_LITERAL(opline)))?
					ZEND_BOOL : ZEND_BOOL_NOT;
				SET_UNUSED(opline->op2);
			}
		}

		if ((opline->opcode == ZEND_BOOL ||
			opline->opcode == ZEND_BOOL_NOT ||
			opline->opcode == ZEND_JMPZ ||
			opline->opcode == ZEND_JMPNZ ||
			opline->opcode == ZEND_JMPZNZ) &&
			ZEND_OP1_TYPE(opline) == IS_TMP_VAR &&
			VAR_SOURCE(opline->op1) != NULL &&
			!used_ext[VAR_NUM(ZEND_OP1(opline).var)] &&
			VAR_SOURCE(opline->op1)->opcode == ZEND_BOOL_NOT) {
			/* T = BOOL_NOT(X) + JMPZ(T) -> NOP, JMPNZ(X) */
			zend_op *src = VAR_SOURCE(opline->op1);

			COPY_NODE(opline->op1, src->op1);

			switch (opline->opcode) {
				case ZEND_BOOL:
					/* T = BOOL_NOT(X) + BOOL(T) -> NOP, BOOL_NOT(X) */
					opline->opcode = ZEND_BOOL_NOT;
					break;
				case ZEND_BOOL_NOT:
					/* T = BOOL_NOT(X) + BOOL_BOOL(T) -> NOP, BOOL(X) */
					opline->opcode = ZEND_BOOL;
					break;
				case ZEND_JMPZ:
					/* T = BOOL_NOT(X) + JMPZ(T,L) -> NOP, JMPNZ(X,L) */
					opline->opcode = ZEND_JMPNZ;
					break;
				case ZEND_JMPNZ:
					/* T = BOOL_NOT(X) + JMPNZ(T,L) -> NOP, JMPZ(X,L) */
					opline->opcode = ZEND_JMPZ;
					break;
				case ZEND_JMPZNZ:
				{
					/* T = BOOL_NOT(X) + JMPZNZ(T,L1,L2) -> NOP, JMPZNZ(X,L2,L1) */
					int op_t;
					zend_code_block *op_b;

					op_t = opline->extended_value;
					opline->extended_value = ZEND_OP2(opline).opline_num;
					ZEND_OP2(opline).opline_num = op_t;

					op_b = block->ext_to;
					block->ext_to = block->op2_to;
					block->op2_to = op_b;
				}
				break;
			}

			VAR_UNSET(opline->op1);
			MAKE_NOP(src);
			continue;
		} else
#if 0
		/* T = BOOL_NOT(X) + T = JMPZ_EX(T, X) -> T = BOOL_NOT(X), JMPNZ(X) */
		if(0 && (opline->opcode == ZEND_JMPZ_EX ||
			opline->opcode == ZEND_JMPNZ_EX) &&
		   ZEND_OP1_TYPE(opline) == IS_TMP_VAR &&
		   VAR_SOURCE(opline->op1) != NULL &&
		   VAR_SOURCE(opline->op1)->opcode == ZEND_BOOL_NOT &&
		   ZEND_OP1(opline).var == ZEND_RESULT(opline).var
		   ) {
			zend_op *src = VAR_SOURCE(opline->op1);
			if(opline->opcode == ZEND_JMPZ_EX) {
				opline->opcode = ZEND_JMPNZ;
			} else {
				opline->opcode = ZEND_JMPZ;
			}
			COPY_NODE(opline->op1, src->op1);
			SET_UNUSED(opline->result);
			continue;
		} else
#endif
		/* T = BOOL(X) + JMPZ(T) -> NOP, JMPZ(X) */
		if ((opline->opcode == ZEND_BOOL ||
			opline->opcode == ZEND_BOOL_NOT ||
			opline->opcode == ZEND_JMPZ ||
			opline->opcode == ZEND_JMPZ_EX ||
			opline->opcode == ZEND_JMPNZ_EX ||
			opline->opcode == ZEND_JMPNZ ||
			opline->opcode == ZEND_JMPZNZ) &&
			ZEND_OP1_TYPE(opline) == IS_TMP_VAR &&
			VAR_SOURCE(opline->op1) != NULL &&
			(!used_ext[VAR_NUM(ZEND_OP1(opline).var)] ||
			(ZEND_RESULT_TYPE(opline) == IS_TMP_VAR &&
			 ZEND_RESULT(opline).var == ZEND_OP1(opline).var)) &&
			(VAR_SOURCE(opline->op1)->opcode == ZEND_BOOL ||
			VAR_SOURCE(opline->op1)->opcode == ZEND_QM_ASSIGN)) {
			zend_op *src = VAR_SOURCE(opline->op1);
			COPY_NODE(opline->op1, src->op1);

			VAR_UNSET(opline->op1);
			MAKE_NOP(src);
			continue;
		} else if (last_op && opline->opcode == ZEND_ECHO &&
				  last_op->opcode == ZEND_ECHO &&
				  ZEND_OP1_TYPE(opline) == IS_CONST &&
				  Z_TYPE(ZEND_OP1_LITERAL(opline)) != IS_DOUBLE &&
				  ZEND_OP1_TYPE(last_op) == IS_CONST &&
				  Z_TYPE(ZEND_OP1_LITERAL(last_op)) != IS_DOUBLE) {
			/* compress consecutive ECHO's.
			 * Float to string conversion may be affected by current
			 * locale setting.
			 */
			int l;

			if (Z_TYPE(ZEND_OP1_LITERAL(opline)) != IS_STRING) {
				convert_to_string_safe(&ZEND_OP1_LITERAL(opline));
			}
			if (Z_TYPE(ZEND_OP1_LITERAL(last_op)) != IS_STRING) {
				convert_to_string_safe(&ZEND_OP1_LITERAL(last_op));
			}
			l = Z_STRLEN(ZEND_OP1_LITERAL(opline)) + Z_STRLEN(ZEND_OP1_LITERAL(last_op));
			if (IS_INTERNED(Z_STRVAL(ZEND_OP1_LITERAL(last_op)))) {
				char *tmp = emalloc(l + 1);
				memcpy(tmp, Z_STRVAL(ZEND_OP1_LITERAL(last_op)), l + 1);
				Z_STRVAL(ZEND_OP1_LITERAL(last_op)) = tmp;
			} else {
				Z_STRVAL(ZEND_OP1_LITERAL(last_op)) = erealloc(Z_STRVAL(ZEND_OP1_LITERAL(last_op)), l + 1);
			}
			memcpy(Z_STRVAL(ZEND_OP1_LITERAL(last_op))+Z_STRLEN(ZEND_OP1_LITERAL(last_op)), Z_STRVAL(ZEND_OP1_LITERAL(opline)), Z_STRLEN(ZEND_OP1_LITERAL(opline)));
			Z_STRVAL(ZEND_OP1_LITERAL(last_op))[l] = '\0';
			zval_dtor(&ZEND_OP1_LITERAL(opline));
#if ZEND_EXTENSION_API_NO > PHP_5_3_X_API_NO
			Z_STRVAL(ZEND_OP1_LITERAL(opline)) = (char*)zend_new_interned_string(Z_STRVAL(ZEND_OP1_LITERAL(last_op)), l + 1, 1 TSRMLS_CC);
			Z_TYPE(ZEND_OP1_LITERAL(last_op)) = IS_NULL;
#else
			Z_STRVAL(ZEND_OP1_LITERAL(opline)) = Z_STRVAL(ZEND_OP1_LITERAL(last_op));
#endif
			Z_STRLEN(ZEND_OP1_LITERAL(opline)) = l;
			MAKE_NOP(last_op);
		} else if (opline->opcode == ZEND_CONCAT &&
				  ZEND_OP2_TYPE(opline) == IS_CONST &&
				  ZEND_OP1_TYPE(opline) == IS_TMP_VAR &&
				  VAR_SOURCE(opline->op1) &&
				  (VAR_SOURCE(opline->op1)->opcode == ZEND_CONCAT ||
				   VAR_SOURCE(opline->op1)->opcode == ZEND_ADD_STRING) &&
				  ZEND_OP2_TYPE(VAR_SOURCE(opline->op1)) == IS_CONST &&
				  ZEND_RESULT(VAR_SOURCE(opline->op1)).var == ZEND_OP1(opline).var) {
			/* compress consecutive CONCATs */
			zend_op *src = VAR_SOURCE(opline->op1);
			int l;

			if (Z_TYPE(ZEND_OP2_LITERAL(opline)) != IS_STRING) {
				convert_to_string_safe(&ZEND_OP2_LITERAL(opline));
			}
			if (Z_TYPE(ZEND_OP2_LITERAL(src)) != IS_STRING) {
				convert_to_string_safe(&ZEND_OP2_LITERAL(src));
			}

			VAR_UNSET(opline->op1);
			if (ZEND_OP1_TYPE(src) == IS_UNUSED) {
				/* 5.3 may use IS_UNUSED as first argument to ZEND_ADD_... */
				opline->opcode = ZEND_ADD_STRING;
			}
			COPY_NODE(opline->op1, src->op1);
			l = Z_STRLEN(ZEND_OP2_LITERAL(opline)) + Z_STRLEN(ZEND_OP2_LITERAL(src));
			if (IS_INTERNED(Z_STRVAL(ZEND_OP2_LITERAL(src)))) {
				char *tmp = emalloc(l + 1);
				memcpy(tmp, Z_STRVAL(ZEND_OP2_LITERAL(src)), l + 1);
				Z_STRVAL(ZEND_OP2_LITERAL(src)) = tmp;
			} else {
				Z_STRVAL(ZEND_OP2_LITERAL(src)) = erealloc(Z_STRVAL(ZEND_OP2_LITERAL(src)), l + 1);
			}
			memcpy(Z_STRVAL(ZEND_OP2_LITERAL(src))+Z_STRLEN(ZEND_OP2_LITERAL(src)), Z_STRVAL(ZEND_OP2_LITERAL(opline)), Z_STRLEN(ZEND_OP2_LITERAL(opline)));
			Z_STRVAL(ZEND_OP2_LITERAL(src))[l] = '\0';
			if (!IS_INTERNED(Z_STRVAL(ZEND_OP2_LITERAL(opline)))) {
				efree(Z_STRVAL(ZEND_OP2_LITERAL(opline)));
			}
#if ZEND_EXTENSION_API_NO > PHP_5_3_X_API_NO
			Z_STRVAL(ZEND_OP2_LITERAL(opline)) = (char*)zend_new_interned_string(Z_STRVAL(ZEND_OP2_LITERAL(src)), l + 1, 1 TSRMLS_CC);
			Z_TYPE(ZEND_OP2_LITERAL(src)) = IS_NULL;
#else
			Z_STRVAL(ZEND_OP2_LITERAL(opline)) = Z_STRVAL(ZEND_OP2_LITERAL(src));
#endif
			Z_STRLEN(ZEND_OP2_LITERAL(opline)) = l;
			MAKE_NOP(src);
		} else if ((opline->opcode == ZEND_ADD_STRING || opline->opcode == ZEND_ADD_VAR) && ZEND_OP1_TYPE(opline) == IS_CONST) {
			/* convert ADD_STRING(C1, C2) to CONCAT(C1, C2) */
			opline->opcode = ZEND_CONCAT;
			continue;
		} else if (opline->opcode == ZEND_ADD_CHAR && ZEND_OP1_TYPE(opline) == IS_CONST && ZEND_OP2_TYPE(opline) == IS_CONST) {
            /* convert ADD_CHAR(C1, C2) to CONCAT(C1, C2) */
			char c = (char)Z_LVAL(ZEND_OP2_LITERAL(opline));
			ZVAL_STRINGL(&ZEND_OP2_LITERAL(opline), &c, 1, 1);
			opline->opcode = ZEND_CONCAT;
			continue;
		} else if ((opline->opcode == ZEND_ADD ||
					opline->opcode == ZEND_SUB ||
					opline->opcode == ZEND_MUL ||
					opline->opcode == ZEND_DIV ||
					opline->opcode == ZEND_MOD ||
					opline->opcode == ZEND_SL ||
					opline->opcode == ZEND_SR ||
					opline->opcode == ZEND_CONCAT ||
					opline->opcode == ZEND_IS_EQUAL ||
					opline->opcode == ZEND_IS_NOT_EQUAL ||
					opline->opcode == ZEND_IS_SMALLER ||
					opline->opcode == ZEND_IS_SMALLER_OR_EQUAL ||
					opline->opcode == ZEND_IS_IDENTICAL ||
					opline->opcode == ZEND_IS_NOT_IDENTICAL ||
					opline->opcode == ZEND_BOOL_XOR ||
					opline->opcode == ZEND_BW_OR ||
					opline->opcode == ZEND_BW_AND ||
					opline->opcode == ZEND_BW_XOR) &&
					ZEND_OP1_TYPE(opline)==IS_CONST &&
					ZEND_OP2_TYPE(opline)==IS_CONST) {
			/* evaluate constant expressions */
			int (*binary_op)(zval *result, zval *op1, zval *op2 TSRMLS_DC) = get_binary_op(opline->opcode);
			zval result;
			int er;

            if ((opline->opcode == ZEND_DIV || opline->opcode == ZEND_MOD) &&
                ((Z_TYPE(ZEND_OP2_LITERAL(opline)) == IS_LONG &&
                  Z_LVAL(ZEND_OP2_LITERAL(opline)) == 0) ||
                 (Z_TYPE(ZEND_OP2_LITERAL(opline)) == IS_DOUBLE &&
                  Z_DVAL(ZEND_OP2_LITERAL(opline)) == 0.0))) {
				if (RESULT_USED(opline)) {
					SET_VAR_SOURCE(opline);
				}
                opline++;
				continue;
			}
			er = EG(error_reporting);
			EG(error_reporting) = 0;
			if (binary_op(&result, &ZEND_OP1_LITERAL(opline), &ZEND_OP2_LITERAL(opline) TSRMLS_CC) == SUCCESS) {
				PZ_SET_REFCOUNT_P(&result, 1);
				PZ_UNSET_ISREF_P(&result);

				literal_dtor(&ZEND_OP1_LITERAL(opline));
				literal_dtor(&ZEND_OP2_LITERAL(opline));
				ZEND_OP1_LITERAL(opline) = result;
				SET_UNUSED(opline->op2);

				opline->opcode = ZEND_QM_ASSIGN;
			}
			EG(error_reporting) = er;
		} else if ((opline->opcode == ZEND_BOOL ||
				   	opline->opcode == ZEND_BOOL_NOT ||
				  	opline->opcode == ZEND_BW_NOT) && ZEND_OP1_TYPE(opline) == IS_CONST) {
			/* evaluate constant unary ops */
			unary_op_type unary_op = get_unary_op(opline->opcode);
			zval result;

			if (unary_op) {
				unary_op(&result, &ZEND_OP1_LITERAL(opline) TSRMLS_CC);
				literal_dtor(&ZEND_OP1_LITERAL(opline));
			} else {
				/* BOOL */
				result = ZEND_OP1_LITERAL(opline);
				convert_to_boolean(&result);
			}
			PZ_SET_REFCOUNT_P(&result, 1);
			PZ_UNSET_ISREF_P(&result);
			ZEND_OP1_LITERAL(opline) = result;
			opline->opcode = ZEND_QM_ASSIGN;
		} else if ((opline->opcode == ZEND_RETURN || opline->opcode == ZEND_EXIT) &&
					ZEND_OP1_TYPE(opline) == IS_TMP_VAR &&
				   	VAR_SOURCE(opline->op1) &&
				   	VAR_SOURCE(opline->op1)->opcode == ZEND_QM_ASSIGN) {
			/* T = QM_ASSIGN(X), RETURN(T) to RETURN(X) */
			zend_op *src = VAR_SOURCE(opline->op1);
			VAR_UNSET(opline->op1);
			COPY_NODE(opline->op1, src->op1);
			MAKE_NOP(src);
		} else if ((opline->opcode == ZEND_ADD_STRING ||
					opline->opcode == ZEND_ADD_CHAR) &&
				  	ZEND_OP1_TYPE(opline) == IS_TMP_VAR &&
				  	VAR_SOURCE(opline->op1) &&
				  	VAR_SOURCE(opline->op1)->opcode == ZEND_INIT_STRING) {
			/* convert T = INIT_STRING(), T = ADD_STRING(T, X) to T = QM_ASSIGN(X) */
			/* CHECKME: Remove ZEND_ADD_VAR optimization, since some conversions -
			   namely, BOOL(false)->string - don't allocate memory but use empty_string
			   and ADD_CHAR fails */
			zend_op *src = VAR_SOURCE(opline->op1);
			VAR_UNSET(opline->op1);
			COPY_NODE(opline->op1, opline->op2);
			if (opline->opcode == ZEND_ADD_CHAR) {
				char c = (char)Z_LVAL(ZEND_OP2_LITERAL(opline));
				ZVAL_STRINGL(&ZEND_OP1_LITERAL(opline), &c, 1, 1);
			}
			SET_UNUSED(opline->op2);
			MAKE_NOP(src);
			opline->opcode = ZEND_QM_ASSIGN;
		} else if ((opline->opcode == ZEND_ADD_STRING ||
				   	opline->opcode == ZEND_ADD_CHAR ||
				   	opline->opcode == ZEND_ADD_VAR ||
				   	opline->opcode == ZEND_CONCAT) &&
				  	ZEND_OP1_TYPE(opline) == IS_TMP_VAR &&
				  	VAR_SOURCE(opline->op1) &&
				  	VAR_SOURCE(opline->op1)->opcode == ZEND_CONCAT &&
				  	ZEND_OP2_TYPE(VAR_SOURCE(opline->op1)) == IS_CONST &&
				  	Z_TYPE(ZEND_OP2_LITERAL(VAR_SOURCE(opline->op1))) == IS_STRING &&
				  	Z_STRLEN(ZEND_OP2_LITERAL(VAR_SOURCE(opline->op1))) == 0) {
			/* convert T = CONCAT(X,''), T = ADD_STRING(T, Y) to T = CONCAT(X,Y) */
			zend_op *src = VAR_SOURCE(opline->op1);
			VAR_UNSET(opline->op1);
			COPY_NODE(opline->op1, src->op1);
			if (opline->opcode == ZEND_ADD_CHAR) {
				char c = (char)Z_LVAL(ZEND_OP2_LITERAL(opline));
				ZVAL_STRINGL(&ZEND_OP2_LITERAL(opline), &c, 1, 1);
			}
			opline->opcode = ZEND_CONCAT;
			literal_dtor(&ZEND_OP2_LITERAL(src)); /* will take care of empty_string too */
			MAKE_NOP(src);
		} else if (opline->opcode == ZEND_ADD_VAR &&
					ZEND_OP1_TYPE(opline) == IS_TMP_VAR &&
					VAR_SOURCE(opline->op1) &&
					VAR_SOURCE(opline->op1)->opcode == ZEND_INIT_STRING) {
			/* convert T = INIT_STRING(), T = ADD_VAR(T, X) to T = CAST(STRING, X) */
			zend_op *src = VAR_SOURCE(opline->op1);
			VAR_UNSET(opline->op1);
			COPY_NODE(opline->op1, opline->op2);
			SET_UNUSED(opline->op2);
			MAKE_NOP(src);
			opline->opcode = ZEND_CAST;
			opline->extended_value = IS_STRING;
		} else if ((opline->opcode == ZEND_ADD_STRING ||
					opline->opcode == ZEND_ADD_CHAR ||
					opline->opcode == ZEND_ADD_VAR ||
					opline->opcode == ZEND_CONCAT) &&
					ZEND_OP1_TYPE(opline) == IS_TMP_VAR &&
					VAR_SOURCE(opline->op1) &&
					VAR_SOURCE(opline->op1)->opcode == ZEND_CAST &&
					VAR_SOURCE(opline->op1)->extended_value == IS_STRING) {
			/* convert T1 = CAST(STRING, X), T2 = CONCAT(T1, Y) to T2 = CONCAT(X,Y) */
			zend_op *src = VAR_SOURCE(opline->op1);
			VAR_UNSET(opline->op1);
			COPY_NODE(opline->op1, src->op1);
			if (opline->opcode == ZEND_ADD_CHAR) {
				char c = (char)Z_LVAL(ZEND_OP2_LITERAL(opline));
				ZVAL_STRINGL(&ZEND_OP2_LITERAL(opline), &c, 1, 1);
			}
			opline->opcode = ZEND_CONCAT;
			MAKE_NOP(src);
		} else if (opline->opcode == ZEND_QM_ASSIGN &&
					ZEND_OP1_TYPE(opline) == IS_TMP_VAR &&
					ZEND_RESULT_TYPE(opline) == IS_TMP_VAR &&
					ZEND_OP1(opline).var == ZEND_RESULT(opline).var) {
			/* strip T = QM_ASSIGN(T) */
			MAKE_NOP(opline);
		} else if (opline->opcode == ZEND_BOOL &&
					ZEND_OP1_TYPE(opline) == IS_TMP_VAR &&
					VAR_SOURCE(opline->op1) &&
					(VAR_SOURCE(opline->op1)->opcode == ZEND_IS_EQUAL ||
					VAR_SOURCE(opline->op1)->opcode == ZEND_IS_NOT_EQUAL ||
					VAR_SOURCE(opline->op1)->opcode == ZEND_IS_SMALLER ||
					VAR_SOURCE(opline->op1)->opcode == ZEND_IS_SMALLER_OR_EQUAL ||
					VAR_SOURCE(opline->op1)->opcode == ZEND_BOOL ||
					VAR_SOURCE(opline->op1)->opcode == ZEND_IS_IDENTICAL ||
					VAR_SOURCE(opline->op1)->opcode == ZEND_IS_NOT_IDENTICAL ||
					VAR_SOURCE(opline->op1)->opcode == ZEND_ISSET_ISEMPTY_VAR ||
					VAR_SOURCE(opline->op1)->opcode == ZEND_ISSET_ISEMPTY_DIM_OBJ) &&
					!used_ext[VAR_NUM(ZEND_OP1(opline).var)]) {
			/* T = IS_SMALLER(X, Y), T1 = BOOL(T) => T = IS_SMALLER(X, Y), T1 = QM_ASSIGN(T) */
			zend_op *src = VAR_SOURCE(opline->op1);
			COPY_NODE(src->result, opline->result);
			SET_VAR_SOURCE(src);
			MAKE_NOP(opline);
		}
		/* get variable source */
		if (RESULT_USED(opline)) {
			SET_VAR_SOURCE(opline);
		}
		if (opline->opcode != ZEND_NOP) {
			last_op = opline;
		}
		opline++;
	}

	/* remove leading NOPs */
	while (block->len > 0 && block->start_opline->opcode == ZEND_NOP) {
		if (block->len == 1) {
			/* this block is all NOPs, join with following block */
			if (block->follow_to) {
				delete_code_block(block);
			}
			if (op_array->T) {
				efree(Tsource);
			}
			return;
		}
		block->start_opline++;
		block->start_opline_no++;
		block->len--;
	}

	/* strip the inside NOPs */
	opline = block->start_opline;
	end = opline + block->len;
	while (opline < end) {
		if (opline->opcode == ZEND_NOP) {
			zend_op *nop = opline + 1;
			int noplen;
			while (nop < end && nop->opcode == ZEND_NOP) {
				nop++;
			}
			noplen = nop-opline;
			if (nop < end) {
				/* move up non-NOP opcodes */
				memmove(opline, nop, (end-nop)*sizeof(zend_op));
			} else {
				/* all NOPs up to the end, do nothing */
			}
			block->len -= noplen;
			end = block->start_opline + block->len;
		}
		opline++;
	}

	if (op_array->T) {
		efree(Tsource);
	}
}

/* Rebuild plain (optimized) op_array from CFG */
static void assemble_code_blocks(zend_cfg *cfg, zend_op_array *op_array)
{
	zend_code_block *blocks = cfg->blocks;
	zend_op *new_opcodes = emalloc(op_array->last * sizeof(zend_op));
	zend_op *opline = new_opcodes;
	zend_code_block *cur_block = blocks;

	/* Copy code of reachable blocks into a single buffer */
	while (cur_block) {
		if (cur_block->access) {
			memcpy(opline, cur_block->start_opline, cur_block->len * sizeof(zend_op));
			cur_block->start_opline = opline;
			opline += cur_block->len;
			if ((opline - 1)->opcode == ZEND_JMP) {
				zend_code_block *next;
				next = cur_block->next;
				while (next && !next->access) {
					next = next->next;
				}
				if (next && next == cur_block->op1_to) {
					/* JMP to the next block - strip it */
					cur_block->follow_to = cur_block->op1_to;
					cur_block->op1_to = NULL;
					MAKE_NOP((opline - 1));
					opline--;
					cur_block->len--;
				}
			}
		} else {
			/* this block will not be used, delete all constants there */
			zend_op *_opl;
			zend_op *end = cur_block->start_opline + cur_block->len;
			for (_opl = cur_block->start_opline; _opl && _opl < end; _opl++) {
				if (ZEND_OP1_TYPE(_opl) == IS_CONST) {
					literal_dtor(&ZEND_OP1_LITERAL(_opl));
				}
				if (ZEND_OP2_TYPE(_opl) == IS_CONST) {
					literal_dtor(&ZEND_OP2_LITERAL(_opl));
				}
			}
		}
		cur_block = cur_block->next;
	}
#if ZEND_EXTENSION_API_NO < PHP_5_3_X_API_NO
	if (opline[-1].opcode == ZEND_THROW) {
		/* if we finished with THROW, we need to add space between THROW and HANDLE to not confuse
		   zend_throw_internal */
		MAKE_NOP(opline);
		opline->lineno = opline[-1].lineno;
		opline++;
	}
	MAKE_NOP(opline);
	opline->opcode = ZEND_HANDLE_EXCEPTION;
	opline->lineno = opline[-1].lineno;
	opline++;
#endif

	op_array->last = opline-new_opcodes;

	/* adjust exception jump targets */
	if (op_array->last_try_catch) {
		int i;
		for (i = 0; i< op_array->last_try_catch; i++) {
			op_array->try_catch_array[i].try_op = cfg->try[i]->start_opline - new_opcodes;
			op_array->try_catch_array[i].catch_op = cfg->catch[i]->start_opline - new_opcodes;
		}
		efree(cfg->try);
		efree(cfg->catch);
	}

	/* adjust loop jump targets */
	if (op_array->last_brk_cont) {
		int i;
		for (i = 0; i< op_array->last_brk_cont; i++) {
			op_array->brk_cont_array[i].start = cfg->loop_start[i]->start_opline - new_opcodes;
			op_array->brk_cont_array[i].cont = cfg->loop_cont[i]->start_opline - new_opcodes;
			op_array->brk_cont_array[i].brk = cfg->loop_brk[i]->start_opline - new_opcodes;
		}
		efree(cfg->loop_start);
		efree(cfg->loop_cont);
		efree(cfg->loop_brk);
	}

    /* adjust jump targets */
	for (cur_block = blocks; cur_block; cur_block = cur_block->next) {
		if (!cur_block->access) {
			continue;
		}
		if (cur_block->op1_to) {
			ZEND_OP1(&cur_block->start_opline[cur_block->len - 1]).opline_num = cur_block->op1_to->start_opline - new_opcodes;
		}
		if (cur_block->op2_to) {
			ZEND_OP2(&cur_block->start_opline[cur_block->len - 1]).opline_num = cur_block->op2_to->start_opline - new_opcodes;
		}
		if (cur_block->ext_to) {
			cur_block->start_opline[cur_block->len - 1].extended_value = cur_block->ext_to->start_opline - new_opcodes;
		}
		print_block(cur_block, new_opcodes, "Out ");
	}
	efree(op_array->opcodes);
	op_array->opcodes = erealloc(new_opcodes, op_array->last * sizeof(zend_op));

#if ZEND_EXTENSION_API_NO >= PHP_5_3_X_API_NO
	/* adjust early binding list */
	if (op_array->early_binding != (zend_uint)-1) {
		zend_uint *opline_num = &op_array->early_binding;
		zend_op *end;

		opline = op_array->opcodes;
		end = opline + op_array->last;
		while (opline < end) {
			if (opline->opcode == ZEND_DECLARE_INHERITED_CLASS_DELAYED) {
				*opline_num = opline - op_array->opcodes;
				opline_num = &ZEND_RESULT(opline).opline_num;
			}
			++opline;
		}
		*opline_num = -1;
	}
#endif
}

static void zend_jmp_optimization(zend_code_block *block, zend_op_array *op_array, zend_code_block *blocks)
{
	/* last_op is the last opcode of the current block */
	zend_op *last_op = (block->start_opline + block->len - 1);

	if (!block->len) {
		return;
	}
	switch (last_op->opcode) {
		case ZEND_JMP:
			{
				zend_op *target = block->op1_to->start_opline;
				zend_code_block *next = block->next;

				while (next && !next->access) {
					/* find used one */
					next = next->next;
				}

				/* JMP(next) -> NOP */
				if (block->op1_to == next) {
					block->follow_to = block->op1_to;
					block->op1_to = NULL;
					MAKE_NOP(last_op);
					block->len--;
					if (block->len == 0) {
						/* this block is nothing but NOP now */
						delete_code_block(block);
					}
					break;
				}

				if (((target->opcode == ZEND_JMP &&
					block->op1_to != block->op1_to->op1_to) ||
					target->opcode == ZEND_JMPZNZ) &&
					!block->op1_to->protected) {
					/* JMP L, L: JMP L1 -> JMP L1 */
					/* JMP L, L: JMPZNZ L1,L2 -> JMPZNZ L1,L2 */
					*last_op = *target;
					if (ZEND_OP1_TYPE(last_op) == IS_CONST) {
						zval_copy_ctor(&ZEND_OP1_LITERAL(last_op));
					}
					del_source(block, block->op1_to);
					if (block->op1_to->op2_to) {
						block->op2_to = block->op1_to->op2_to;
						ADD_SOURCE(block, block->op2_to);
					}
					if (block->op1_to->ext_to) {
						block->ext_to = block->op1_to->ext_to;
						ADD_SOURCE(block, block->ext_to);
					}
					if (block->op1_to->op1_to) {
						block->op1_to = block->op1_to->op1_to;
						ADD_SOURCE(block, block->op1_to);
					} else {
						block->op1_to = NULL;
					}
				} else if (target->opcode == ZEND_RETURN ||
#if ZEND_EXTENSION_API_NO > PHP_5_3_X_API_NO
				          target->opcode == ZEND_RETURN_BY_REF ||
#endif
#if ZEND_EXTENSION_API_NO > PHP_5_4_X_API_NO
            	          target->opcode == ZEND_FAST_RET ||
#endif
			    	      target->opcode == ZEND_EXIT) {
					/* JMP L, L: RETURN to immediate RETURN */
					*last_op = *target;
					if (ZEND_OP1_TYPE(last_op) == IS_CONST) {
						zval_copy_ctor(&ZEND_OP1_LITERAL(last_op));
					}
					del_source(block, block->op1_to);
					block->op1_to = NULL;
#if 0
				/* Temporarily disabled - see bug #0025274 */
				} else if (0&& block->op1_to != block &&
			           block->op1_to != blocks &&
						   op_array->last_try_catch == 0 &&
				           target->opcode != ZEND_FREE &&
				           target->opcode != ZEND_SWITCH_FREE) {
				    /* Block Reordering (saves one JMP on each "for" loop iteration)
				     * It is disabled for some cases (ZEND_FREE/ZEND_SWITCH_FREE)
				     * which may break register allocation.
            	     */
					zend_bool can_reorder = 0;
					zend_block_source *cs = block->op1_to->sources;

					/* the "target" block doesn't had any followed block */
					while(cs) {
						if (cs->from->follow_to == block->op1_to) {
							can_reorder = 0;
							break;
						}
						cs = cs->next;
					}
					if (can_reorder) {
						next = block->op1_to;
						/* the "target" block is not followed by current "block" */
						while (next->follow_to != NULL) {
							if (next->follow_to == block) {
								can_reorder = 0;
								break;
							}
							next = next->follow_to;
						}
						if (can_reorder) {
							zend_code_block *prev = blocks;

							while (prev->next != block->op1_to) {
								prev = prev->next;
							}
							prev->next = next->next;
							next->next = block->next;
							block->next = block->op1_to;

							block->follow_to = block->op1_to;
							block->op1_to = NULL;
							MAKE_NOP(last_op);
							block->len--;
							if(block->len == 0) {
								/* this block is nothing but NOP now */
								delete_code_block(block);
							}
							break;
						}
					}
#endif
				}
			}
			break;

		case ZEND_JMPZ:
		case ZEND_JMPNZ:
			/* constant conditional JMPs */
			if (ZEND_OP1_TYPE(last_op) == IS_CONST) {
				int should_jmp = zend_is_true(&ZEND_OP1_LITERAL(last_op));
				if (last_op->opcode == ZEND_JMPZ) {
					should_jmp = !should_jmp;
				}
				literal_dtor(&ZEND_OP1_LITERAL(last_op));
				ZEND_OP1_TYPE(last_op) = IS_UNUSED;
				if (should_jmp) {
					/* JMPNZ(true) -> JMP */
					last_op->opcode = ZEND_JMP;
					COPY_NODE(last_op->op1, last_op->op2);
					block->op1_to = block->op2_to;
					del_source(block, block->follow_to);
					block->op2_to = NULL;
					block->follow_to = NULL;
				} else {
					/* JMPNZ(false) -> NOP */
					MAKE_NOP(last_op);
					del_source(block, block->op2_to);
					block->op2_to = NULL;
				}
				break;
			}

			if (block->op2_to) {
				zend_uchar same_type = ZEND_OP1_TYPE(last_op);
				zend_uint same_var = VAR_NUM_EX(last_op->op1);
				zend_op *target;
				zend_op *target_end;
				zend_code_block *target_block = block->op2_to;;

next_target:
				target = target_block->start_opline;
				target_end = target_block->start_opline + target_block->len;
				while (target < target_end && target->opcode == ZEND_NOP) {
					target++;
				}

				/* next block is only NOP's */
				if (target == target_end) {
					target_block = target_block->follow_to;
					goto next_target;
				} else if (target->opcode == INV_COND(last_op->opcode) &&
					/* JMPZ(X, L), L: JMPNZ(X, L2) -> JMPZ(X, L+1) */
				   (ZEND_OP1_TYPE(target) & (IS_TMP_VAR|IS_CV)) &&
				   same_type == ZEND_OP1_TYPE(target) &&
				   same_var == VAR_NUM_EX(target->op1) &&
				   target_block->follow_to &&
				   !target_block->protected
				   ) {
					del_source(block, block->op2_to);
					block->op2_to = target_block->follow_to;
					ADD_SOURCE(block, block->op2_to);
				} else if (target->opcode == INV_COND_EX(last_op->opcode) &&
							(ZEND_OP1_TYPE(target) & (IS_TMP_VAR|IS_CV)) &&
				    		same_type == ZEND_OP1_TYPE(target) &&
				    		same_var == VAR_NUM_EX(target->op1) &&
							target_block->follow_to &&
							!target_block->protected) {
					/* JMPZ(X, L), L: X = JMPNZ_EX(X, L2) -> JMPZ(X, L+1) */
					last_op->opcode += 3;
					last_op->result = target->result;
					del_source(block, block->op2_to);
					block->op2_to = target_block->follow_to;
					ADD_SOURCE(block, block->op2_to);
				} else if (target_block->op2_to &&
						   target->opcode == last_op->opcode &&
						   (ZEND_OP1_TYPE(target) & (IS_TMP_VAR|IS_CV)) &&
						   same_type == ZEND_OP1_TYPE(target) &&
						   same_var == VAR_NUM_EX(target->op1) &&
						   !target_block->protected) {
					/* JMPZ(X, L), L: JMPZ(X, L2) -> JMPZ(X, L2) */
					del_source(block, block->op2_to);
					block->op2_to = target_block->op2_to;
					ADD_SOURCE(block, block->op2_to);
				} else if (target_block->op1_to &&
							target->opcode == ZEND_JMP &&
							!target_block->protected) {
					/* JMPZ(X, L), L: JMP(L2) -> JMPZ(X, L2) */
					del_source(block, block->op2_to);
					block->op2_to = target_block->op1_to;
					ADD_SOURCE(block, block->op2_to);
				} else if (target_block->op2_to &&
							target_block->ext_to &&
							target->opcode == ZEND_JMPZNZ &&
							(ZEND_OP1_TYPE(target) & (IS_TMP_VAR|IS_CV)) &&
				        	same_type == ZEND_OP1_TYPE(target) &&
				        	same_var == VAR_NUM_EX(target->op1) &&
							!target_block->protected) {
					/* JMPZ(X, L), L: JMPZNZ(X, L2, L3) -> JMPZ(X, L2) */
					del_source(block, block->op2_to);
					if (last_op->opcode == ZEND_JMPZ) {
						block->op2_to = target_block->op2_to;
					} else {
						block->op2_to = target_block->ext_to;
					}
					ADD_SOURCE(block, block->op2_to);
				}
			}

			if (block->follow_to &&
			    (last_op->opcode == ZEND_JMPZ || last_op->opcode == ZEND_JMPNZ)) {
				zend_op *target;
				zend_op *target_end;

				while (1) {
					target = block->follow_to->start_opline;
					target_end = block->follow_to->start_opline + block->follow_to->len;
					while (target < target_end && target->opcode == ZEND_NOP) {
						target++;
					}

					/* next block is only NOP's */
					if (target == target_end) {
						del_source(block, block->follow_to);
						block->follow_to = block->follow_to->follow_to;
						ADD_SOURCE(block, block->follow_to);
					} else {
						break;
					}
				}
				/* JMPZ(X,L1), JMP(L2) -> JMPZNZ(X,L1,L2) */
				if (target->opcode == ZEND_JMP &&
					block->follow_to->op1_to &&
					!block->follow_to->protected) {
					del_source(block, block->follow_to);
					if (last_op->opcode == ZEND_JMPZ) {
						block->ext_to = block->follow_to->op1_to;
						ADD_SOURCE(block, block->ext_to);
					} else {
						block->ext_to = block->op2_to;
						block->op2_to = block->follow_to->op1_to;
						ADD_SOURCE(block, block->op2_to);
					}
					block->follow_to = NULL;
					last_op->opcode = ZEND_JMPZNZ;
				}
			}
			break;

		case ZEND_JMPNZ_EX:
		case ZEND_JMPZ_EX:
			/* constant conditional JMPs */
			if (ZEND_OP1_TYPE(last_op) == IS_CONST) {
				int should_jmp = zend_is_true(&ZEND_OP1_LITERAL(last_op));
				if (last_op->opcode == ZEND_JMPZ_EX) {
					should_jmp = !should_jmp;
				}
				if (!should_jmp) {
					/* T = JMPZ_EX(true,L)   -> T = QM_ASSIGN(true)
					 * T = JMPNZ_EX(false,L) -> T = QM_ASSIGN(false)
					 */
					last_op->opcode = ZEND_QM_ASSIGN;
					SET_UNUSED(last_op->op2);
					del_source(block, block->op2_to);
					block->op2_to = NULL;
				}
				break;
			}

			if (block->op2_to) {
				zend_op *target, *target_end;
				char *same_t=NULL;
				zend_code_block *target_block;
				int var_num = 0;
				if (op_array->T >= (zend_uint)op_array->last_var) {
					var_num = op_array->T;
				} else {
					var_num = op_array->last_var;
				}
				if (var_num <= 0) {
   					return;
				}
				same_t = ecalloc(var_num, sizeof(char));
				if (same_t == NULL) {
					return;
				}
				same_t[VAR_NUM_EX(last_op->op1)] |= ZEND_OP1_TYPE(last_op);
				same_t[VAR_NUM_EX(last_op->result)] |= ZEND_RESULT_TYPE(last_op);
				target_block = block->op2_to;
next_target_ex:
				target = target_block->start_opline;
				target_end = target_block->start_opline + target_block->len;
				while (target < target_end && target->opcode == ZEND_NOP) {
					target++;
				}
 				/* next block is only NOP's */
				if (target == target_end) {
					target_block = target_block->follow_to;
					goto next_target_ex;
				} else if (target_block->op2_to &&
						   target->opcode == last_op->opcode-3 &&
						   (ZEND_OP1_TYPE(target) & (IS_TMP_VAR|IS_CV)) &&
						   (same_t[VAR_NUM_EX(target->op1)] & ZEND_OP1_TYPE(target)) != 0 &&
						   !target_block->protected) {
					/* T = JMPZ_EX(X, L1), L1: JMPZ({X|T}, L2) -> T = JMPZ_EX(X, L2) */
					del_source(block, block->op2_to);
					block->op2_to = target_block->op2_to;
					ADD_SOURCE(block, block->op2_to);
				} else if (target_block->op2_to &&
						   target->opcode == INV_EX_COND(last_op->opcode) &&
					   	   (ZEND_OP1_TYPE(target) & (IS_TMP_VAR|IS_CV)) &&
						   (same_t[VAR_NUM_EX(target->op1)] & ZEND_OP1_TYPE(target)) != 0 &&
						   !target_block->protected) {
					/* T = JMPZ_EX(X, L1), L1: JMPNZ({X|T1}, L2) -> T = JMPZ_EX(X, L1+1) */
					del_source(block, block->op2_to);
					block->op2_to = target_block->follow_to;
					ADD_SOURCE(block, block->op2_to);
				} else if (target_block->op2_to &&
						   target->opcode == INV_EX_COND_EX(last_op->opcode) &&
					       (ZEND_OP1_TYPE(target) & (IS_TMP_VAR|IS_CV)) &&
						   (same_t[VAR_NUM_EX(target->op1)] & ZEND_OP1_TYPE(target)) != 0 &&
						   (same_t[VAR_NUM_EX(target->result)] & ZEND_RESULT_TYPE(target)) != 0 &&
						   !target_block->protected) {
					/* T = JMPZ_EX(X, L1), L1: T = JMPNZ_EX(T, L2) -> T = JMPZ_EX(X, L1+1) */
					del_source(block, block->op2_to);
					block->op2_to = target_block->follow_to;
					ADD_SOURCE(block, block->op2_to);
				} else if (target_block->op2_to &&
						   target->opcode == last_op->opcode &&
						   (ZEND_OP1_TYPE(target) & (IS_TMP_VAR|IS_CV)) &&
						   (same_t[VAR_NUM_EX(target->op1)] & ZEND_OP1_TYPE(target)) != 0 &&
						   (same_t[VAR_NUM_EX(target->result)] & ZEND_RESULT_TYPE(target)) != 0 &&
						   !target_block->protected) {
					/* T = JMPZ_EX(X, L1), L1: T = JMPZ({X|T}, L2) -> T = JMPZ_EX(X, L2) */
					del_source(block, block->op2_to);
					block->op2_to = target_block->op2_to;
					ADD_SOURCE(block, block->op2_to);
				} else if (target_block->op1_to &&
						   target->opcode == ZEND_JMP &&
						   !target_block->protected) {
					/* T = JMPZ_EX(X, L), L: JMP(L2) -> T = JMPZ(X, L2) */
					del_source(block, block->op2_to);
					block->op2_to = target_block->op1_to;
					ADD_SOURCE(block, block->op2_to);
				} else if (target_block->op2_to &&
						   target_block->ext_to &&
						   target->opcode == ZEND_JMPZNZ &&
						   (ZEND_OP1_TYPE(target) & (IS_TMP_VAR|IS_CV)) &&
						   (same_t[VAR_NUM_EX(target->op1)] & ZEND_OP1_TYPE(target)) != 0 &&
						   !target_block->protected) {
					/* T = JMPZ_EX(X, L), L: JMPZNZ({X|T}, L2, L3) -> T = JMPZ_EX(X, L2) */
					del_source(block, block->op2_to);
					if (last_op->opcode == ZEND_JMPZ_EX) {
						block->op2_to = target_block->op2_to;
					} else {
						block->op2_to = target_block->ext_to;
					}
					ADD_SOURCE(block, block->op2_to);
				}
				if (same_t != NULL) {
					efree(same_t);
				}
			}
			break;

		case ZEND_JMPZNZ: {
			zend_code_block *next = block->next;

			while (next && !next->access) {
				/* find first accessed one */
				next = next->next;
			}

			if (ZEND_OP1_TYPE(last_op) == IS_CONST) {
				if (!zend_is_true(&ZEND_OP1_LITERAL(last_op))) {
					/* JMPZNZ(false,L1,L2) -> JMP(L1) */
					zend_code_block *todel;

					literal_dtor(&ZEND_OP1_LITERAL(last_op));
					last_op->opcode = ZEND_JMP;
					SET_UNUSED(last_op->op1);
					SET_UNUSED(last_op->op2);
					block->op1_to = block->op2_to;
					todel = block->ext_to;
					block->op2_to = NULL;
					block->ext_to = NULL;
					del_source(block, todel);
				} else {
					/* JMPZNZ(true,L1,L2) -> JMP(L2) */
					zend_code_block *todel;

					literal_dtor(&ZEND_OP1_LITERAL(last_op));
					last_op->opcode = ZEND_JMP;
					SET_UNUSED(last_op->op1);
					SET_UNUSED(last_op->op2);
					block->op1_to = block->ext_to;
					todel =  block->op2_to;
					block->op2_to = NULL;
					block->ext_to = NULL;
					del_source(block, todel);
				}
			} else if (block->op2_to == block->ext_to) {
				/* both goto the same one - it's JMP */
				/* JMPZNZ(?,L,L) -> JMP(L) */
				last_op->opcode = ZEND_JMP;
				SET_UNUSED(last_op->op1);
				SET_UNUSED(last_op->op2);
				block->op1_to = block->op2_to;
				block->op2_to = NULL;
				block->ext_to = NULL;
			} else if (block->op2_to == next) {
				/* jumping to next on Z - can follow to it and jump only on NZ */
				/* JMPZNZ(X,L1,L2) L1: -> JMPNZ(X,L2) */
				last_op->opcode = ZEND_JMPNZ;
				block->op2_to = block->ext_to;
				block->follow_to = next;
				block->ext_to = NULL;
				/* no need to add source - it's block->op2_to */
			} else if (block->ext_to == next) {
				/* jumping to next on NZ - can follow to it and jump only on Z */
				/* JMPZNZ(X,L1,L2) L2: -> JMPZ(X,L1) */
				last_op->opcode = ZEND_JMPZ;
				block->follow_to = next;
				block->ext_to = NULL;
				/* no need to add source - it's block->ext_to */
			}

			if (last_op->opcode == ZEND_JMPZNZ && block->op2_to) {
				zend_uchar same_type = ZEND_OP1_TYPE(last_op);
				zend_uchar same_var = VAR_NUM_EX(last_op->op1);
				zend_op *target;
				zend_op *target_end;
				zend_code_block *target_block = block->op2_to;

next_target_znz:
				target = target_block->start_opline;
				target_end = target_block->start_opline + target_block->len;
				while (target < target_end && target->opcode == ZEND_NOP) {
					target++;
				}
				/* next block is only NOP's */
				if (target == target_end) {
					target_block = target_block->follow_to;
					goto next_target_znz;
				} else if (target_block->op2_to &&
						   (target->opcode == ZEND_JMPZ || target->opcode == ZEND_JMPZNZ) &&
						   (ZEND_OP1_TYPE(target) & (IS_TMP_VAR|IS_CV)) &&
						   same_type == ZEND_OP1_TYPE(target) &&
						   same_var == VAR_NUM_EX(target->op1) &&
						   !target_block->protected) {
				    /* JMPZNZ(X, L1, L2), L1: JMPZ(X, L3) -> JMPZNZ(X, L3, L2) */
					del_source(block, block->op2_to);
					block->op2_to = target_block->op2_to;
					ADD_SOURCE(block, block->op2_to);
				} else if (target->opcode == ZEND_JMPNZ &&
						   (ZEND_OP1_TYPE(target) & (IS_TMP_VAR|IS_CV)) &&
						   same_type == ZEND_OP1_TYPE(target) &&
						   same_var == VAR_NUM_EX(target->op1) &&
						   target_block->follow_to &&
						   !target_block->protected) {
                    /* JMPZNZ(X, L1, L2), L1: X = JMPNZ(X, L3) -> JMPZNZ(X, L1+1, L2) */
					del_source(block, block->op2_to);
					block->op2_to = target_block->follow_to;
					ADD_SOURCE(block, block->op2_to);
				} else if (target_block->op1_to &&
					       target->opcode == ZEND_JMP &&
					       !target_block->protected) {
                    /* JMPZNZ(X, L1, L2), L1: JMP(L3) -> JMPZNZ(X, L3, L2) */
					del_source(block, block->op2_to);
					block->op2_to = target_block->op1_to;
					ADD_SOURCE(block, block->op2_to);
				}
			}
			break;
		}
	}
}

/* Global data dependencies */

#if ZEND_EXTENSION_API_NO > PHP_5_3_X_API_NO

# define T_USAGE(op) do { \
		if ((op ## _type & (IS_VAR | IS_TMP_VAR)) && \
		   !defined_here[VAR_NUM(op.var)] && !used_ext[VAR_NUM(op.var)]) {	\
			used_ext[VAR_NUM(op.var)] = 1;									\
		} \
	} while (0)

# define NEVER_USED(op) ((op ## _type & (IS_VAR | IS_TMP_VAR)) && !usage[VAR_NUM(op.var)]) /* !used_ext[op.var] && */
# define RES_NEVER_USED(opline) (opline->result_type == IS_UNUSED || NEVER_USED(opline->result))

#else

# define T_USAGE(op) do { \
		if ((op.op_type == IS_VAR || op.op_type == IS_TMP_VAR) && \
		   !defined_here[VAR_NUM(op.u.var)] && !used_ext[VAR_NUM(op.u.var)]) {	\
			used_ext[VAR_NUM(op.u.var)] = 1;									\
		} \
	} while (0)

# define NEVER_USED(op) ((op.op_type == IS_VAR || op.op_type == IS_TMP_VAR) && !usage[VAR_NUM(op.u.var)]) /* !used_ext[op.u.var] && */
# define RES_NEVER_USED(opline) (ZEND_RESULT_TYPE(opline) == IS_UNUSED || NEVER_USED(opline->result))

#endif

/* Find a set of variables which are used outside of the block where they are
 * defined. We won't apply some optimization patterns for sush variables. */
static void zend_t_usage(zend_code_block *block, zend_op_array *op_array, char *used_ext)
{
	zend_code_block *next_block = block->next;
	char *usage;
	char *defined_here;

	if (op_array->T == 0) {
		/* shortcut - if no Ts, nothing to do */
		return;
	}

	usage = ecalloc(op_array->T, 1);
	defined_here = emalloc(op_array->T);

	while (next_block) {
		zend_op *opline = next_block->start_opline;
		zend_op *end = opline + next_block->len;

		if (!next_block->access) {
			next_block = next_block->next;
			continue;
		}
		memset(defined_here, 0, op_array->T);

		while (opline<end) {
			T_USAGE(opline->op1);
			T_USAGE(opline->op2);

			if (RESULT_USED(opline)) {
				if (!defined_here[VAR_NUM(ZEND_RESULT(opline).var)] && !used_ext[VAR_NUM(ZEND_RESULT(opline).var)] &&
				    (opline->opcode == ZEND_RECV || opline->opcode == ZEND_RECV_INIT ||
					(opline->opcode == ZEND_OP_DATA && ZEND_RESULT_TYPE(opline) == IS_TMP_VAR) ||
					opline->opcode == ZEND_ADD_ARRAY_ELEMENT)) {
					/* these opcodes use the result as argument */
					used_ext[VAR_NUM(ZEND_RESULT(opline).var)] = 1;
				}
				defined_here[VAR_NUM(ZEND_RESULT(opline).var)] = 1;
			}
			opline++;
		}
		next_block = next_block->next;
	}

#if DEBUG_BLOCKPASS
	{
		int i;
		for (i = 0; i< op_array->T; i++) {
			fprintf(stderr, "T%d: %c\n", i, used_ext[i] + '0');
		}
	}
#endif

	while (block) {
		zend_op *opline = block->start_opline + block->len - 1;

		if (!block->access) {
			block = block->next;
			continue;
		}

		memcpy(usage, used_ext, op_array->T);

		while (opline >= block->start_opline) {
			/* usage checks */
			if (RES_NEVER_USED(opline)) {
				switch (opline->opcode) {
					case ZEND_ASSIGN_ADD:
					case ZEND_ASSIGN_SUB:
					case ZEND_ASSIGN_MUL:
					case ZEND_ASSIGN_DIV:
					case ZEND_ASSIGN_MOD:
					case ZEND_ASSIGN_SL:
					case ZEND_ASSIGN_SR:
					case ZEND_ASSIGN_CONCAT:
					case ZEND_ASSIGN_BW_OR:
					case ZEND_ASSIGN_BW_AND:
					case ZEND_ASSIGN_BW_XOR:
					case ZEND_PRE_INC:
					case ZEND_PRE_DEC:
					case ZEND_POST_INC:
					case ZEND_POST_DEC:
					case ZEND_ASSIGN:
					case ZEND_ASSIGN_REF:
					case ZEND_DO_FCALL:
					case ZEND_DO_FCALL_BY_NAME:
						if (ZEND_RESULT_TYPE(opline) == IS_VAR) {
#if ZEND_EXTENSION_API_NO > PHP_5_3_X_API_NO
							ZEND_RESULT_TYPE(opline) |= EXT_TYPE_UNUSED;
#else
							ZEND_RESULT(opline).EA.type |= EXT_TYPE_UNUSED;
#endif
						}
						break;
					case ZEND_QM_ASSIGN:
					case ZEND_BOOL:
					case ZEND_BOOL_NOT:
						if (ZEND_OP1_TYPE(opline) == IS_CONST) {
							literal_dtor(&ZEND_OP1_LITERAL(opline));
						}
						MAKE_NOP(opline);
						break;
					case ZEND_PRINT:
						opline->opcode = ZEND_ECHO;
						ZEND_RESULT_TYPE(opline) = IS_UNUSED;
						break;
					case ZEND_JMPZ_EX:
					case ZEND_JMPNZ_EX:
						opline->opcode -= 3;
						SET_UNUSED(opline->result);
						break;
				}
			}

			if (opline->opcode == ZEND_RECV ||
                opline->opcode == ZEND_RECV_INIT ||
                opline->opcode == ZEND_ADD_ARRAY_ELEMENT) {
				if (ZEND_OP1_TYPE(opline) == IS_VAR || ZEND_OP1_TYPE(opline) == IS_TMP_VAR) {
					usage[VAR_NUM(ZEND_RESULT(opline).var)] = 1;
				}
			} else {
				if (RESULT_USED(opline)) {
					usage[VAR_NUM(ZEND_RESULT(opline).var)] = 0;
				}
			}

			if (ZEND_OP1_TYPE(opline) == IS_VAR || ZEND_OP1_TYPE(opline) == IS_TMP_VAR) {
				usage[VAR_NUM(ZEND_OP1(opline).var)] = 1;
			}
			if (ZEND_OP2_TYPE(opline) == IS_VAR || ZEND_OP2_TYPE(opline) == IS_TMP_VAR) {
				usage[VAR_NUM(ZEND_OP2(opline).var)] = 1;
			}


#if ZEND_EXTENSION_API_NO > PHP_5_3_X_API_NO
			if ((ZEND_RESULT_TYPE(opline) & IS_VAR) &&
                (ZEND_RESULT_TYPE(opline) & EXT_TYPE_UNUSED) &&
                usage[VAR_NUM(ZEND_RESULT(opline).var)]) {
				ZEND_RESULT_TYPE(opline) &= ~EXT_TYPE_UNUSED;
 			}
#else
			if (ZEND_RESULT_TYPE(opline) == IS_VAR &&
			    usage[VAR_NUM(ZEND_RESULT(opline).var)] &&
			    (ZEND_RESULT(opline).EA.type & EXT_TYPE_UNUSED) != 0) {
				ZEND_RESULT(opline).EA.type &= ~EXT_TYPE_UNUSED;
			}
#endif

			opline--;
		}
		block = block->next;
	} /* end blocks */

	efree(defined_here);
	efree(usage);
}

#define PASSES 3

static void zend_block_optimization(zend_op_array *op_array TSRMLS_DC)
{
	zend_cfg cfg;
	zend_code_block *cur_block;
	int pass;
	char *usage;

#if DEBUG_BLOCKPASS
	fprintf(stderr, "File %s func %s\n", op_array->filename, op_array->function_name? op_array->function_name : "main");
	fflush(stderr);
#endif

#if ZEND_EXTENSION_API_NO > PHP_5_4_X_API_NO
	if (op_array->has_finally_block) {
		return;
	}
#endif

    /* Build CFG */
	if (!find_code_blocks(op_array, &cfg)) {
		return;
	}

	zend_rebuild_access_path(&cfg, op_array, 0);
	/* full rebuild here to produce correct sources! */
	usage = emalloc(op_array->T);
	for (pass = 0; pass < PASSES; pass++) {
		/* Compute data dependencies */
		memset(usage, 0, op_array->T);
		zend_t_usage(cfg.blocks, op_array, usage);

		/* optimize each basic block separately */
		for (cur_block = cfg.blocks; cur_block; cur_block = cur_block->next) {
			if (!cur_block->access) {
				continue;
			}
			zend_optimize_block(cur_block, op_array, usage TSRMLS_CC);
		}

		/* Jump optimization for each block */
		for (cur_block = cfg.blocks; cur_block; cur_block = cur_block->next) {
			if (!cur_block->access) {
				continue;
			}
			zend_jmp_optimization(cur_block, op_array, cfg.blocks);
		}

		/* Eliminate unreachable basic blocks */
		zend_rebuild_access_path(&cfg, op_array, 1);
	}

	memset(usage, 0, op_array->T);
	zend_t_usage(cfg.blocks, op_array, usage);
	assemble_code_blocks(&cfg, op_array);
	efree(usage);

	/* Destroy CFG */
	for (cur_block = cfg.blocks; cur_block; cur_block = cur_block->next) {
		zend_block_source *cs = cur_block->sources;
		while (cs) {
			zend_block_source *n = cs->next;
			efree(cs);
			cs = n;
		}
	}
	efree(cfg.blocks);
}
