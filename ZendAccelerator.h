/*
   +----------------------------------------------------------------------+
   | Zend Optimizer+                                                      |
   +----------------------------------------------------------------------+
   | Copyright (c) 1998-2013 The PHP Group                                |
   +----------------------------------------------------------------------+
   | This source file is subject to version 3.01 of the PHP license,      |
   | that is bundled with this package in the file LICENSE, and is        |
   | available through the world-wide-web at the following url:           |
   | http://www.php.net/license/3_01.txt                                  |
   | If you did not receive a copy of the PHP license and are unable to   |
   | obtain it through the world-wide-web, please send a note to          |
   | license@php.net so we can mail you a copy immediately.               |
   +----------------------------------------------------------------------+
   | Authors: Andi Gutmans <andi@zend.com>                                |
   |          Zeev Suraski <zeev@zend.com>                                |
   |          Stanislav Malyshev <stas@zend.com>                          |
   |          Dmitry Stogov <dmitry@zend.com>                             |
   +----------------------------------------------------------------------+
*/

#ifndef ZEND_ACCELERATOR_H
#define ZEND_ACCELERATOR_H

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#define ACCELERATOR_PRODUCT_NAME	"Zend Optimizer+"
#define ACCELERATOR_VERSION "7.0.0-dev"
/* 2 - added Profiler support, on 20010712 */
/* 3 - added support for Optimizer's encoded-only-files mode */
/* 4 - works with the new Optimizer, that supports the file format with licenses */
/* 5 - API 4 didn't really work with the license-enabled file format.  v5 does. */
/* 6 - Monitor was removed from ZendPlatform.so, to a module of its own */
/* 7 - Optimizer was embeded into Accelerator */
/* 8 - Standalone Open Source OptimizerPlus */
#define ACCELERATOR_API_NO 8



#if ZEND_WIN32
# include "zend_config.w32.h"
#else
#include "zend_config.h"
# include <sys/time.h>
# include <sys/resource.h>
#endif

#if HAVE_UNISTD_H
# include "unistd.h"
#endif

#include "zend_extensions.h"
#include "zend_compile.h"

#include "Optimizer/zend_optimizer.h"
#include "zend_accelerator_hash.h"
#include "zend_accelerator_debug.h"

extern zend_module_entry ZendOptimizerPlus_module_entry;
#define phpext_ZendOptimizerPlus_ptr &ZendOptimizerPlus_module_entry

#ifndef PHPAPI
# ifdef ZEND_WIN32
#  define PHPAPI __declspec(dllimport)
# else
#  define PHPAPI
# endif
#endif

#ifndef ZEND_EXT_API
# if WIN32|WINNT
#  define ZEND_EXT_API __declspec(dllexport)
# else
#  define ZEND_EXT_API
# endif
#endif

#ifdef ZEND_WIN32
# ifndef MAXPATHLEN
#  define MAXPATHLEN     _MAX_PATH
# endif
# include <direct.h>
#else
# include <sys/param.h>
#endif

#define PHP_5_0_X_API_NO		220040412
#define PHP_5_1_X_API_NO		220051025
#define PHP_5_2_X_API_NO		220060519
#define PHP_5_3_X_API_NO		220090626
#define PHP_5_4_X_API_NO		220100525

/*** file locking ***/
#ifndef ZEND_WIN32
extern int lock_file;

# if defined(__FreeBSD__) || (defined(__APPLE__) && defined(__MACH__)/* Darwin */) || defined(__OpenBSD__) || defined(__NetBSD__)
#  define FLOCK_STRUCTURE(name, type, whence, start, len) \
		struct flock name = {start, len, -1, type, whence}
# elif defined(__svr4__)
#  define FLOCK_STRUCTURE(name, type, whence, start, len) \
		struct flock name = {type, whence, start, len}
# elif defined(__linux__) || defined(__hpux)
#  define FLOCK_STRUCTURE(name, type, whence, start, len) \
		struct flock name = {type, whence, start, len, 0}
