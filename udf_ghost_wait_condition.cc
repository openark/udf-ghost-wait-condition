
#include <my_global.h>
#include <my_sys.h>

#include <new>
#include <vector>
#include <algorithm>

#if defined(MYSQL_SERVER)
#include <m_string.h>		/* To get strmov() */
#else
/* when compiled as standalone */
#include <string.h>
#define strmov(a,b) stpcpy(a,b)
#endif

#include <mysql.h>
#include <sql_class.h>
#include <mysqld.h>
#include <ctype.h>

#ifdef _WIN32
/* inet_aton needs winsock library */
#pragma comment(lib, "ws2_32")
#endif

#ifdef HAVE_DLOPEN

/* These must be right or mysqld will not find the symbol! */

C_MODE_START;
my_bool create_ghost_wait_condition_init(UDF_INIT *initid, UDF_ARGS *args, char *message);
void create_ghost_wait_condition_deinit(UDF_INIT *initid);
longlong create_ghost_wait_condition(UDF_INIT *initid, UDF_ARGS *args, char *is_null, char *error);

my_bool destroy_ghost_wait_condition_init(UDF_INIT *initid, UDF_ARGS *args, char *message);
void destroy_ghost_wait_condition_deinit(UDF_INIT *initid);
longlong destroy_ghost_wait_condition(UDF_INIT *initid, UDF_ARGS *args, char *is_null, char *error);

my_bool ghost_wait_on_condition_init(UDF_INIT *initid, UDF_ARGS *args, char *message);
void ghost_wait_on_condition_deinit(UDF_INIT *initid);
longlong ghost_wait_on_condition(UDF_INIT *initid, UDF_ARGS *args, char *is_null, char *error);

C_MODE_END;

/*
create function create_ghost_wait_condition RETURNS INTEGER SONAME 'udf_ghost_wait_condition.so' ;
create function destroy_ghost_wait_condition RETURNS INTEGER SONAME 'udf_ghost_wait_condition.so' ;
create function ghost_wait_on_condition RETURNS INTEGER SONAME 'udf_ghost_wait_condition.so' ;

create function create_ghost_wait_condition RETURNS INTEGER SONAME 'udf_ghost_wait_condition.so' ;create function destroy_ghost_wait_condition RETURNS INTEGER SONAME 'udf_ghost_wait_condition.so' ;create function ghost_wait_on_condition RETURNS INTEGER SONAME 'udf_ghost_wait_condition.so' ;

drop function create_ghost_wait_condition;
drop function destroy_ghost_wait_condition;
drop function ghost_wait_on_condition;

drop function create_ghost_wait_condition;drop function destroy_ghost_wait_condition;drop function ghost_wait_on_condition;

create or replace view t9v as select * from t9 where ghost_wait_on_condition(0) >= -1 with check option;
*/

/*
  mysql_mutex_lock(&rli->log_space_lock);

  // Tell the I/O thread to take the relay_log_space_limit into account
  rli->ignore_log_space_limit= 0;
  mysql_mutex_unlock(&rli->log_space_lock);


  sql/rpl_rli.cc
  131:  mysql_mutex_init(key_relay_log_info_log_space_lock,
  132:                   &log_space_lock, MY_MUTEX_INIT_FAST);
  177:  mysql_mutex_destroy(&log_space_lock);

  sql/rpl_rli.h
  302:  mysql_mutex_t log_space_lock;
  mysql_mutex_t log_space_lock;
  mysql_cond_t log_space_cond;

  extern PSI_mutex_key
    key_relay_log_info_log_space_lock

    mysql_mutex_init(key_relay_log_info_log_space_lock,
                     &log_space_lock, MY_MUTEX_INIT_FAST);
    mysql_cond_init(key_relay_log_info_log_space_cond, &log_space_cond, NULL);
    mysql_mutex_destroy(&log_space_lock);
    mysql_cond_destroy(&log_space_cond);

    sql/binlog.cc
    4141:  mysql_cond_broadcast(&rli->log_space_cond);

    sql/mysqld.cc
    9585:  key_relay_log_info_data_cond, key_relay_log_info_log_space_cond,
    9626:  { &key_relay_log_info_log_space_cond, "Relay_log_info::log_space_cond", 0},

    sql/mysqld.h
    381:  key_relay_log_info_data_cond, key_relay_log_info_log_space_cond,

    sql/rpl_rli.cc
    133:  mysql_cond_init(key_relay_log_info_log_space_cond, &log_space_cond, NULL);
    178:  mysql_cond_destroy(&log_space_cond);

    sql/rpl_rli.h
    303:  mysql_cond_t log_space_cond;

    sql/rpl_slave.cc
    2616:  thd->ENTER_COND(&rli->log_space_cond,
    2623:    mysql_cond_wait(&rli->log_space_cond, &rli->log_space_lock);
    7820:        mysql_cond_broadcast(&rli->log_space_cond);

    mysql_mutex_lock(&rli->log_space_lock);
    thd->ENTER_COND(&rli->log_space_cond,
                    &rli->log_space_lock,
                    &stage_waiting_for_relay_log_space,
                    &old_stage);
    while (rli->log_space_limit < rli->log_space_total &&
           !(slave_killed=io_slave_killed(thd,mi)) &&
           !rli->ignore_log_space_limit)
      mysql_cond_wait(&rli->log_space_cond, &rli->log_space_lock);
      error= mysql_cond_timedwait(&di->cond, &di->mutex, &abstime);

      thd->EXIT_COND(&old_stage);

      struct timespec abstime;
      set_timespec(abstime, delayed_insert_timeout);
*/

