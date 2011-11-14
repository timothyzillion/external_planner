/*
 * Copyright 2011 Timothy J. Kordas
 *
 * This is the external-planner hook to insert ourselves into the planning
 * loop of the Postgres backend.
 *
 * Put our shared-lib in the shared_preload_libraries list in postgresql.conf
 *
 * We use a custom-variable-class to retrieve our configuration information:
 *   remote_planner.addr
 *   remote_planner.port
 */

#include "postgres.h"
#include "optimizer/planner.h"
#include "utils/elog.h"
#include "lib/stringinfo.h"
#include "nodes/nodes.h"
#include "nodes/print.h"

#include "fmgr.h"

#ifdef PG_MODULE_MAGIC
PG_MODULE_MAGIC;
#endif

/* Postgres hook interface */
static planner_hook_type prev_planner_hook = NULL;
extern PlannedStmt *remote_planner(Query *parse, int cursorOptions, ParamListInfo boundParams);

/* default config */
#define REMOTE_PLANNER_ADDR_DEFAULT "127.0.0.1"
#define REMOTE_PLANNER_PORT_DEFAULT "12345"

/* Access to postgres global configuration, for our options */
#include "utils/guc.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static bool remote_planner_connected = false;

/* Retrieve planner, config and connect. */
static int remote_planner_connect(void);

static PlannedStmt *remote_planner_send(char *query_tree, uint32_t len);

void
_PG_init(void)
{
    /* activate password checks when the module is loaded */
    prev_planner_hook = planner_hook;
    planner_hook = remote_planner;
}


PlannedStmt *
remote_planner(Query *parse, int cursorOptions, ParamListInfo boundParams)
{
    PlannedStmt *rv = NULL;

    char *parse_tree;
    uint32_t parse_tree_len=0;

    char *parse_tree_pretty;

    elog(LOG, "remote_planner got called");

    if (!remote_planner_connected) {
        if (remote_planner_connect() == 0) {
            remote_planner_connected = true;
        }
    }

    parse_tree = nodeToString(parse);
    parse_tree_len = strlen(parse_tree);
    parse_tree_pretty = pretty_format_node_dump(parse_tree);

    elog(LOG, "remote_planner: Query-tree: %s", parse_tree_pretty);
    pfree(parse_tree_pretty);

    if (remote_planner_connected) {
        rv = remote_planner_send(parse_tree, parse_tree_len);
    }

    /* Done with our parse_tree data */
    pfree(parse_tree);

    if (rv != NULL)
        return rv;

    elog(LOG, "remote_planner: fallback");

    if (prev_planner_hook != NULL) {
        /* Call any earlier hooks */
        elog(LOG, "     calling prev planner-hook");
        return (prev_planner_hook)(parse, cursorOptions, boundParams);
    }

    elog(LOG, "     calling standard_planner");

    /* Call the standard planner */
    return standard_planner(parse, cursorOptions, boundParams);
}

static int
remote_planner_connect(void)
{
    struct in_addr addr;
    int port;

    const char *port_str;
    const char *addr_str;

    addr_str = GetConfigOption("remote_planner.addr", true, false);
    if (addr_str == NULL) {
        addr_str = REMOTE_PLANNER_ADDR_DEFAULT;
    }

    port_str = GetConfigOption("remote_planner.port", true, false);
    if (port_str == NULL) {
        port_str = REMOTE_PLANNER_PORT_DEFAULT;
    }
    port = atoi(port_str);

    if (port < 0 || port > 65535) {
        elog(WARNING, "remote_planner: invalid port %s", port_str);
        return -1;
    }

    if (!inet_aton(addr_str, &addr)) {
        elog(WARNING, "remote_planner: invalid address '%s'", addr_str);
        return -1;
    }

    elog(LOG, "remote_planner: using '%s:%d'", addr_str, port);

    /* tell caller we're done */

    return 0;
}

static PlannedStmt *
remote_planner_send(char *query_tree, uint32_t len)
{
    return NULL;
}
