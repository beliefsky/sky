use crate::{event::{Event, EventCallBack, EventLoopHandle}};
use crate::os;

pub struct EventLoop {
    ep_fd: i32,
    now: u64
}

impl EventLoopHandle for EventLoop {
    fn new() -> Self {
        let ep_fd = unsafe {
            os::epoll_create1(os::EPOLL_CLOEXEC)
        };
        
        let now = time_now(); 

        return EventLoop {
            ep_fd,
            now
        };
    }

    fn run(&mut self) {
        println!("this fd is :{}", self.ep_fd);
        let mut events = [
            os::EpollEvent{flags: 0, u64:0},
            os::EpollEvent{flags: 0, u64:0},
            os::EpollEvent{flags: 0, u64:0},
            os::EpollEvent{flags: 0, u64:0},
            os::EpollEvent{flags: 0, u64:0},
            os::EpollEvent{flags: 0, u64:0}
        ];
        let event_ptr = events.as_mut_ptr();

        loop {
            let n = unsafe {os::epoll_wait(self.ep_fd, event_ptr, 6, 1000)};
            if n < 0 {
                continue;
            }
            self.now = time_now();

            for i in 0..n {
                let event = &events[i as usize];

                if (event.flags & (os::EPOLL_RDHUP | os::EPOLL_HUP)) != 0 {
                    println!("aaaaaaaaaaa");
                    continue;
                }
                println!("run: {}", n);
            }

            println!("loop :{}, ts: {}", n, self.now);
        }
    }

    fn register<T>(&mut self, event: &mut Event<T>) where T: EventCallBack {
        let mut  ev = os::EpollEvent{
            flags: os::EPOLL_IN | os::EPOLL_OUT | os::EPOLL_PRI | os::EPOLL_RDHUP | os::EPOLL_ERR | os::EPOLL_ET,
            u64: 0
        };

        event.reg = true;
        unsafe {
            os::epoll_ctl(self.ep_fd, os::EPOLL_CTL_ADD, event.fd, &mut ev);
        }
    }

    fn unregister<T>(&mut self, event: &mut Event<T>) where T: EventCallBack {
        if !event.reg {
            return;
        }
        unsafe {
            os::close(event.fd);
        }
        event.fd = -1;
    }
}

impl Drop for EventLoop {
    fn drop(&mut self) {
        if self.ep_fd == -1 {
            return;
        }

        unsafe {
            os::close(self.ep_fd);
        }
    }
}

fn time_now() -> u64 {
    return std::time::SystemTime::now()
    .duration_since(std::time::UNIX_EPOCH)
    .expect("Time went backwards")
    .as_secs();
}