# elif defined(_AIX)
#  if defined(_LARGE_FILES) || defined(__64BIT__)
#   define FLOCK_STRUCTURE(name, type, whence, start, len) \
		struct flock name = {type, whence, 0, 0, 0, start, len }
#  else
#   define FLOCK_STRUCTURE(name, type, whence, start, len) \
		struct flock name = {type, whence, start, len}
#  endif
# else
#  error "Don't know how to define struct flock"
# endif
#endif

#if ZEND_EXTENSION_API_NO < PHP_5_3_X_API_NO
	#define Z_REFCOUNT_P(pz)				(pz)->refcount
	#define Z_SET_REFCOUNT_P(pz, v)			(pz)->refcount = (v)
	#define Z_ADDREF_P(pz)					++((pz)->refcount)
	#define Z_DELREF_P(pz)					--((pz)->refcount)
	#define Z_ISREF_P(pz)					(pz)->is_ref
	#define Z_SET_ISREF_P(pz)				Z_SET_ISREF_TO_P(pz, 1)
	#define Z_UNSET_ISREF_P(pz)				Z_SET_ISREF_TO_P(pz, 0)
	#define Z_SET_ISREF_TO_P(pz, isref)		(pz)->is_ref = (isref)
	#define PZ_REFCOUNT_P(pz)				(pz)->refcount
	#define PZ_SET_REFCOUNT_P(pz, v)		(pz)->refcount = (v)
	#define PZ_ADDREF_P(pz)					++((pz)->refcount)
	#define PZ_DELREF_P(pz)					--((pz)->refcount)
	#define PZ_ISREF_P(pz)					(pz)->is_ref
	#define PZ_SET_ISREF_P(pz)				Z_SET_ISREF_TO_P(pz, 1)
	#define PZ_UNSET_ISREF_P(pz)			Z_SET_ISREF_TO_P(pz, 0)
	#define PZ_SET_ISREF_TO_P(pz, isref)	(pz)->is_ref = (isref)
#else
	#define PZ_REFCOUNT_P(pz)				(pz)->refcount__gc
	#define PZ_SET_REFCOUNT_P(pz, v)		(pz)->refcount__gc = (v)
	#define PZ_ADDREF_P(pz)					++((pz)->refcount__gc)
	#define PZ_DELREF_P(pz)					--((pz)->refcount__gc)
	#define PZ_ISREF_P(pz)					(pz)->is_ref__gc
	#define PZ_SET_ISREF_P(pz)				Z_SET_ISREF_TO_P(pz, 1)
	#define PZ_UNSET_ISREF_P(pz)			Z_SET_ISREF_TO_P(pz, 0)
	#define PZ_SET_ISREF_TO_P(pz, isref)	(pz)->is_ref__gc = (isref)
#endif

#if ZEND_EXTENSION_API_NO < PHP_5_3_X_API_NO
# ifdef ALLOCA_FLAG
	#define DO_ALLOCA(x)	do_alloca_with_limit(x, use_heap)
	#define FREE_ALLOCA(x)	free_alloca_with_limit(x, use_heap)
# else
	#define ALLOCA_FLAG(x)
	#define DO_ALLOCA(x)	do_alloca(x)
	#define FREE_ALLOCA(x)	free_alloca(x)
# endif
#else
	#define DO_ALLOCA(x)	do_alloca(x, use_heap)
	#define FREE_ALLOCA(x)	free_alloca(x, use_heap)
#endif


#if ZEND_WIN32
typedef unsigned __int64 accel_time_t;
#else
typedef time_t accel_time_t;
#endif

typedef struct _zend_persistent_script {
	ulong          hash_value;
	char          *full_path;              /* full real path with resolved symlinks */
	unsigned int   full_path_len;
	zend_op_array  main_op_array;
	HashTable      function_table;
	HashTable      class_table;
	long           compiler_halt_offset;   /* position of __HALT_COMPILER or -1 */
	int            ping_auto_globals_mask; /* which autoglobals are used by the script */
	accel_time_t   timestamp;              /* the script modification time */
	zend_bool      corrupted;
#if ZEND_EXTENSION_API_NO < PHP_5_3_X_API_NO
	zend_uint      early_binding;          /* the linked list of delayed declarations */
#endif

	void          *mem;                    /* shared memory area used by script structures */
	size_t         size;                   /* size of used shared memory */

	/* All entries that shouldn't be counted in the ADLER32
	 * checksum must be declared in this struct
	 */
	struct zend_persistent_script_dynamic_members {
		time_t       last_used;
		ulong        hits;
		unsigned int memory_consumption;
		unsigned int checksum;
		time_t       revalidate;
	} dynamic_members;
} zend_persistent_script;

