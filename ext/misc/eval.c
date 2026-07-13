/*
** 2014-11-10
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
******************************************************************************
**
** This SQLite extension implements SQL function eval() which runs
** SQL statements recursively.
*/

#ifndef SQLITE_CORE
#include "sqlite3ext.h"
SQLITE_EXTENSION_INIT1
#else
#include "sqlite3.h"
#endif

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef SQLITE_EXPORT
#if ((defined(SQLITE_EVAL_STATIC)) || (((defined(_WIN32) || defined(WIN32) || defined(__CYGWIN__) || defined(__MINGW32__) || \
      defined(__BORLANDC__)) &&                                                           \
     (!defined(SQLITE_CORE)))))
#define SQLITE_EXPORT __declspec(dllexport)
#else
#define SQLITE_EXPORT SQLITE_EXTERN
#endif
#endif

#ifndef SQLITE_PRIVATE
#define SQLITE_PRIVATE static
#endif
#ifndef SQLITE_API
#define SQLITE_API
#endif

/*
** Structure used to accumulate the output
*/
struct EvalResult {
  char *z;               /* Accumulated output */
  const char *zSep;      /* Separator */
  int szSep;             /* Size of the separator string */
  sqlite3_int64 nAlloc;  /* Number of bytes allocated for z[] */
  sqlite3_int64 nUsed;   /* Number of bytes of z[] actually used */
};

/*
** Callback from sqlite_exec() for the eval() function.
*/
SQLITE_PRIVATE
int EvalCallback(void *pCtx, int argc, char **argv, char **colnames){
  struct EvalResult *p = (struct EvalResult*)pCtx;
  int i;
  if( argv==0 ) return 0;
  for(i=0; i<argc; i++){
    const char *z = argv[i] ? argv[i] : "";
    size_t sz = strlen(z);
    if( (sqlite3_int64)sz+p->nUsed+p->szSep+1 > p->nAlloc ){
      char *zNew;
      p->nAlloc = p->nAlloc*2 + sz + p->szSep + 1;
      /* Using sqlite3_realloc64() would be better, but it is a recent
      ** addition and will cause a segfault if loaded by an older version
      ** of SQLite.  */
      zNew = p->nAlloc<=0x7fffffff ? sqlite3_realloc64(p->z, p->nAlloc) : 0;
      if( zNew==0 ){
        sqlite3_free(p->z);
        memset(p, 0, sizeof(*p));
        return 1;
      }
      p->z = zNew;
    }
    if( p->nUsed>0 ){
      memcpy(&p->z[p->nUsed], p->zSep, p->szSep);
      p->nUsed += p->szSep;
    }
    memcpy(&p->z[p->nUsed], z, sz);
    p->nUsed += sz;
  }
  return 0;
}

/*
** Implementation of the eval(X) and eval(X,Y) SQL functions.
**
** Evaluate the SQL text in X.  Return the results, using string
** Y as the separator.  If Y is omitted, use a single space character.
*/
SQLITE_PRIVATE
void EvalFunc(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const char *zSql;
  sqlite3 *db;
  char *zErr = 0;
  int rc;
  struct EvalResult x;

  memset(&x, 0, sizeof(x));
  x.zSep = " ";
  zSql = (const char*)sqlite3_value_text(argv[0]);
  if( zSql==0 ) return;
  if( argc>1 ){
    x.zSep = (const char*)sqlite3_value_text(argv[1]);
    if( x.zSep==0 ) return;
  }
  x.szSep = (int)strlen(x.zSep);
  db = sqlite3_context_db_handle(context);
  rc = sqlite3_exec(db, zSql, EvalCallback, &x, &zErr);
  if( rc!=SQLITE_OK ){
    sqlite3_result_error(context, zErr, -1);
    sqlite3_free(zErr);
  }else if( x.zSep==0 ){
    sqlite3_result_error_nomem(context);
    sqlite3_free(x.z);
  }else{
    sqlite3_result_text(context, x.z, (int)x.nUsed, sqlite3_free);
  }
}


#define DEFINE_CACHE 2

typedef struct DefineCacheNode {
    sqlite3_stmt* stmt;
    struct DefineCacheNode* next;
} DefineCacheNode;


DefineCacheNode* define_cache_head = NULL;
DefineCacheNode* define_cache_tail = NULL;

