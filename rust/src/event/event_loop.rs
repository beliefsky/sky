
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
    fn register<T>(&mut self, fd: i32, event: T, timeout:u64) -> Event<T> where T: EventCallBack ;
    fn unregister<T>(&mut self, event: &mut Event<T>) where T: EventCallBack;
}