use std::ptr::null_mut;

use crate::os;
use crate::os::Addrinfo;

pub struct TCPListenerConf {
    host: String,
    port: String,
    timeout: i32,
    accept_callback: fn(),
}

pub struct TcpListener {}

impl TcpListener {
    fn new(conf: &TCPListenerConf) -> Self {
        let hints = os::Addrinfo {
            ai_flags: os::AI_PASSIVE,
            ai_family: os::AF_UNSPEC,
            ai_socktype: os::SOCK_STREAM,
            ai_protocol: 0,
            ai_addrlen: 0,
            ai_addr: null_mut(),
            ai_canonname: null_mut(),
            ai_next: null_mut(),
        };
        let mut addrs: *mut os::Addrinfo = null_mut();

        unsafe {
            os::getaddrinfo(conf.host.as_ptr(), conf.port.as_ptr(), &hints, &mut addrs as *mut *mut Addrinfo);
        }
        conf.host.as_ptr();


        return TcpListener {};
    }
}