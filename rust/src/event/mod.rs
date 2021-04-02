#[cfg(target_os = "linux")]
pub use self::epoll_loop::EventLoop;
pub use self::event_loop::{Event, EventCallBack, EventLoopHandle};
#[cfg(target_os = "windows")]
pub use self::iocp_loop::EventLoop;

mod event_loop;

#[cfg(target_os = "linux")]
mod epoll_loop;

#[cfg(target_os = "windows")]
mod iocp_loop;

