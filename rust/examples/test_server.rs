use sky::event::{Event, EventCallBack, EventLoop, EventLoopHandle};

struct Test {
    a: u32
}

impl EventCallBack for Test {
    fn run(&self) -> bool {
        println!("run: {}", self.a);

        return true;
    }

    fn close(&self) {
        println!("close: {}", self.a);
    }
}


fn main() {
    let mut event_loop = EventLoop::new();

    println!("111111111111111111111111111");
    let mut test = Event::new(0, Test { a: 5 });
    println!("2222222222222222222222");
    event_loop.register(&mut test);
    // println!("33333333333333333333333");
    event_loop.run();
    println!("444444444444444444444444444");
}