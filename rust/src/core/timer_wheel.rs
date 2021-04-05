use std::ptr::{NonNull, null_mut};

pub trait Timeout where Self: Sized {
    fn timeout(&self, timer: &Timer<Self>);
}

pub struct Timer<T> where  {
    expire_at: u64,
    data: T,
    prev: Box<Option<Self>>
}

impl <T> Timer<T> where T: Timeout {
    pub fn new(expire_at: u64, data: T) -> Self {

        return Timer{expire_at, data, prev: Box::new(Option::None)};
    }
}

impl<T> std::ops::Deref for Timer<T> where T: Timeout {
    type Target = T;
    #[inline]
    fn deref<'a>(&'a self) -> &'a T {
        return &self.data;
    }
}

impl<T> std::ops::DerefMut for Timer<T> where T: Timeout {
    #[inline]
    fn deref_mut<'a>(&'a mut self) -> &'a mut T {
        return &mut self.data;
    }
}

pub struct TimerWheel {
    num_wheels: u32,
    last_run: u64,
    max_ticks: u64,
}

impl TimerWheel {
    pub fn new(num_wheel: u32, now: u64) -> Self {
        return Self { num_wheels: 0, last_run: 0, max_ticks: 0 };
    }

    pub fn wake_at(&self) -> u64 {
        return 0;
    }

    pub fn run(&self, now: u64) {}
    pub fn expired(&self) {}

    pub fn link<T>(&self, data: T) -> Timer<T> where T: Timeout {
        return Timer::new(0, data);
    }

    pub fn unlink(&self) {



    }
}