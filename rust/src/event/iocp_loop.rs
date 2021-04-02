use crate::{event::{Event, EventCallBack, EventLoopHandle}};
use crate::os;

pub struct EventLoop {
    now: u64
}

impl EventLoopHandle for EventLoop {
    fn new() -> Self {
        return EventLoop { now: 0 };
    }

    fn run(&mut self) {}

    fn register<T>(&mut self, event: &mut Event<T>) where T: EventCallBack {}

    fn unregister<T>(&mut self, event: &mut Event<T>) where T: EventCallBack {}
}