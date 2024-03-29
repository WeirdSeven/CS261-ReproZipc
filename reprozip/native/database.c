#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <sqlite3.h>

#include "database.h"
#include "log.h"

#define count(x) (sizeof((x))/sizeof(*(x)))
#define check(r) do { if((r) != SQLITE_OK) { goto sqlerror; } } while(0)
//#define check(r) 

static sqlite3_uint64 gettime(void)
{
    sqlite3_uint64 timestamp;
    struct timespec now;
    if(clock_gettime(CLOCK_MONOTONIC, &now) == -1)
    {
        /* LCOV_EXCL_START : clock_gettime() is unlikely to fail */
        log_critical(0, "getting time failed (clock_gettime): %s",
                     strerror(errno));
        exit(1);
        /* LCOV_EXCL_END */
    }
    timestamp = now.tv_sec;
    timestamp *= 1000000000;
    timestamp += now.tv_nsec;
    return timestamp;
}

static sqlite3 *db;
//static sqlite3_stmt *stmt_last_rowid;
//static sqlite3_stmt *stmt_insert_process;
//static sqlite3_stmt *stmt_set_exitcode;
//static sqlite3_stmt *stmt_insert_file;
//static sqlite3_stmt *stmt_insert_exec;
//static sqlite3_stmt *stmt_insert_connection;

static int run_id = -1;

