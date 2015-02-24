/*
 * R Jesse Chaney
 * chaneyr@eecs.orst.edu
 * CS344-001
 * Fall 2014
 * Oregon State University
 * School of EECS
 */

/*
 * $Author: chaneyr $
 * $Date: 2014/11/17 12:38:02 $
 * $RCSfile: ipdb.h,v $
 * $Revision: 1.1 $
 */


#ifndef _IPDB_H_
# define _IPDB_H_

#include <semaphore.h>

// I don't think I'd load anywhere near this number, but
// you never know...
# define MAX_ROWS 10000

// This could probably be a bit smaller.
# define NAME_SIZE 50

// This is what I use for one row in the shared memory
// database table.
typedef struct ip_row {
  sem_t row_lock;
  char row_name[NAME_SIZE];
  char row_address4[NAME_SIZE]; // IPv4 address name
  char row_address6[NAME_SIZE]; // IPv6 address name
} ip_row_t;

// A unique name for each user's shared memory object.
# define SHARED_MEM_BASE "/IPDB"
# define SHARED_MEM_NAME(_BUF_) sprintf(_BUF_,"%s__%s"\
		   ,SHARED_MEM_BASE,getenv("LOGNAME"));

// The default prompt.
# define PROMPT ">>> "

// The commands.
# define CMD_HELP         "help"
# define CMD_EXIT         "exit"
# define CMD_FETCH        "fetch"
# define CMD_CHECK        "check"
# define CMD_SHOW         "show"
# define CMD_SAVE         "save"
# define CMD_LOAD         "load"
# define CMD_CLEAR        "clear"
# define CMD_LOCK_TABLE   "lock_table"
# define CMD_UNLOCK_TABLE "unlock_table"
# define CMD_LOCK_ROW     "lock_row"
# define CMD_UNLOCK_ROW   "unlock_row"

#endif // _IPDB_H_
