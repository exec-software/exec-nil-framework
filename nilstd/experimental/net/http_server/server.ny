@pragma once
# Experimental: the ml_nyhttp provider is not linked into the SDK.
module nil.std.net

import nil.std.core
import nil.std.net.http_server.request
import nil.std.net.http_server.response
import nil.std.net.http_server.context
import nil.std.net.http_server.router
import nil.std.net.http_server.middleware

# =============================================================================
# HTTP Server
# =============================================================================

pub struct server_config
    pub host: text
    pub port: i32
    pub backlog: i32
    pub poll_timeout_ms: i32
    pub thread_pool_size: i32
end struct

impl server_config
    pub fn default() -> server_config
        return server_config {
            host: "0.0.0.0",
            port: 8080,
            backlog: 128,
            poll_timeout_ms: 100,
            thread_pool_size: 8
        }
    end fn

    pub fn on(port: i32) -> server_config
        let mut c = server_config.default()
        c.port = port
        return c
    end fn
end impl

pub struct http_server
    pub config: server_config
    pub router: http_router
    pub middleware: middleware_pipeline
    pub server_handle: i64
    pub is_running: bool
end struct

impl http_server
    pub fn new(router: http_router) -> http_server
        return http_server {
            config: server_config.default(),
            router: router,
            middleware: middleware_pipeline.new(),
            server_handle: -1,
            is_running: false
        }
    end fn

    # Convenience constructor that accepts an explicit server_config
    pub fn with_config(router: http_router, cfg: server_config) -> http_server
        return http_server {
            config: cfg,
            router: router,
            middleware: middleware_pipeline.new(),
            server_handle: -1,
            is_running: false
        }
    end fn

    pub fn listen(mut self)
        let handle = ml_http_server_create(
            self.config.host,
            self.config.port,
            self.config.backlog
        )
        if handle < 0 then return end if

        self.server_handle = handle
        if not ml_http_server_start(handle) then
            ml_http_server_free(handle)
            return
        end if

        self.is_running = true
        while self.is_running
            let req_handle = ml_http_next_request(handle, self.config.poll_timeout_ms)
            if req_handle < 0 then continue end if

            let remote = ml_http_request_remote_addr(req_handle)
            let req_result = assemble_request(req_handle, remote)

            match req_result
                Result.Error(msg) ->
                    let err_resp = http_server_response.bad_request(msg)
                    err_resp.send(req_handle)
                Result.Ok(req) ->
                    self.dispatch(req_handle, req)
            end match
        end while

        ml_http_server_stop(handle)
        ml_http_server_free(handle)
        self.server_handle = -1
    end fn

    pub fn dispatch(mut self, req_handle: i64, mut req: http_server_request)
        let match_opt = self.router.resolve(req.method, req.path)
        match match_opt
            Option.None ->
                http_server_response.not_found().send(req_handle)
            Option.Some(rm) ->
                let mut ctx = http_context.new(req)
                let resp = rm.invoke(ctx)
                resp.send(req_handle)
        end match
    end fn

    pub fn stop(mut self)
        self.is_running = false
    end fn
end impl

@interop("ml_nyhttp")
pub fn ml_http_server_create(host: text, port: i32, backlog: i32) -> i64
end fn

@interop("ml_nyhttp")
pub fn ml_http_server_start(handle: i64) -> bool
end fn

@interop("ml_nyhttp")
pub fn ml_http_next_request(handle: i64, timeout_ms: i32) -> i64
end fn

@interop("ml_nyhttp")
pub fn ml_http_server_stop(handle: i64)
end fn

@interop("ml_nyhttp")
pub fn ml_http_server_free(handle: i64)
end fn

@interop("ml_nyhttp")
pub fn ml_http_server_create_tls(host: text, port: i32, backlog: i32, cert_path: text, key_path: text) -> i64
end fn

@interop("ml_nyhttp")
pub fn ml_http_server_error() -> text
end fn

@interop("ml_nyhttp")
pub fn ml_ws_broadcast(server_handle: i64, project_key: text, payload: text)
end fn

@interop("ml_nyhttp")
pub fn ml_ws_next_message(server_handle: i64, timeout_ms: i32) -> i64
end fn

@interop("ml_nyhttp")
pub fn ml_ws_message_body(msg_handle: i64) -> text
end fn

@interop("ml_nyhttp")
pub fn ml_ws_message_project_key(msg_handle: i64) -> text
end fn

@interop("ml_nyhttp")
pub fn ml_ws_message_free(msg_handle: i64)
end fn

@interop("ml_nyhttp")
pub fn ml_http_request_remote_addr(req_handle: i64) -> text
end fn