int db_init(const char *filename)
{
	//printf("I am initing!\n");
    int tables_exist;

    check(sqlite3_open(filename, &db));
    log_debug(0, "database file opened: %s", filename);

    check(sqlite3_exec(db, "BEGIN IMMEDIATE;", NULL, NULL, NULL));

    {
        int ret;
        const char *sql = ""
                "SELECT name FROM SQLITE_MASTER "
                "WHERE type='table';";
        sqlite3_stmt *stmt_get_tables;
        unsigned int found = 0x00;
        check(sqlite3_prepare_v2(db, sql, -1, &stmt_get_tables, NULL));
        while((ret = sqlite3_step(stmt_get_tables)) == SQLITE_ROW)
        {
            const char *colname = (const char*)sqlite3_column_text(
                    stmt_get_tables, 0);
            if(strcmp("processes", colname) == 0)
                found |= 0x01;
            else if(strcmp("opened_files", colname) == 0)
                found |= 0x02;
            else if(strcmp("executed_files", colname) == 0)
                found |= 0x04;
            else if(strcmp("connections", colname) == 0)
                found |= 0x08;
            else
                goto wrongschema;
        }
        if(found == 0x00)
            tables_exist = 0;
        else if(found == 0x0F)
            tables_exist = 1;
        else
        {
        wrongschema:
            log_critical(0, "database schema is wrong");
            return -1;
        }
        sqlite3_finalize(stmt_get_tables);
        if(ret != SQLITE_DONE)
            goto sqlerror;
    }

    if(!tables_exist)
    {
        const char *sql[] = {
            "CREATE TABLE processes("
            "    id INTEGER NOT NULL PRIMARY KEY,"
            "    run_id INTEGER NOT NULL,"
            "    parent INTEGER,"
            "    timestamp INTEGER NOT NULL,"
            "    exit_timestamp INTEGER,"
            "    cpu_time INTEGER,"
            "    is_thread BOOLEAN NOT NULL,"
            "    exitcode INTEGER"
            "    );",
            "CREATE INDEX proc_parent_idx ON processes(parent);",
            "CREATE TABLE opened_files("
            "    id INTEGER NOT NULL PRIMARY KEY,"
            "    run_id INTEGER NOT NULL,"
            "    name TEXT NOT NULL,"
            "    timestamp INTEGER NOT NULL,"
            "    mode INTEGER NOT NULL,"
            "    is_directory BOOLEAN NOT NULL,"
            "    process INTEGER NOT NULL"
            "    );",
            "CREATE INDEX open_proc_idx ON opened_files(process);",
            "CREATE TABLE executed_files("
            "    id INTEGER NOT NULL PRIMARY KEY,"
            "    name TEXT NOT NULL,"
            "    run_id INTEGER NOT NULL,"
            "    timestamp INTEGER NOT NULL,"
            "    process INTEGER NOT NULL,"
            "    argv TEXT NOT NULL,"
            "    envp TEXT NOT NULL,"
            "    workingdir TEXT NOT NULL"
            "    );",
            "CREATE INDEX exec_proc_idx ON executed_files(process);",
            "CREATE TABLE connections("
            "    id INTEGER NOT NULL PRIMARY KEY,"
            "    run_id INTEGER NOT NULL,"
            "    timestamp INTEGER NOT NULL,"
            "    process INTEGER NOT NULL,"
            "    inbound INTEGER NOT NULL,"
            "    family TEXT NULL,"
            "    protocol TEXT NULL,"
            "    address TEXT NULL"
            "    );",
            "CREATE INDEX connections_proc_idx ON connections(process);",
        };
        size_t i;

        //time_t exec_start_time = clock();

        for(i = 0; i < count(sql); ++i)
            check(sqlite3_exec(db, sql[i], NULL, NULL, NULL));

        //time_t exec_end_time = clock();
        //printf("\t\t-> the exec time in database is : %f\n", (double)(exec_end_time - exec_start_time)/CLOCKS_PER_SEC);

    }

    /* Get the first unused run_id */
    {
        sqlite3_stmt *stmt_get_run_id;
        const char *sql = "SELECT max(run_id) + 1 FROM processes;";
        check(sqlite3_prepare_v2(db, sql, -1, &stmt_get_run_id, NULL));
        if(sqlite3_step(stmt_get_run_id) != SQLITE_ROW)
        {
            sqlite3_finalize(stmt_get_run_id);
            goto sqlerror;
        }
        run_id = sqlite3_column_int(stmt_get_run_id, 0);
        if(sqlite3_step(stmt_get_run_id) != SQLITE_DONE)
        {
            sqlite3_finalize(stmt_get_run_id);
            goto sqlerror;
        }
        sqlite3_finalize(stmt_get_run_id);
    }
    log_debug(0, "This is run %d", run_id);

    {
        //const char *sql = ""
        //        "SELECT last_insert_rowid()";
        //check(sqlite3_prepare_v2(db, sql, -1, &stmt_last_rowid, NULL));
    }

    {
        //const char *sql = ""
        //        "INSERT INTO processes(run_id, parent, timestamp, is_thread) "
        //        "VALUES(?, ?, ?, ?)";

        //time_t prepare_start_time = clock();

        //check(sqlite3_prepare_v2(db, sql, -1, &stmt_insert_process, NULL));

        //time_t prepare_end_time = clock();
        //printf("\t\t-> the prepare time(insert process) in database is : %f\n", (double)(prepare_end_time - prepare_start_time)/CLOCKS_PER_SEC);

    }

    {
    //    const char *sql = ""
    //            "UPDATE processes SET exitcode=?, exit_timestamp=?, "
    //            "        cpu_time=? "
    //            "WHERE id=?";
    //    check(sqlite3_prepare_v2(db, sql, -1, &stmt_set_exitcode, NULL));
    }

    {
    //    const char *sql = ""
    //            "INSERT INTO opened_files(run_id, name, timestamp, "
    //            "        mode, is_directory, process) "
    //            "VALUES(?, ?, ?, ?, ?, ?)";

        //time_t prepare_start_time = clock();

    //    check(sqlite3_prepare_v2(db, sql, -1, &stmt_insert_file, NULL));
    
        //time_t prepare_end_time = clock();
        //printf("\t\t-> the prepare time(insert open_files) in database is : %f\n", (double)(prepare_end_time - prepare_start_time)/CLOCKS_PER_SEC);
	}

    {
        //const char *sql = ""
        //        "INSERT INTO executed_files(run_id, name, timestamp, process, "
        //        "        argv, envp, workingdir) "
        //        "VALUES(?, ?, ?, ?, ?, ?, ?)";

        //time_t prepare_start_time = clock();

        //check(sqlite3_prepare_v2(db, sql, -1, &stmt_insert_exec, NULL));
    
        //time_t prepare_end_time = clock();
        //printf("\t\t-> the prepare time(insert exec_files) in database is : %f\n", (double)(prepare_end_time - prepare_start_time)/CLOCKS_PER_SEC);
}

    {
    //    const char *sql = ""
    //            "INSERT INTO connections(run_id, timestamp, process, "
    //            "        inbound, family, protocol, address) "
    //            "VALUES(?, ?, ?, ?, ?, ?, ?)";
    //    check(sqlite3_prepare_v2(db, sql, -1, &stmt_insert_connection, NULL));
    }

    return 0;

sqlerror:
    log_critical(0, "sqlite3 error creating database: %s", sqlite3_errmsg(db));
    return -1;
}

