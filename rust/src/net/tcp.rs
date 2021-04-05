use std::ptr::{null_mut, null};

use crate::{event::{Event, EventLoop, EventLoopHandle}, os};

pub struct TcpListener {
    pub fd: i32
}

impl TcpListener {
    pub fn new(host: String, port: String) -> Self {
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

        let opt = unsafe {
            os::getaddrinfo(host.as_ptr(), port.as_ptr(), &hints, &mut addrs as *mut *mut os::Addrinfo)
        };

        if -1 == opt || addrs.is_null() {
            panic!("getaddrinfo error, opt = {}, addr is null: {}", opt, addrs.is_null());
        }
        let fd = unsafe {create_socket(addrs)};

        unsafe {
            os::freeaddrinfo(addrs);
        }
        if -1 == fd {
            panic!("create socket error");
        }

        return TcpListener {
            fd
        };
    }

    pub fn register(self, ev_loop: &mut EventLoop) {
        let fd = self.fd;

        let event = Event::new(fd, self, listener_accept, listener_error);

        ev_loop.register(event, 60);

    }
}

impl Drop for TcpListener {
    fn drop(&mut self) {
        println!("drop listener: {}", self.fd);
        unsafe  {
            // os::close(self.fd);
        }
    }
}

unsafe fn create_socket(addr: *mut os::Addrinfo) ->i32 {
    let fd = os::socket(
        (*addr).ai_family, 
        (*addr).ai_socktype | os::SOCK_NONBLOCK | os::SOCK_CLOEXEC,
         (*addr).ai_protocol
    );
    if -1 == fd {
        return -1;
    }

    let opt:i32 = 1;

    let ptr = opt as *const u8;

    os::setsockopt(fd, os::SOCK_STREAM, os::SO_REUSEADDR, ptr, 4);
    os::setsockopt(fd, os::SOCK_STREAM, os::SO_REUSEPORT, ptr, 4);

    if 0 != os::bind(fd, (*addr).ai_addr, (*addr).ai_addrlen) {
        os::close(fd);
        return -1;
    }
    if 0 != os::listen(fd, 128) {
        os::close(fd);
        return -1;
    }

    return fd;
}

fn listener_accept(event: &mut Event) -> bool {
    let listener = event.get_fd();

    loop {
        let fd = unsafe {os::accept4(listener,null(), 0,os::SOCK_NONBLOCK | os::SOCK_CLOEXEC)};
        if fd == -1 {
            break;
        }

        unsafe {
            println!("close fd: {}", fd);
            os::close(fd);
        }
    }

    return true;
}

fn listener_error(event: &mut Event) {

}