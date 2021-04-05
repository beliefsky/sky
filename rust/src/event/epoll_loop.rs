use crate::{event::{Event, EventLoopHandle}};
use crate::os;

pub struct EventLoop {
    ep_fd: i32,
    now: u64,
    event_len: i32,
    event_layout: std::alloc::Layout,
    event_ptr: *mut os::EpollEvent
}

impl EventLoopHandle for EventLoop {
    fn new() -> Self {
        let ep_fd = unsafe { os::epoll_create1(os::EPOLL_CLOEXEC) };
        if -1 == ep_fd {
            panic!("create event loop error");
        }

        let now = time_now();

        let event_len: i32 = 1024; 
        let event_layout;
        let event_ptr;
        unsafe {
            event_layout = std::alloc::Layout::from_size_align_unchecked(
                event_len as usize * std::mem::size_of::<os::EpollEvent>(), 
                8
            );
            event_ptr = std::alloc::alloc(event_layout) as *mut os::EpollEvent;
        }


        return EventLoop { 
            ep_fd, 
            now,
            event_len,
            event_layout,
            event_ptr
        };
    }

    fn run(&mut self) {
        loop {
            let n = unsafe { os::epoll_wait(self.ep_fd, self.event_ptr, self.event_len, -1) as isize };
            if n < 0 {
                continue;
            }
            self.now = time_now();

            for i in 0..n {
                let epoll_event;
                let event;

                unsafe {
                    epoll_event = &mut *(self.event_ptr.offset(i));                    
                    event = &mut *(epoll_event.u64 as *mut Event);
                }

                if (epoll_event.flags & (os::EPOLL_RDHUP | os::EPOLL_HUP)) != 0 {
                    (event.close)(event);
                    continue;
                }
                if (epoll_event.flags & os::EPOLL_IN) != 0 {
                    event.read = true;
                }
                if (epoll_event.flags & os::EPOLL_OUT) != 0 {
                    event.write = true;
                }
                if event.wait {
                    continue;
                }

                if !(event.run)(event) {
                    (event.close)(event);
                }
                println!("run1: {}", 2);

                // unsafe {
                //     let event_layout = std::alloc::Layout::from_size_align_unchecked(
                //         std::mem::size_of::<Event>(), 
                //         8
                //     );
                //     std::ptr::drop_in_place(event as *mut Event);
                //     std::alloc::dealloc(event as *mut Event as *mut u8, event_layout)
                // }

            }
        }
    }

    fn register(&mut self, event: Event, timeout: u64) {
        let fd = event.fd;

        let event_layout;
        let event_ptr;
        unsafe {
            event_layout = std::alloc::Layout::from_size_align_unchecked(
                std::mem::size_of::<Event>(), 
                8
            );
            event_ptr = std::alloc::alloc(event_layout) as *mut Event;

            std::ptr::write(event_ptr, event);
        }
        let mut epoll_event = os::EpollEvent {
            flags: os::EPOLL_IN
                | os::EPOLL_OUT
                | os::EPOLL_PRI
                | os::EPOLL_RDHUP
                | os::EPOLL_ERR
                | os::EPOLL_ET,
            u64: event_ptr as u64
        };
    

        unsafe {
            os::epoll_ctl(self.ep_fd, os::EPOLL_CTL_ADD, fd, &mut epoll_event);
        }
    }

    fn unregister(&mut self, event: Event) {
        if !event.reg {
            return;
        }
        // event.close_fd();
    }
}

impl Drop for EventLoop {
    fn drop(&mut self) {
        unsafe {
            os::close(self.ep_fd);
            std::alloc::dealloc(self.event_ptr as *mut u8, self.event_layout);
        }
    }
}

fn time_now() -> u64 {
    return std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .expect("Time went backwards")
        .as_secs();
}