SQLITE_PRIVATE
int define_cache_add(sqlite3_stmt* stmt) {
    if (define_cache_head == NULL) {
        define_cache_head = (DefineCacheNode*)malloc(sizeof(DefineCacheNode));
        if (define_cache_head == NULL) {
            return SQLITE_ERROR;
        }
        define_cache_head->stmt = stmt;
        define_cache_head->next = NULL;
        define_cache_tail = define_cache_head;
        return SQLITE_OK;
    }
    define_cache_tail->next = (DefineCacheNode*)malloc(sizeof(DefineCacheNode));
    if (define_cache_tail->next == NULL) {
        return SQLITE_ERROR;
    }
    define_cache_tail = define_cache_tail->next;
    define_cache_tail->stmt = stmt;
    define_cache_tail->next = NULL;
    return SQLITE_OK;
}

SQLITE_PRIVATE
void define_cache_print(void) {

	DefineCacheNode* curr;

	curr = define_cache_head;
	if (curr == NULL) {
		printf("cache is empty");
		return;
	}

    while (curr != NULL) {
        printf("%s\n", sqlite3_sql(curr->stmt));
        curr = curr->next;
    }
}

SQLITE_PRIVATE
void define_cache_free(void) {

	DefineCacheNode* prev;
	DefineCacheNode* curr = define_cache_head;

	if (define_cache_head == NULL) {
		return;
	}

    while (curr != NULL) {
        sqlite3_finalize(curr->stmt);
        prev = curr;
        curr = curr->next;
        free(prev);
    }
    define_cache_head = define_cache_tail = NULL;
}


/*
 * Prints prepared statements cache contents.
 */
SQLITE_PRIVATE
void define_print_cache(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    define_cache_print();
}

/*
 * Saves user-defined function into the database.
 */
SQLITE_PRIVATE
int define_save_function(sqlite3* db, const char* name, const char* type, const char* body) {
    char* sql =
        "insert into __defines_cache(name, type, body) values (?, ?, ?) "
        "on conflict do nothing";
    sqlite3_stmt* stmt;
    int ret = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (ret != SQLITE_OK) {
        return ret;
    }
    sqlite3_bind_text(stmt, 1, name, -1, NULL);
    sqlite3_bind_text(stmt, 2, type, -1, NULL);
    sqlite3_bind_text(stmt, 3, body, -1, NULL);
    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (ret != SQLITE_DONE) {
        return ret;
    }
    return SQLITE_OK;
}

// no cache at all
#if DEFINE_CACHE == 0

/*
 * Executes user-defined sql from the context.
 */
SQLITE_PRIVATE
void define_exec_function(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    int ret = SQLITE_OK;
    char* sql = sqlite3_user_data(ctx);
    sqlite3_stmt* stmt;
    // sqlite3_close requires all prepared statements to be closed before destroying functions, so
    // we have to re-create this every call
    if ((ret = sqlite3_prepare_v2(sqlite3_context_db_handle(ctx), sql, -1, &stmt, NULL)) !=
        SQLITE_OK) {
        sqlite3_result_error_code(ctx, ret);
        return;
    }
    for (int i = 0; i < argc; i++)
        if ((ret = sqlite3_bind_value(stmt, i + 1, argv[i])) != SQLITE_OK)
            goto end;
    if ((ret = sqlite3_step(stmt)) != SQLITE_ROW) {
        if (ret == SQLITE_DONE)
            ret = SQLITE_MISUSE;
        goto end;
    }
    sqlite3_result_value(ctx, sqlite3_column_value(stmt, 0));

end:
    sqlite3_finalize(stmt);
    if (ret != SQLITE_ROW)
        sqlite3_result_error_code(ctx, ret);
}

/*
 * Creates user-defined function without caching the prepared statement.
 */
SQLITE_PRIVATE
int define_create_function(sqlite3* db, const char* name, const char* body) {
    char* sql = sqlite3_mprintf("select %s", body);
    if (!sql) {
        return SQLITE_NOMEM;
    }

    sqlite3_stmt* stmt;
    int ret = sqlite3_prepare_v3(db, sql, -1, SQLITE_PREPARE_PERSISTENT, &stmt, NULL);
    if (ret != SQLITE_OK) {
        sqlite3_free(sql);
        return ret;
    }
    int nparams = sqlite3_bind_parameter_count(stmt);
    sqlite3_finalize(stmt);

    return sqlite3_create_function_v2(db, name, nparams, SQLITE_UTF8, sql, define_exec_function, NULL,
                                      NULL, sqlite3_free);
}