int db_close(int rollback)
{
	//printf("I am closing!\n");
    if(rollback)
    {
        check(sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL));
    }
    else
    {
        check(sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL));
    }
    log_debug(0, "database file closed%s", rollback?" (rolled back)":"");
    //check(sqlite3_finalize(stmt_last_rowid));
    //check(sqlite3_finalize(stmt_insert_process));
    //check(sqlite3_finalize(stmt_set_exitcode));
    //check(sqlite3_finalize(stmt_insert_file));
    //check(sqlite3_finalize(stmt_insert_exec));
    //check(sqlite3_finalize(stmt_insert_connection));
    check(sqlite3_close(db));
    run_id = -1;
    return 0;

sqlerror:
    log_critical(0, "sqlite3 error on exit: %s", sqlite3_errmsg(db));
    return -1;
}

#define DB_NO_PARENT ((unsigned int)-2)

int db_add_process(unsigned int *id, unsigned int parent_id,
                   const char *working_dir, int is_thread)
{
	//printf("I am adding process!\n");
    char sql_insert_process[1024];
    sql_insert_process[0] = '\0';
    if(parent_id == DB_NO_PARENT)
    {
        sprintf(sql_insert_process, "INSERT INTO processes(run_id, parent, timestamp, is_thread) VALUES(%d, null, %lld, %d)", run_id, gettime(), is_thread?1:0);
    }
    else
    {
        sprintf(sql_insert_process, "INSERT INTO processes(run_id, parent, timestamp, is_thread) VALUES(%d, %d, %lld, %d)", run_id, parent_id, gettime(), is_thread?1:0);
    }



/*
    check(sqlite3_bind_int(stmt_insert_process, 1, run_id));
    if(parent_id == DB_NO_PARENT)
    {
        check(sqlite3_bind_null(stmt_insert_process, 2));
    }
    else
    {
        check(sqlite3_bind_int(stmt_insert_process, 2, parent_id));
    }
    // This assumes that we won't go over 2^32 seconds (~135 years) 
    check(sqlite3_bind_int64(stmt_insert_process, 3, gettime()));
    check(sqlite3_bind_int(stmt_insert_process, 4, is_thread?1:0));
*/
    //time_t step_start_time = clock();

    check(sqlite3_exec(db, sql_insert_process, NULL, NULL, NULL));
    //if(sqlite3_step(stmt_insert_process) != SQLITE_DONE)
    //    goto sqlerror;

    ////time_t step_end_time = clock();
    ////printf("\t\t-> the step time(insert process) in database is : %f\n", (double)(step_end_time - step_start_time)/CLOCKS_PER_SEC);

    //sqlite3_reset(stmt_insert_process);

    /* Get id */
    sqlite3_stmt *stmt_last_rowid;
	{
        const char *sql = ""
                "SELECT last_insert_rowid()";
        check(sqlite3_prepare_v2(db, sql, -1, &stmt_last_rowid, NULL));
    }
    if(sqlite3_step(stmt_last_rowid) != SQLITE_ROW)
        goto sqlerror;
    *id = sqlite3_column_int(stmt_last_rowid, 0);
    if(sqlite3_step(stmt_last_rowid) != SQLITE_DONE)
        goto sqlerror;
    sqlite3_finalize(stmt_last_rowid);

    return db_add_file_open(*id, working_dir, FILE_WDIR, 1);

sqlerror:
    printf("sqlite3 error inserting process: %s\n", sqlite3_errmsg(db));
    /* LCOV_EXCL_START : Insertions shouldn't fail */
    log_critical(0, "sqlite3 error inserting process: %s", sqlite3_errmsg(db));
    return -1;
    /* LCOV_EXCL_END */
}