typedef struct _zend_accel_directives {
	long           memory_consumption;
	long           max_accelerated_files;
	double         max_wasted_percentage;
	char          *user_blacklist_filename;
	long           consistency_checks;
	long           force_restart_timeout;
	zend_bool      use_cwd;
	zend_bool      ignore_dups;
	zend_bool      validate_timestamps;
	zend_bool      revalidate_path;
	zend_bool      save_comments;
	zend_bool      load_comments;
	zend_bool      fast_shutdown;
	zend_bool      protect_memory;
	zend_bool      file_override_enabled;
	zend_bool      inherited_hack;
	zend_bool      enable_cli;
	unsigned long  revalidate_freq;
	char          *error_log;
#ifdef ZEND_WIN32
	char          *mmap_base;
#endif
	char          *memory_model;
	long           log_verbosity_level;

	long           optimization_level;
#if ZEND_EXTENSION_API_NO > PHP_5_3_X_API_NO
	long           interned_strings_buffer;
#endif
} zend_accel_directives;

typedef struct _zend_accel_globals {
    /* copy of CG(function_table) used for compilation scripts into cashe */
    /* imitially it contains only internal functions */
	HashTable               function_table;
	int                     internal_functions_count;
	int                     counted;   /* the process uses shatred memory */
	zend_bool               enabled;
	zend_bool               locked;    /* thread obtained exclusive lock */
	HashTable               bind_hash; /* prototype and zval lookup table */
	zend_accel_directives   accel_directives;
	char                   *cwd;              /* current working directory or NULL */
	int                     cwd_len;          /* "cwd" string lenght */
	char                   *include_path_key; /* one letter key of current "include_path" */
	char                   *include_path;     /* current settion of "include_path" directive */
	int                     include_path_len; /* "include_path" string lenght */
	int                     include_path_check;
	time_t                  request_time;
	long					max_cached_filesize;
	/* preallocated shared-memory block to save current script */
	void                   *mem;
	/* cache to save hash lookup on the same INCLUDE opcode */
	zend_op                *cache_opline;
	zend_persistent_script *cache_persistent_script;
	/* preallocated buffer for keys */
	int                     key_len;
	char                    key[MAXPATHLEN * 8];
} zend_accel_globals;

typedef struct _zend_accel_shared_globals {
	/* Cache Data Structures */
	unsigned long   hits;
	unsigned long   misses;
	unsigned long   blacklist_misses;
	zend_accel_hash hash;             /* hash table for cached scripts */
	zend_accel_hash include_paths;    /* used "include_path" values    */

	/* Directives & Maintenance */
	time_t          last_restart_time;
	time_t          force_restart_time;
	zend_bool       accelerator_enabled;
	zend_bool       restart_pending;
	zend_bool       cache_status_before_restart;
#ifdef ZEND_WIN32
    unsigned long   mem_usage;
    unsigned long   restart_in;
#endif
	zend_bool       restart_in_progress;
    time_t          revalidate_at;
#if ZEND_EXTENSION_API_NO > PHP_5_3_X_API_NO
	/* Interned Strings Support */
	char           *interned_strings_start;
	char           *interned_strings_top;
	char           *interned_strings_end;
	HashTable       interned_strings;
	struct {
		Bucket **arBuckets;
		Bucket  *pListHead;
		Bucket  *pListTail;
		char    *top;
	} interned_strings_saved_state;
#endif
} zend_accel_shared_globals;

extern zend_bool accel_startup_ok;

extern zend_accel_shared_globals *accel_shared_globals;
#define ZCSG(element)   (accel_shared_globals->element)

