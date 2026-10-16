@schema
@pragma once
module nil.std.net

import nil.std.core

pub enum socket_domain
    af_inet
    af_inet6
end enum

pub enum socket_type
    sock_stream
    sock_dgram
end enum

pub struct socket
    handle: i64
    is_open: bool
end struct

@interop("ml_net")
pub fn ml_socket_create(domain: i32, type: i32) -> i64
end fn

@interop("ml_net")
pub fn ml_socket_bind(handle: i64, ip: text, port: i32) -> i32
end fn

@interop("ml_net")
pub fn ml_socket_listen(handle: i64, backlog: i32) -> i32
end fn

@interop("ml_net")
pub fn ml_socket_accept(handle: i64) -> i64
end fn

@interop("ml_net")
pub fn ml_socket_connect(handle: i64, ip: text, port: i32) -> i32
end fn

@interop("ml_net")
pub fn ml_socket_send(handle: i64, data: ptr<u8>, len: i32) -> i32
end fn

@interop("ml_net")
pub fn ml_socket_recv(handle: i64, buffer: ptr<u8>, len: i32) -> i32
end fn

@interop("ml_net")
pub fn ml_socket_close(handle: i64) -> i32
end fn

impl socket
    pub fn new(domain: socket_domain, type: socket_type) -> result<socket, text>
        let d: i32 = 0
        let t: i32 = 0
        match domain
            socket_domain.af_inet -> d = 2
            socket_domain.af_inet6 -> d = 23
            _ -> d = 2
        end match
        match type
            socket_type.sock_stream -> t = 1
            socket_type.sock_dgram -> t = 2
            _ -> t = 1
        end match
        let h = ml_socket_create(d, t)
        if h < 0 then
            return result.error("Failed to create socket")
        end if
        return result.ok(socket { handle: h, is_open: true })
    end fn

    pub fn bind(self, ip: text, port: i32) -> result<unit, text>
        let r = ml_socket_bind(self.handle, ip, port)
        if r < 0 then
            return result.error("Bind failed")
        end if
        return result.ok(())
    end fn

    pub fn listen(self, backlog: i32) -> result<unit, text>
        let r = ml_socket_listen(self.handle, backlog)
        if r < 0 then
            return result.error("Listen failed")
        end if
        return result.ok(())
    end fn

    pub fn accept(self) -> result<socket, text>
        let h = ml_socket_accept(self.handle)
        if h < 0 then
            return result.error("Accept failed")
        end if
        return result.ok(socket { handle: h, is_open: true })
    end fn

    pub fn connect(self, ip: text, port: i32) -> result<unit, text>
        let r = ml_socket_connect(self.handle, ip, port)
        if r < 0 then
            return result.error("Connect failed")
        end if
        return result.ok(())
    end fn

    pub fn send(self, data: ptr<u8>, len: i32) -> result<i32, text>
        let r = ml_socket_send(self.handle, data, len)
        if r < 0 then
            return result.error("Send failed")
        end if
        return result.ok(r)
    end fn

    pub fn recv(self, buffer: ptr<u8>, len: i32) -> result<i32, text>
        let r = ml_socket_recv(self.handle, buffer, len)
        if r < 0 then
            return result.error("Recv failed")
        end if
        return result.ok(r)
    end fn

    pub fn close(mut self) -> result<unit, text>
        if self.is_open then
            let r = ml_socket_close(self.handle)
            self.is_open = false
            if r < 0 then
                return result.error("Close failed")
            end if
        end if
        return result.ok(())
    end fn

    pub fn bind_primitive(self, ip: text, port: i32) -> i32
        return ml_socket_bind(self.handle, ip, port)
    end fn

    pub fn listen_primitive(self, backlog: i32) -> i32
        return ml_socket_listen(self.handle, backlog)
    end fn

    pub fn accept_primitive(self) -> i64
        return ml_socket_accept(self.handle)
    end fn

    pub fn connect_primitive(self, ip: text, port: i32) -> i32
        return ml_socket_connect(self.handle, ip, port)
    end fn
end impl

pub fn create_socket_primitive(domain: i32, type: i32) -> i64
    return ml_socket_create(domain, type)
end fn