int db_add_first_process(unsigned int *id, const char *working_dir)
{
    return db_add_process(id, DB_NO_PARENT, working_dir, 0);
}

int db_add_exit(unsigned int id, int exitcode, int cpu_time)
{
	char sql_set_exitcode[1024];
	sql_set_exitcode[0] = '\0';
	sprintf(sql_set_exitcode, "UPDATE processes SET exitcode=%d, exit_timestamp=%lld, cpu_time=%d WHERE id=%d", exitcode, gettime(), cpu_time, id);
	check(sqlite3_exec(db, sql_set_exitcode, NULL, NULL, NULL));
    //check(sqlite3_bind_int(stmt_set_exitcode, 1, exitcode));
    //check(sqlite3_bind_int64(stmt_set_exitcode, 2, gettime()));
    //check(sqlite3_bind_int(stmt_set_exitcode, 3, cpu_time));
    //check(sqlite3_bind_int(stmt_set_exitcode, 4, id));

    //if(sqlite3_step(stmt_set_exitcode) != SQLITE_DONE)
    //    goto sqlerror;
    //sqlite3_reset(stmt_set_exitcode);

    return 0;

sqlerror:
    /* LCOV_EXCL_START : Insertions shouldn't fail */
    log_critical(0, "sqlite3 error setting exitcode: %s", sqlite3_errmsg(db));
    return -1;
    /* LCOV_EXCL_END */
}

int db_add_file_open(unsigned int process, const char *name,
                     unsigned int mode, int is_dir)
{
	//printf("I am adding open_files!\n");
	char sql_insert_file[1024];
	sql_insert_file[0] = '\0';
    //printf("name = [%s]\n", name);
	sprintf(sql_insert_file, "INSERT INTO opened_files(run_id, name, timestamp, mode, is_directory, process) VALUES(%d, '%s', %lld, %d, %d, %d)", run_id, name, gettime(), mode, is_dir, process);
	check(sqlite3_exec(db, sql_insert_file, NULL, NULL, NULL));

    //check(sqlite3_bind_int(stmt_insert_file, 1, run_id));
    //check(sqlite3_bind_text(stmt_insert_file, 2, name, -1, SQLITE_TRANSIENT));
    ///* This assumes that we won't go over 2^32 seconds (~135 years) */
    //check(sqlite3_bind_int64(stmt_insert_file, 3, gettime()));
    //check(sqlite3_bind_int(stmt_insert_file, 4, mode));
    //check(sqlite3_bind_int(stmt_insert_file, 5, is_dir));
    //check(sqlite3_bind_int(stmt_insert_file, 6, process));

    ////time_t step_start_time = clock();
    //if(sqlite3_step(stmt_insert_file) != SQLITE_DONE)
    //    goto sqlerror;

    //time_t step_end_time = clock();
    //printf("\t\t-> the step time(insert open_files) in database is : %f\n", (double)(step_end_time - step_start_time)/CLOCKS_PER_SEC);

    //sqlite3_reset(stmt_insert_file);
    return 0;

sqlerror:
    /* LCOV_EXCL_START : Insertions shouldn't fail */
    log_critical(0, "sqlite3 error inserting file: %s", sqlite3_errmsg(db));
    return -1;
    /* LCOV_EXCL_END */
}

