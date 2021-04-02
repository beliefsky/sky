pub const EPOLL_CLOEXEC: u32 = 0x80000;

pub const EPOLL_CTL_ADD: i32 = 1;
pub const EPOLL_CTL_DEL: i32 = 2;
pub const EPOLL_CTL_MOD: i32 = 3;

pub const EPOLL_IN: u32 = 0x01;
pub const EPOLL_PRI: u32 = 0x02;
pub const EPOLL_OUT: u32 = 0x04;
pub const EPOLL_ERR: u32 = 0x08;
pub const EPOLL_HUP: u32 = 0x10;
pub const EPOLL_RDHUP: u32 = 0x2000;
pub const EPOLL_ONE_SHOT: u32 = 0x40000000;
pub const EPOLL_ET: u32 = 0x80000000;


#[repr(C)]
pub struct EpollEvent {
    pub flags: u32,
    pub u64: u64,
}


extern "C" {
    pub fn epoll_create1(flags: u32) -> i32;
    pub fn epoll_wait(
        ep_fd: i32,
        events: *mut EpollEvent,
        max_events: i32,
        timeout: i32,
    ) -> i32;

    pub fn epoll_ctl(
        ep_fd: i32,
        op: i32,
        fd: i32,
        events: *mut EpollEvent,
    ) -> i32;
}