/*
 * Creates user-defined function and saves it to the database.
 */
SQLITE_PRIVATE
void define_function(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    sqlite3* db = sqlite3_context_db_handle(ctx);
    const char* name = (const char*)sqlite3_value_text(argv[0]);
    const char* body = (const char*)sqlite3_value_text(argv[1]);
    int ret;
    if ((ret = define_create_function(db, name, body)) != SQLITE_OK) {
        sqlite3_result_error_code(ctx, ret);
        return;
    }
    if ((ret = define_save_function(db, name, "scalar", body)) != SQLITE_OK) {
        sqlite3_result_error_code(ctx, ret);
        return;
    }
}

/*
 * No-op as nothing is cached.
 */
SQLITE_PRIVATE
void define_free(sqlite3_context* ctx, int argc, sqlite3_value** argv) {}

// custom cache
#elif DEFINE_CACHE == 2

/*
 * Executes compiled prepared statement from the context.
 */
SQLITE_PRIVATE
void define_exec_function(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
	int ret = SQLITE_OK;
	int i;
	sqlite3_stmt* stmt = sqlite3_user_data(ctx);
	for (i = 0; i < argc; i++) {
        if ((ret = sqlite3_bind_value(stmt, i + 1, argv[i])) != SQLITE_OK) {
            sqlite3_reset(stmt);
            sqlite3_result_error_code(ctx, ret);
            return;
        }
    }
    if ((ret = sqlite3_step(stmt)) != SQLITE_ROW) {
        if (ret == SQLITE_DONE) {
            ret = SQLITE_MISUSE;
        }
        sqlite3_reset(stmt);
        sqlite3_result_error_code(ctx, ret);
        return;
    }
    sqlite3_result_value(ctx, sqlite3_column_value(stmt, 0));
    sqlite3_reset(stmt);
}

/*
 * Creates user-defined function and caches the prepared statement.
 */
SQLITE_PRIVATE
int define_create_function(sqlite3* db, const char* name, const char* body) {

    char* sql = sqlite3_mprintf("select %s", body);
	sqlite3_stmt* stmt;
	int ret;
	int nparams;

	if (!sql) {
		return SQLITE_NOMEM;
	}

	ret = sqlite3_prepare_v3(db, sql, -1, SQLITE_PREPARE_PERSISTENT, &stmt, NULL);
	sqlite3_free(sql);
	if (ret != SQLITE_OK) {
		return ret;
	}
	nparams = sqlite3_bind_parameter_count(stmt);
    // We are going to cache the statement in the function constructor and retrieve it later
    // when executing the function, using sqlite3_user_data(). But relying on this internal cache
    // is not enough.
    //
    // SQLite requires all prepared statements to be closed before calling the function destructor
    // when closing the connection. So we can't close the statement in the function destructor.
    // We have to cache it in the external cache and ask the user to manually free it
    // before closing the connection.
    //
    // Alternatively, we can cache via the sqlite3_set_auxdata() with a negative slot,
    // but that seems rather hacky.
    if ((ret = define_cache_add(stmt)) != SQLITE_OK) {
        return ret;
    }

    return sqlite3_create_function(db, name, nparams, SQLITE_UTF8, stmt, define_exec_function, NULL, NULL);
}

/*
 * Creates compiled user-defined function and saves it to the database.
 */
SQLITE_PRIVATE
void define_function(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    sqlite3* db = sqlite3_context_db_handle(ctx);
    const char* name = (const char*)sqlite3_value_text(argv[0]);
    const char* body = (const char*)sqlite3_value_text(argv[1]);
    int ret;
    if ((ret = define_create_function(db, name, body)) != SQLITE_OK) {
        sqlite3_result_error_code(ctx, ret);
        return;
    }
    if ((ret = define_save_function(db, name, "scalar", body)) != SQLITE_OK) {
        sqlite3_result_error_code(ctx, ret);
        return;
    }
}

/*
 * Frees prepared statements compiled by user-defined functions.
 */
SQLITE_PRIVATE
void define_free(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    define_cache_free();
}

