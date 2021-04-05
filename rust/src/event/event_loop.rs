use crate::os;

pub trait EventLoopHandle {
    fn new() -> Self;
    fn run(&mut self);
    fn register(&mut self, event: Event, timeout: u64);
    fn unregister(&mut self, event: Event);
}

pub struct Event {
    fd: i32,
    pub reg: bool,
    pub wait: bool,
    pub read: bool,
    pub write: bool,
    pub run: fn(event: &mut Event) ->bool, 
    pub close: fn(event: &mut Event)
}

impl Event {
    pub fn new<T>(
        fd: i32,
        data: T, 
        run: fn(event: &mut Event) ->bool, 
        close: fn(event: &mut Event)
    ) ->Self {

        return Event {
            fd,
            reg: false,
            wait: false,
            read: true,
            write: true,
            run,
            close
        };
    }

    #[inline(always)]
    pub fn get_fd(&self) -> i32 {
        return self.fd;
    }

    pub fn close_fd(&mut self) {
        if -1 != self.fd {
            unsafe {
                os::close(self.fd);
            }

            self.fd = -1;
        }
    }
}

impl Drop for Event {
    fn drop(&mut self) {

        if -1 == self.fd {
            return;
        }

        unsafe {
            os::close(self.fd);
        }
    }
}

// impl<T> std::ops::Deref for Event<T> where T: EventCallBack {
//     type Target = T;
//     #[inline]
//     fn deref<'a>(&'a self) -> &'a T {
//         return &self.data;
//     }
// }

// impl<T> std::ops::DerefMut for Event<T> where T: EventCallBack {
//     #[inline]
//     fn deref_mut<'a>(&'a mut self) -> &'a mut T {
//         return &mut self.data;
//     }
// }