PSI_cond_key key_ghost_wait_cond;
PSI_mutex_key key_ghost_wait_lock;

mysql_mutex_t ghost_wait_lock;
mysql_cond_t  ghost_wait_cond;
int           ghost_wait_cond_used;

// 30 seconds till timeout
#define GHOST_WAIT_TIMEOUT 30

struct StaticBlock {
  StaticBlock() {
    ghost_wait_cond_used = 0;
    mysql_mutex_init(key_ghost_wait_lock, &ghost_wait_lock, MY_MUTEX_INIT_FAST);
    mysql_cond_init(key_ghost_wait_cond, &ghost_wait_cond, NULL);
  }
};
static StaticBlock staticBlock;


my_bool create_ghost_wait_condition_init(UDF_INIT *initid, UDF_ARGS *args, char *message)
{
  if (args->arg_count != 1)
  {
    strmov(message,"This function expects 1 integer argument. Negative for noop; Other for creating a condition");
    return 1;
  }
  args->arg_type[0]= INT_RESULT;		/* Force argument to int */

  return 0;
}

void create_ghost_wait_condition_deinit(UDF_INIT *initid __attribute__((unused)))
{
}

longlong create_ghost_wait_condition(UDF_INIT *initid __attribute__((unused)), UDF_ARGS *args,
                    char *is_null __attribute__((unused)),
                    char *error __attribute__((unused)))
{
  int success = 0;

  longlong input_val=0;
  if (args->arg_count) {
    input_val= *((longlong*) args->args[0]);
  }
  if (input_val < 0) {
    return input_val;
  }

  mysql_mutex_lock(&ghost_wait_lock);
  if (ghost_wait_cond_used == 0) {
    ghost_wait_cond_used = 1;
    success = 1;
  }
  mysql_mutex_unlock(&ghost_wait_lock);
  return success;
}


my_bool destroy_ghost_wait_condition_init(UDF_INIT *initid, UDF_ARGS *args, char *message)
{
  if (args->arg_count != 1)
  {
    strmov(message,"This function expects 1 integer argument. Negative for noop");
    return 1;
  }
  args->arg_type[0]= INT_RESULT;		/* Force argument to int */

  return 0;
}

void destroy_ghost_wait_condition_deinit(UDF_INIT *initid __attribute__((unused)))
{
}

longlong destroy_ghost_wait_condition(UDF_INIT *initid __attribute__((unused)), UDF_ARGS *args,
                    char *is_null __attribute__((unused)),
                    char *error __attribute__((unused)))
{
  int success = 0;

  longlong input_val=0;
  if (args->arg_count) {
    input_val= *((longlong*) args->args[0]);
  }
  if (input_val < 0) {
    return input_val;
  }

  mysql_mutex_lock(&ghost_wait_lock);
  if (ghost_wait_cond_used == 1) {
    mysql_cond_broadcast(&ghost_wait_cond);
    ghost_wait_cond_used = 0;
    success = 1;
  }
  mysql_mutex_unlock(&ghost_wait_lock);
  return success;
}



my_bool ghost_wait_on_condition_init(UDF_INIT *initid, UDF_ARGS *args, char *message)
{
  if (args->arg_count != 1)
  {
    strmov(message,"This function expects 1 integer argument. Negative for noop");
    return 1;
  }
  args->arg_type[0]= INT_RESULT;		/* Force argument to int */

  return 0;
}

void ghost_wait_on_condition_deinit(UDF_INIT *initid __attribute__((unused)))
{
}

// ghost_wait_on_condition attempts to wait on a condition. Return value:
// -1 on noop (has not waited)
// 0 when waited and released
// >0 on error, namely on wait-timeout
longlong ghost_wait_on_condition(UDF_INIT *initid __attribute__((unused)), UDF_ARGS *args,
                    char *is_null __attribute__((unused)),
                    char *error __attribute__((unused)))
{
  int wait_result = -1;

  longlong input_val=0;
  if (args->arg_count) {
    input_val= *((longlong*) args->args[0]);
  }
  if (input_val < 0) {
    return input_val;
  }

  mysql_mutex_lock(&ghost_wait_lock);
  if (ghost_wait_cond_used == 1) {
    //mysql_cond_wait(&ghost_wait_cond, &ghost_wait_lock);
    struct timespec abstime;
    set_timespec(abstime, GHOST_WAIT_TIMEOUT);
    wait_result = mysql_cond_timedwait(&ghost_wait_cond, &ghost_wait_lock, &abstime);
    //mysql_cond_timedwait(&ghost_wait_cond, &ghost_wait_lock, &ghost_condition_timeout);
    //wait_result=1;
  }
  mysql_mutex_unlock(&ghost_wait_lock);
  return wait_result;
}


#endif /* HAVE_DLOPEN */