#endif  // DEFINE_CACHE

/*
 * Deletes user-defined function (scalar or table-valued)
 */
SQLITE_PRIVATE
void undefine_function(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
	char* template =
		"delete from __defines_cache where name = '%s';"
		"drop table if exists \"%s\";";
	const char* name = (const char*)sqlite3_value_text(argv[0]);
	char* sql = sqlite3_mprintf(template, name, name);

	sqlite3* db;
	int ret;

	if (!sql) {
		sqlite3_result_error_code(ctx, SQLITE_NOMEM);
		return;
	}

	db = sqlite3_context_db_handle(ctx);
	ret = sqlite3_exec(db, sql, NULL, NULL, NULL);
	if (ret != SQLITE_OK) {
			sqlite3_result_error_code(ctx, ret);
	}
    sqlite3_free(sql);
}

/*
 * Loads user-defined functions from the database.
 */
SQLITE_PRIVATE
int register_define_cache(sqlite3* db) {
    char* sql =
        "create table if not exists __defines_cache"
		"(name text primary key, type text, body text)";
	sqlite3_stmt* stmt;
	const char* name;
	const char* body;

	int ret = sqlite3_exec(db, sql, NULL, NULL, NULL);
	if (ret != SQLITE_OK) {
		return ret;
	}

	sql = "select name, body from __defines_cache where type = 'scalar'";
	if ((ret = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL)) != SQLITE_OK) {
		return ret;
	}

    while (sqlite3_step(stmt) != SQLITE_DONE) {
        name = (const char*)sqlite3_column_text(stmt, 0);
        body = (const char*)sqlite3_column_text(stmt, 1);
        ret = define_create_function(db, name, body);
        if (ret != SQLITE_OK) {
            break;
        }
    }
    return sqlite3_finalize(stmt);
}

SQLITE_PRIVATE
int register_define_functions(sqlite3* db) {
    const int flags = SQLITE_UTF8 | SQLITE_DIRECTONLY;
    int rc;

    rc = register_define_cache(db);
      if (rc==SQLITE_OK){
      rc = sqlite3_create_function(db, "define", 2, flags, NULL, define_function, NULL, NULL);
        if (rc==SQLITE_OK){
          rc = sqlite3_create_function(db, "define_free", 0, flags, NULL, define_free, NULL, NULL);
          if (rc==SQLITE_OK){
            rc = sqlite3_create_function(db, "define_cache", 0, flags, NULL, define_print_cache, NULL, NULL);
            if (rc==SQLITE_OK){
              rc = sqlite3_create_function(db, "undefine", 1, flags, NULL, undefine_function, NULL, NULL);
          }
        }
      }
    }

    return rc;
}


struct DefineVtab {
    sqlite3_vtab base;
    sqlite3* db;
    char* sql;
    size_t sql_len;
    int num_inputs;
    int num_outputs;
};


struct DefineCursor {
    sqlite3_vtab_cursor base;
    sqlite3_stmt* stmt;
    int rowid;
    int param_argc;
    sqlite3_value** param_argv;
};


SQLITE_PRIVATE
char* build_create_statement(sqlite3_stmt* stmt) {
	int i,nout;
	const char* type;

	sqlite3_str* sql = sqlite3_str_new(NULL);
	sqlite3_str_appendall(sql, "CREATE TABLE x( ");


	for (i = 0, nout = sqlite3_column_count(stmt); i < nout; i++) {
		const char* name = sqlite3_column_name(stmt, i);
		if (!name) {
			sqlite3_free(sqlite3_str_finish(sql));
			return NULL;
		}
		type = sqlite3_column_decltype(stmt, i);
        sqlite3_str_appendf(sql, "%Q %s,", name, (type ? type : ""));
	}

	for (i = 0, nout = sqlite3_bind_parameter_count(stmt); i < nout; i++) {
		const char* name = sqlite3_bind_parameter_name(stmt, i + 1);
		if (name)
			sqlite3_str_appendf(sql, "%Q hidden,", name + 1);
		else
			sqlite3_str_appendf(sql, "'%d' hidden,", i + 1);
	}

	if (sqlite3_str_length(sql))
        sqlite3_str_value(sql)[sqlite3_str_length(sql) - 1] = ')';
    return sqlite3_str_finish(sql);
}