static char *strarray2nulsep(const char *const *array, size_t *plen)
{
    char *list;
    size_t len = 0;
    {
        const char *const *a = array;
        while(*a)
        {
            len += strlen(*a) + 1;
            ++a;
        }
    }
    {
        const char *const *a = array;
        char *p;
        p = list = malloc(len);
        while(*a)
        {
            const char *s = *a;
            while(*s)
                *p++ = *s++;
            *p++ = '\0';
            ++a;
        }
    }
    *plen = len;
    return list;
}

int db_add_exec(unsigned int process, const char *binary,
                const char *const *argv, const char *const *envp,
                const char *workingdir)
{
    //printf("I am adding exec_files!\n");
	sqlite3_stmt *stmt_insert_exec;

	const char *sql = ""
            "INSERT INTO executed_files(run_id, name, timestamp, process, "
            "        argv, envp, workingdir) "
            "VALUES(?, ?, ?, ?, ?, ?, ?)";
    check(sqlite3_prepare_v2(db, sql, -1, &stmt_insert_exec, NULL));
 
	check(sqlite3_bind_int(stmt_insert_exec, 1, run_id));
    check(sqlite3_bind_text(stmt_insert_exec, 2, binary,
                            -1, SQLITE_TRANSIENT));
    /* This assumes that we won't go over 2^32 seconds (~135 years) */
    check(sqlite3_bind_int64(stmt_insert_exec, 3, gettime()));
    check(sqlite3_bind_int(stmt_insert_exec, 4, process));
    {
        size_t len;
        char *arglist = strarray2nulsep(argv, &len);
        check(sqlite3_bind_text(stmt_insert_exec, 5, arglist, len,
                                SQLITE_TRANSIENT));
        free(arglist);
    }
    {
        size_t len;
        char *envlist = strarray2nulsep(envp, &len);
        check(sqlite3_bind_text(stmt_insert_exec, 6, envlist, len,
                                SQLITE_TRANSIENT));
        free(envlist);
    }
    check(sqlite3_bind_text(stmt_insert_exec, 7, workingdir,
                            -1, SQLITE_TRANSIENT));

    //time_t step_start_time = clock();

    if(sqlite3_step(stmt_insert_exec) != SQLITE_DONE)
        goto sqlerror;
	sqlite3_finalize(stmt_insert_exec);

    //time_t step_end_time = clock();
    //printf("\t\t-> the step time(insert exec_files) in database is : %f\n", (double)(step_end_time - step_start_time)/CLOCKS_PER_SEC);

	//
	//char sql_insert_exec[100000];
	//sql_insert_exec[0] = '\0';

    //size_t len1;
    //char *arglist = strarray2nulsep(argv, &len1);
    //size_t len2;
    //char *envlist = strarray2nulsep(envp, &len2);
    //printf("%.*s\n", (int)len2, envlist);
	//sprintf(sql_insert_exec, "INSERT INTO executed_files(run_id, name, timestamp, process, argv, envp, workingdir) VALUES(%d, '%s', %lld, %d, '%.*s', '%.*s', '%s')" , run_id, binary, gettime(), process, (int)len1, arglist, (int)len2, envlist, workingdir);
    //free(arglist);
    //free(envlist);
	//check(sqlite3_exec(db, sql_insert_exec, NULL, NULL, NULL));

    
    //sqlite3_reset(stmt_insert_exec);
    return 0;

sqlerror:
    /* LCOV_EXCL_START : Insertions shouldn't fail */
    log_critical(0, "sqlite3 error inserting exec: %s", sqlite3_errmsg(db));
    return -1;
    /* LCOV_EXCL_END */
}

