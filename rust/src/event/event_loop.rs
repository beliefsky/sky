
pub struct Event<T> {
    pub reg: bool,
    pub wait: bool,
    pub read: bool,
    pub write: bool,
    pub data: T
}

pub trait EventCallBack {
    fn get_fd(&self) ->i32;
    fn run(&self) -> bool;
    fn close(&self);
}

pub trait EventLoopHandle {
    fn new() -> Self;
    fn run(&mut self);
    fn register<T>(&mut self, data: T, timeout:u64) -> Event<T> where T: EventCallBack ;
    fn unregister<T>(&mut self, event: &mut Event<T>) where T: EventCallBack;
}