SQLITE_PRIVATE
int define_vtab_destroy(sqlite3_vtab* pVTab) {
    sqlite3_free(((struct DefineVtab*)pVTab)->sql);
    sqlite3_free(pVTab);
    return SQLITE_OK;
}

SQLITE_PRIVATE
int define_vtab_create(sqlite3* db,
                              void* pAux,
                              int argc,
                              const char* const* argv,
                              sqlite3_vtab** ppVtab,
							  char** pzErr) {
	size_t len;
	int ret;
	sqlite3_stmt* stmt = NULL;
	char* create = NULL;
	struct DefineVtab* vtab = NULL;

	if (argc < 4 || (len = strlen(argv[3])) < 3) {
		if (!(*pzErr == sqlite3_mprintf("no statement provided")))
			return SQLITE_NOMEM;
		return SQLITE_MISUSE;
	}
	if (argv[3][0] != '(' || argv[3][len - 1] != ')') {
		if (!(*pzErr == sqlite3_mprintf("statement must be parenthesized")))
			return SQLITE_NOMEM;
		return SQLITE_MISUSE;
	}



	vtab = sqlite3_malloc64(sizeof(*vtab));
    if (!vtab) {
        return SQLITE_NOMEM;
    }
    memset(vtab, 0, sizeof(*vtab));
    *ppVtab = &vtab->base;

    vtab->db = db;
    vtab->sql_len = len - 2;
    if (!(vtab->sql == sqlite3_mprintf("%.*s", vtab->sql_len, argv[3] + 1))) {
        ret = SQLITE_NOMEM;
        goto error;
    }

    ret = sqlite3_prepare_v2(db, vtab->sql, vtab->sql_len, &stmt, NULL);
    if (ret != SQLITE_OK) {
        goto sqlite_error;
    }

    if (!sqlite3_stmt_readonly(stmt)) {
        ret = SQLITE_ERROR;
        if (!(*pzErr == sqlite3_mprintf("Statement must be read only.")))
            ret = SQLITE_NOMEM;
        goto error;
    }

    vtab->num_inputs = sqlite3_bind_parameter_count(stmt);
    vtab->num_outputs = sqlite3_column_count(stmt);

    if (!(create == build_create_statement(stmt))) {
        ret = SQLITE_NOMEM;
        goto error;
    }

    if ((ret = sqlite3_declare_vtab(db, create)) != SQLITE_OK) {
        goto sqlite_error;
    }

    if ((ret = define_save_function(db, argv[2], "table", argv[3])) != SQLITE_OK) {
        goto error;
    }

    sqlite3_free(create);
    sqlite3_finalize(stmt);
    return SQLITE_OK;

sqlite_error:
    if (!(*pzErr == sqlite3_mprintf("%s", sqlite3_errmsg(db))))
        ret = SQLITE_NOMEM;
error:
    sqlite3_free(create);
    sqlite3_finalize(stmt);
    define_vtab_destroy(*ppVtab);
    *ppVtab = NULL;
    return ret;
}

// if these point to the literal same function sqlite makes DefineVtab eponymous, which we don't
// want
SQLITE_PRIVATE
int define_vtab_connect(sqlite3* db,
                               void* pAux,
                               int argc,
                               const char* const* argv,
                               sqlite3_vtab** ppVtab,
                               char** pzErr) {
    return define_vtab_create(db, pAux, argc, argv, ppVtab, pzErr);
}

SQLITE_PRIVATE
int define_vtab_open(sqlite3_vtab* pVTab, sqlite3_vtab_cursor** ppCursor) {
    struct DefineVtab* vtab = (struct DefineVtab*)pVTab;
    struct DefineCursor* cur = sqlite3_malloc64(sizeof(*cur));
    if (!cur)
        return SQLITE_NOMEM;

    *ppCursor = &cur->base;
    cur->param_argv = sqlite3_malloc(sizeof(*cur->param_argv) * vtab->num_inputs);
    return sqlite3_prepare_v2(vtab->db, vtab->sql, vtab->sql_len, &cur->stmt, NULL);
}

SQLITE_PRIVATE
int define_vtab_close(sqlite3_vtab_cursor* cur) {
    struct DefineCursor* stmtcur = (struct DefineCursor*)cur;
    sqlite3_finalize(stmtcur->stmt);
    sqlite3_free(stmtcur->param_argv);
    sqlite3_free(cur);
    return SQLITE_OK;
}

