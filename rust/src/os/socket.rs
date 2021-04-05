pub const AF_UNSPEC:i32 = 0;
pub const SOCK_STREAM:i32 = 1;
pub const SO_REUSEADDR:i32 = 2;
pub const SO_REUSEPORT:i32 = 15;

pub const AI_PASSIVE:i32 = 0x0001;
pub const SOCK_CLOEXEC:i32 = 524288;
pub const SOCK_NONBLOCK:i32 = 2048;


#[repr(C)]
pub struct Sockaddr {
    pub sa_family: u16,
    pub sa_data: [u8; 14],
}

#[repr(C)]
pub struct Addrinfo {
    pub ai_flags:i32,
    pub ai_family:i32,
    pub ai_socktype:i32,
    pub ai_protocol:i32,
    pub ai_addrlen:u32,
    pub ai_addr: *mut Sockaddr,
    pub ai_canonname: *mut u8,
    pub ai_next: *mut Addrinfo
}


extern "C" {
    pub fn socket(domain:i32, socke_type:i32, protocol:i32) -> i32;
    pub fn bind(fd:i32, addr:*const Sockaddr, addr_len:u32) -> i32;
    pub fn listen(fd:i32, n:i32) -> i32;
    pub fn accept4(fd:i32, addr:*const Sockaddr, addr_len:u32, flags: i32) -> i32;
    pub fn close(fd: i32);
    pub fn getaddrinfo(name: *const u8, service: *const u8, req: *const Addrinfo, pai: *mut *mut Addrinfo) ->i32;
    pub fn setsockopt(fd:i32, level:i32, optname:i32, optval: *const u8, optlen: u32) ->i32;
    pub fn freeaddrinfo(aiai: *mut Addrinfo);
}