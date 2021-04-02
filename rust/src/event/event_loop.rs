
pub struct Event<T> {
    pub fd: i32,
    pub reg: bool,
    pub wait: bool,
    pub read: bool,
    pub write: bool,
    pub data: T
}

pub trait EventCallBack {
    fn run(&self) -> bool;
    fn close(&self);
}

pub trait EventLoopHandle {
    fn new() -> Self;
    fn run(&mut self);
    fn register<T>(&mut self, event: &mut Event<T>) where T: EventCallBack;
    fn unregister<T>(&mut self, event: &mut Event<T>) where T: EventCallBack;
}

impl<T> Event<T> {
    pub fn new(fd: i32, data: T) -> Self where T: EventCallBack {
        return Event { 
            fd, 
            reg: false, 
            wait: false,
            read: false,
            write: false, 
            data
        };
    }
}