int db_add_connection(unsigned int process, int inbound, const char *family,
                      const char *protocol, const char *address)
{
	//printf("I am adding connections!\n");
	char sql_insert_connection[1024];
	sql_insert_connection[0] = '\0';

	if(family == NULL)
	{
		if(protocol == NULL)
		{
			if(address == NULL)
			{
				// all null
				sprintf(sql_insert_connection, "INSERT INTO connections(run_id, timestamp, process, inbound, family, protocol, address) VALUES(%d, %lld, %d, %d, null, null, null)" , run_id, gettime(), process, inbound?1:0);
			}
			else 
			{
				// f null, p null, a %s
				sprintf(sql_insert_connection, "INSERT INTO connections(run_id, timestamp, process, inbound, family, protocol, address) VALUES(%d, %lld, %d, %d, null, null, '%s')'" , run_id, gettime(), process, inbound?1:0, address);
			}
		}
		else
		{
			if(address == NULL)
			{
				//f null, p %s, a null
				sprintf(sql_insert_connection, "INSERT INTO connections(run_id, timestamp, process, inbound, family, protocol, address) VALUES(%d, %lld, %d, %d, null, '%s', null)" , run_id, gettime(), process, inbound?1:0, protocol);
			}
			else 
			{
				//f null, p %s, a %s
				sprintf(sql_insert_connection, "INSERT INTO connections(run_id, timestamp, process, inbound, family, protocol, address) VALUES(%d, %lld, %d, %d, null, '%s', '%s')" , run_id, gettime(), process, inbound?1:0, protocol, address);
			}
		}
	}
    else
    {
    	if(protocol == NULL)
		{
			if(address == NULL)
			{
				//f %s, p null, a null
				sprintf(sql_insert_connection, "INSERT INTO connections(run_id, timestamp, process, inbound, family, protocol, address) VALUES(%d, %lld, %d, %d, '%s', null, null)" , run_id, gettime(), process, inbound?1:0, family);
			}
			else 
			{
				//f %s. p null, a %s
				sprintf(sql_insert_connection, "INSERT INTO connections(run_id, timestamp, process, inbound, family, protocol, address) VALUES(%d, %lld, %d, %d, '%s', null, '%s')" , run_id, gettime(), process, inbound?1:0, family, address);
			}
		}
		else
		{
			if(address == NULL)
			{
				//f %s, p %s, a null
				sprintf(sql_insert_connection, "INSERT INTO connections(run_id, timestamp, process, inbound, family, protocol, address) VALUES(%d, %lld, %d, %d, '%s', '%s', null)" , run_id, gettime(), process, inbound?1:0, family, protocol);
			}
			else 
			{
				//f %s, p %s, a %s
				sprintf(sql_insert_connection, "INSERT INTO connections(run_id, timestamp, process, inbound, family, protocol, address) VALUES(%d, %lld, %d, %d, '%s', '%s', '%s')" , run_id, gettime(), process, inbound?1:0, family, protocol, address);
			}
		}
    }

	check(sqlite3_exec(db, sql_insert_connection, NULL, NULL, NULL));

/*
    check(sqlite3_bind_int(stmt_insert_connection, 1, run_id));
    check(sqlite3_bind_int64(stmt_insert_connection, 2, gettime()));
    check(sqlite3_bind_int(stmt_insert_connection, 3, process));
    check(sqlite3_bind_int(stmt_insert_connection, 4, inbound?1:0));
    if(family == NULL)
        check(sqlite3_bind_null(stmt_insert_connection, 5));
    else
        check(sqlite3_bind_text(stmt_insert_connection, 5, family,
                                -1, SQLITE_TRANSIENT));
    if(protocol == NULL)
        check(sqlite3_bind_null(stmt_insert_connection, 6));
    else
        check(sqlite3_bind_text(stmt_insert_connection, 6, protocol,
                                -1, SQLITE_TRANSIENT));
    if(address == NULL)
        check(sqlite3_bind_null(stmt_insert_connection, 7));
    else
        check(sqlite3_bind_text(stmt_insert_connection, 7, address,
                                -1, SQLITE_TRANSIENT));
*/
    //if(sqlite3_step(stmt_insert_connection) != SQLITE_DONE)
    //    goto sqlerror;
    //sqlite3_reset(stmt_insert_connection);
    return 0;

sqlerror:
    /* LCOV_EXCL_START : Insertions shouldn't fail */
    log_critical(0, "sqlite3 error inserting network connection: %s",
                 sqlite3_errmsg(db));
    return -1;
    /* LCOV_EXCL_END */
}