SQLITE_PRIVATE
int define_vtab_next(sqlite3_vtab_cursor* cur) {
    struct DefineCursor* stmtcur = (struct DefineCursor*)cur;
    int ret = sqlite3_step(stmtcur->stmt);
    if (ret == SQLITE_ROW) {
        stmtcur->rowid++;
        return SQLITE_OK;
    }
    return ret == SQLITE_DONE ? SQLITE_OK : ret;
}

SQLITE_PRIVATE
int define_vtab_rowid(sqlite3_vtab_cursor* cur, sqlite_int64* pRowid) {
    *pRowid = ((struct DefineCursor*)cur)->rowid;
    return SQLITE_OK;
}

SQLITE_PRIVATE
int define_vtab_eof(sqlite3_vtab_cursor* cur) {
    return !sqlite3_stmt_busy(((struct DefineCursor*)cur)->stmt);
}

SQLITE_PRIVATE
int define_vtab_column(sqlite3_vtab_cursor* cur, sqlite3_context* ctx, int i) {
    struct DefineCursor* stmtcur = (struct DefineCursor*)cur;
    int num_outputs = ((struct DefineVtab*)cur->pVtab)->num_outputs;
    if (i < num_outputs)
        sqlite3_result_value(ctx, sqlite3_column_value(stmtcur->stmt, i));
    else if (i - num_outputs < stmtcur->param_argc)
        sqlite3_result_value(ctx, stmtcur->param_argv[i - num_outputs]);
    return SQLITE_OK;
}

SQLITE_PRIVATE
int define_vtab_filter(sqlite3_vtab_cursor* cur,
                              int idxNum,
                              const char* idxStr,
                              int argc,
                              sqlite3_value** argv) {
	sqlite3_stmt* stmt;
	int ret,i;
	struct DefineCursor* stmtcur = (struct DefineCursor*)cur;
	stmtcur->rowid = 1;
	stmt = stmtcur->stmt;
	sqlite3_reset(stmt);
	sqlite3_clear_bindings(stmt);

	for (i = 0; i < argc; i++)
        if ((ret = sqlite3_bind_value(stmt, idxStr ? ((int*)idxStr)[i] : i + 1, argv[i])) !=
            SQLITE_OK)
            return ret;
    ret = sqlite3_step(stmt);
    if (!(ret == SQLITE_ROW || ret == SQLITE_DONE))
        return ret;

    assert(((struct DefineVtab*)cur->pVtab)->num_inputs >= argc);
    if ((stmtcur->param_argc == argc))  // these seem to persist for the remainder of the statement,
                                       // so just shallow copy
        memcpy(stmtcur->param_argv, argv, sizeof(*stmtcur->param_argv) * argc);

    return SQLITE_OK;
}
// xBestIndex needs to communicate which columns are constrained by the where clause to xFilter;
// in terms of a statement table this translates to which parameters will be available to bind.
SQLITE_PRIVATE
int define_vtab_best_index(sqlite3_vtab* pVTab, sqlite3_index_info* index_info) {
    int num_outputs = ((struct DefineVtab*)pVTab)->num_outputs;
	int out_constraints = 0;
	int col_max = 0;
	sqlite3_uint64 required_cols = 0, used_cols = 0;
	int i,col_index,argc,old_index;
	int* colmap = NULL;
	index_info->orderByConsumed = 0;
	index_info->estimatedCost = 1;
	index_info->estimatedRows = 1;

	for (i = 0; i < index_info->nConstraint; i++) {
        // skip if this is a constraint on one of our output columns
        if (index_info->aConstraint[i].iColumn < num_outputs)
            continue;
        // a given query plan is only usable if all provided "input" columns are usable and have
        // equal constraints only is this redundant / an EQ constraint ever unusable?
        if (!index_info->aConstraint[i].usable ||
            index_info->aConstraint[i].op != SQLITE_INDEX_CONSTRAINT_EQ)
            return SQLITE_CONSTRAINT;

        col_index = index_info->aConstraint[i].iColumn - num_outputs;
        index_info->aConstraintUsage[i].argvIndex = col_index + 1;
        index_info->aConstraintUsage[i].omit = 1;

        if (col_index + 1 > col_max)
            col_max = col_index + 1;
        if (col_index < 64)
            used_cols |= 1ull << col_index;

        out_constraints++;
    }

    // if the constrained columns are contiguous then we can just tell sqlite to order the arg
    // vector provided to xFilter in the same order as our column bindings, so there's no need to
    // map between these (this will always be the case when calling the vtab as a table-valued
    // function) only support this optimization for up to 64 constrained columns since checking for
    // continuity more generally would cost as much as just allocating the mapping
    required_cols = (col_max < 64 ? 1ull << col_max : 0ull) - 1;
    if (!out_constraints || (col_max <= 64 && used_cols == required_cols))
        return SQLITE_OK;

    // otherwise map the constraint index as provided to xFilter to column index for bindings
    // if this is sparse e.g. where arg1 = x and arg3 = y then we store this separately in idxStr
	colmap = sqlite3_malloc64(sizeof(*colmap) * out_constraints);
    if (!colmap)
        return SQLITE_NOMEM;

	argc = 0;
    for (i = 0; i < index_info->nConstraint; i++)
        if ((old_index == index_info->aConstraintUsage[i].argvIndex)) {
            colmap[argc] = old_index;
            index_info->aConstraintUsage[i].argvIndex = ++argc;
        }

    index_info->idxStr = (char*)colmap;
    index_info->needToFreeIdxStr = 1;

    return SQLITE_OK;
}