#ifdef ZTS
# define ZCG(v)	TSRMG(accel_globals_id, zend_accel_globals *, v)
extern int accel_globals_id;
#else
# define ZCG(v) (accel_globals.v)
extern zend_accel_globals accel_globals;
#endif

extern char *zps_api_failure_reason;

void zend_accel_schedule_restart(TSRMLS_D);
int  accelerator_shm_read_lock(TSRMLS_D);
void accelerator_shm_read_unlock(TSRMLS_D);

char *accel_make_persistent_key_ex(zend_file_handle *file_handle, int path_length, int *key_len TSRMLS_DC);

#if !defined(ZEND_DECLARE_INHERITED_CLASS_DELAYED)
# define ZEND_DECLARE_INHERITED_CLASS_DELAYED 145
#endif

#define ZEND_DECLARE_INHERITED_CLASS_DELAYED_FLAG 0x80

#if ZEND_EXTENSION_API_NO > PHP_5_3_X_API_NO

const char *accel_new_interned_string(const char *arKey, int nKeyLength, int free_src TSRMLS_DC);

# define interned_free(s) do { \
		if (!IS_INTERNED(s)) { \
			free(s); \
		} \
	} while (0)
# define interned_efree(s) do { \
		if (!IS_INTERNED(s)) { \
			efree(s); \
		} \
	} while (0)
# define interned_estrndup(s, n) \
	(IS_INTERNED(s) ? (s) : estrndup(s, n))
# define ZEND_RESULT_TYPE(opline)	(opline)->result_type
# define ZEND_RESULT(opline)		(opline)->result
# define ZEND_OP1_TYPE(opline)		(opline)->op1_type
# define ZEND_OP1(opline)			(opline)->op1
# define ZEND_OP1_CONST(opline)		(*(opline)->op1.zv)
# define ZEND_OP1_LITERAL(opline)	(op_array)->literals[(opline)->op1.constant].constant
# define ZEND_OP2_TYPE(opline)		(opline)->op2_type
# define ZEND_OP2(opline)			(opline)->op2
# define ZEND_OP2_CONST(opline)		(*(opline)->op2.zv)
# define ZEND_OP2_LITERAL(opline)	(op_array)->literals[(opline)->op2.constant].constant
# define ZEND_DONE_PASS_TWO(op_array)	(((op_array)->fn_flags & ZEND_ACC_DONE_PASS_TWO) != 0)
# define ZEND_CE_FILENAME(ce)			(ce)->info.user.filename
# define ZEND_CE_DOC_COMMENT(ce)        (ce)->info.user.doc_comment
# define ZEND_CE_DOC_COMMENT_LEN(ce)	(ce)->info.user.doc_comment_len
#else
# define IS_INTERNED(s)				0
# define interned_free(s)			free(s)
# define interned_efree(s)			efree(s)
# define interned_estrndup(s, n)	estrndup(s, n)
# define ZEND_RESULT_TYPE(opline)	(opline)->result.op_type
# define ZEND_RESULT(opline)		(opline)->result.u
# define ZEND_OP1_TYPE(opline)		(opline)->op1.op_type
# define ZEND_OP1(opline)			(opline)->op1.u
# define ZEND_OP1_CONST(opline)		(opline)->op1.u.constant
# define ZEND_OP1_LITERAL(opline)	(opline)->op1.u.constant
# define ZEND_OP2_TYPE(opline)		(opline)->op2.op_type
# define ZEND_OP2(opline)			(opline)->op2.u
# define ZEND_OP2_CONST(opline)		(opline)->op2.u.constant
# define ZEND_OP2_LITERAL(opline)	(opline)->op2.u.constant
# define ZEND_DONE_PASS_TWO(op_array)	((op_array)->done_pass_two != 0)
# define ZEND_CE_FILENAME(ce)			(ce)->filename
# define ZEND_CE_DOC_COMMENT(ce)        (ce)->doc_comment
# define ZEND_CE_DOC_COMMENT_LEN(ce)	(ce)->doc_comment_len
#endif

#endif /* ZEND_ACCELERATOR_H */
