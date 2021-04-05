use sky::{event::{EventLoop, EventLoopHandle}, net::{ TcpListener}};

fn main() {

    let mut event_loop = EventLoop::new();

    let tcp_server = TcpListener::new("0.0.0.0".to_string(), "8080".to_string());

    tcp_server.register(&mut event_loop);

    event_loop.run();
}