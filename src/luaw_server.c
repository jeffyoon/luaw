/*
* Copyright (c) 2015 raksoras
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <unistd.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "uv.h"
#include "luaw_common.h"
#include "luaw_logging.h"
#include "luaw_buffer.h"
#include "luaw_tcp.h"
#include "lfs.h"

static char* server_ip = "0.0.0.0";
static int server_port = 80;
static uv_tcp_t server;
static uv_loop_t* event_loop;
static uv_signal_t shutdown_signal;

static int start_req_thread_fn_ref;
static int run_ready_threads_fn_ref;

static void handle_shutdown_req(uv_signal_t* handle, int signum) {
    if (signum == SIGHUP) {
        fprintf(stderr, "shutdown request received\n");
        uv_signal_stop(handle);
        uv_stop(event_loop);
    }
}

void init_luaw_server(lua_State* L) {
    lua_getglobal(L, "luaw_scheduler");
    if (!lua_istable(L, -1)) {
        fprintf(stderr, "Luaw scheduler not initialized\n");
        exit(EXIT_FAILURE);
    }

    lua_getfield(L, -1, "resumeThreadId");
    if (lua_isfunction(L, -1)) {
        resume_thread_fn_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    } else {
        fprintf(stderr, "resumeThreadId function not found in luaw scheduler\n");
        exit(EXIT_FAILURE);
    }

    lua_getfield(L, -1, "startRequestThread");
    if (lua_isfunction(L, -1)) {
        start_req_thread_fn_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    } else {
        fprintf(stderr, "startRequestThread function not found in luaw scheduler\n");
        exit(EXIT_FAILURE);
    }

    lua_getfield(L, -1, "runReadyThreads");
    if (lua_isfunction(L, -1)) {
        run_ready_threads_fn_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    } else {
        fprintf(stderr, "runReadyThreads function not found in luaw scheduler\n");
        exit(EXIT_FAILURE);
    }
    lua_pop(L, 1);

    lua_getglobal(L, "luaw_server_config");
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "server_ip");
        if (lua_isstring(L, -1)) {
            server_ip = (char *)lua_tostring(L, -1);
            lua_pop(L, 1);
        }

        lua_getfield(L, -1, "server_port");
        if (lua_isnumber(L, -1)) {
            server_port = lua_tointeger(L, -1);
            lua_pop(L, 1);
        }

        lua_pop(L, 1);  //pop luaw_server_config object
    }

    event_loop = uv_default_loop();
}

/* create a new lua coroutine to service this conn, anchor it in "all active coroutines"
*  global table to prevent it from being garbage collected and start the coroutine
*/
LIBUV_CALLBACK static void on_server_connect(uv_stream_t* server, int status) {
    if (status) {
        raise_lua_error(l_global, "Error in on_server_connect callback: %s\n", uv_strerror(status));
        return;
    }

    lua_rawgeti(l_global, LUA_REGISTRYINDEX, start_req_thread_fn_ref);
    assert(lua_isfunction(l_global, -1));

    connection_t * conn = new_connection();
    if (conn == NULL) {
        raise_lua_error(l_global, "Could not allocate memory for client connection");
        return;
    }
    lua_pushlightuserdata(l_global, conn);
    
    status = uv_accept(server, (uv_stream_t*)&conn->handle);
    if (status) {
        close_connection(conn, status);
        fprintf(stderr, "Error accepting incoming conn: %s\n", uv_strerror(status));
        return;
    }

    status = lua_pcall(l_global, 1, 2, 0);
    if (status) {
        fprintf(stderr, "**** Error starting new client connect thread: %s (%d) ****\n", lua_tostring(l_global, -1), status);
    }
}

void start_server(lua_State *L) {
    fprintf(stderr, "starting server on port %d ...\n", server_port);

    struct sockaddr_in addr;
    int err_code = uv_ip4_addr(server_ip, server_port, &addr);
    if (err_code) {
        fprintf(stderr, "Error initializing socket address: %s\n", uv_strerror(err_code));
        exit(EXIT_FAILURE);
    }

    uv_tcp_init(event_loop, &server);

    err_code = uv_tcp_bind(&server, (const struct sockaddr*) &addr, 0);
    if (err_code) {
        fprintf(stderr, "Error binding to port %d : %s\n", server_port, uv_strerror(err_code));
        exit(EXIT_FAILURE);
    }

    err_code = uv_listen((uv_stream_t*)&server, 128, on_server_connect);
    if (err_code) {
        fprintf(stderr, "Error listening on port %d : %s\n", server_port, uv_strerror(err_code));
        exit(EXIT_FAILURE);
    }

    uv_signal_init(event_loop, &shutdown_signal);
    uv_signal_start(&shutdown_signal, handle_shutdown_req, SIGHUP);
}

static void run_user_threads() {
    /* do the bottom half processing, run ready user threads that are waiting */
    lua_rawgeti(l_global, LUA_REGISTRYINDEX, run_ready_threads_fn_ref);
    int status = lua_pcall(l_global, 0, 1, 0);
    if (status != LUA_OK) {
        fprintf(stderr,"Error running user threads for bottom half processing: %s\n", lua_tostring(l_global, -1));
        uv_stop(event_loop);
    }
    lua_settop(l_global, 0);
}

static void close_walk_cb(uv_handle_t* handle, void* arg) {
	if (!uv_is_closing(handle)) {
		uv_close(handle, NULL);
	}
}

static int server_loop(lua_State *L) {
    int status = 1;
    while (status) {
        status = uv_run(event_loop, UV_RUN_ONCE);
        run_user_threads();
    }
    
    /* clean up resources used by the event loop and Lua */
    uv_walk(event_loop, close_walk_cb, NULL);
    uv_run(event_loop, UV_RUN_ONCE);
    uv_loop_delete(event_loop);
    lua_close(L);
	close_syslog();

    return status;
}

static void run_lua_file(const char* filename) {
    int status = luaL_dofile(l_global, filename);
    if (status != LUA_OK) {
        fprintf(stderr, "Error while ruunning script: %s\n", filename);
        fprintf(stderr, "%s\n", lua_tostring(l_global, -1));
        exit(EXIT_FAILURE);
    }
}

static void set_lua_path(lua_State* L) {
    lua_getglobal( L, "package" );
    lua_pushliteral(L, "?;?.lua;./bin/?;./bin/?.lua;./lib/?;./lib/?.lua");
    lua_setfield( L, -2, "path" );
    lua_pop(L, 1);
}

int main (int argc, char* argv[]) {
	if (argc < 2) {
		fprintf(stderr, "Usage: %s <luaw config file >\n", argv[0]);
		exit(EXIT_FAILURE);
	}

    l_global = luaL_newstate();
	if (!l_global) {
		fprintf(stderr, "Could not create new Lua state\n");
		exit(EXIT_FAILURE);
	}

    lua_gc(l_global, LUA_GCSTOP, 0);  /* stop collector during initialization */
    luaL_openlibs(l_global);  /* open libraries */
    luaw_init_libs(l_global);
    luaopen_lfs(l_global);
    lua_gc(l_global, LUA_GCRESTART, 0);

    set_lua_path(l_global);
    /* run lua on startup scripts passed on the command line */
    for (int i = 1; i < argc; i++) {
        fprintf(stderr, "## Running %s \n", argv[i]);
        run_lua_file(argv[i]);
    }

    init_luaw_server(l_global);
    start_server(l_global);
    int status = server_loop(l_global);

    exit(status);
}