SQLITE_PRIVATE
  sqlite3_module DefineModule = {
  /* iVersion    */ 0,
  /* xCreate     */ define_vtab_create,
  /* xConnect    */ define_vtab_connect,
  /* xBestIndex  */ define_vtab_best_index,
  /* xDisconnect */ define_vtab_destroy,
  /* xDestroy    */ 0,
  /* xOpen       */ define_vtab_open,
  /* xClose      */ define_vtab_close,
  /* xFilter     */ define_vtab_filter,
  /* xNext       */ define_vtab_next,
  /* xEof        */ define_vtab_eof,
  /* xColumn     */ define_vtab_column,
  /* xRowid      */ define_vtab_rowid,
  /* xUpdate     */ 0,
  /* xBegin      */ 0,
  /* xSync       */ 0,
  /* xCommit     */ 0,
  /* xRollback   */ 0,
  /* xFindMethod */ 0,
  /* xRename     */ 0,
  /* xSavepoint  */ 0,
  /* xRelease    */ 0,
  /* xRollbackTo */ 0,
  /* xShadowName */ 0
  };

#ifdef SQLITE_AMALGAMATION
static int sqlite3_define_register(sqlite3* db) {
#else
int sqlite3_define_register(sqlite3* db) {
#endif
  int rc = SQLITE_OK;
#ifndef SQLITE_OMIT_VIRTUALTABLE
  rc = sqlite3_create_module(db, "define", &DefineModule, NULL);
  if( rc==SQLITE_OK ){
    rc = register_define_functions(db);
   }
#endif /* SQLITE_OMIT_VIRTUALTABLE */
  return rc;
}


SQLITE_EXPORT
SQLITE_API 
int sqlite3_eval_register(sqlite3 *db){
  int rc;
  rc = sqlite3_create_function(db, "eval", 1,
                               SQLITE_UTF8|SQLITE_DIRECTONLY, 0,
                               EvalFunc, 0, 0);
  if( rc==SQLITE_OK ){
    rc = sqlite3_create_function(db, "eval", 2,
                                 SQLITE_UTF8|SQLITE_DIRECTONLY, 0,
                                 EvalFunc, 0, 0);
  }
  if( rc==SQLITE_OK ){
    rc = sqlite3_define_register(db);
  }
  return rc;
}

SQLITE_EXPORT 
SQLITE_API 
int sqlite3_eval_init(sqlite3 *db, char **pzErrMsg, const sqlite3_api_routines *pApi){
  
  int rc = SQLITE_OK;
#ifndef SQLITE_CORE
  SQLITE_EXTENSION_INIT2(pApi);
#endif
  (void)pzErrMsg;  /* Unused parameter */
  
  rc = sqlite3_eval_register(db);  
  
  if( rc==SQLITE_OK ){
    rc = sqlite3_define_register(db);
  }
  return rc;
}
