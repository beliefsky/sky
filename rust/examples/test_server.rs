use sky::{event::{Event, EventCallBack, EventLoop, EventLoopHandle}, net::{ TcpListener}};


fn main() {
    let mut event_loop = EventLoop::new();

    let tcp_server = TcpListener::new("0.0.0.0".to_string(), "8080".to_string());

    println!("create fd :{}", tcp_server.get_fd());

   let event = event_loop.register(tcp_server, 60);

   println!("register after");

    event_loop.run();
}