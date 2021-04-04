pub trait EventCallBack where Self: Sized {
    fn get_fd(&self) -> i32;
    fn close_fd(&self);
    fn run(&self, event: &Event<Self>) -> bool;
    fn close(&self, event: &Event<Self>);
}

pub trait EventLoopHandle {
    fn new() -> Self;
    fn run(&mut self);
    fn register<T>(&mut self, data: T, timeout: u64) -> Event<T> where T: EventCallBack;
    fn unregister<T>(&mut self, event: &mut Event<T>) where T: EventCallBack;
}

pub struct Event<T> where T: EventCallBack {
    pub reg: bool,
    pub wait: bool,
    pub read: bool,
    pub write: bool,
    data: T,
}

impl <T> Event<T>  where T: EventCallBack {
    pub fn new(reg: bool, data: T) ->Self {
        return Event {
            reg,
            wait: false,
            read: true,
            write: true,
            data
        };
    }
}

impl<T> std::ops::Deref for Event<T> where T: EventCallBack {
    type Target = T;
    #[inline]
    fn deref<'a>(&'a self) -> &'a T {
        return &self.data;
    }
}

impl<T> std::ops::DerefMut for Event<T> where T: EventCallBack {
    #[inline]
    fn deref_mut<'a>(&'a mut self) -> &'a mut T {
        return &mut self.data;
    }
}
