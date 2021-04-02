

pub struct HttpServer {
    pub a: i32
}

impl HttpServer {
    pub fn create() -> Self {
        return HttpServer { a: 1 };
    }

    pub fn print(&self) {
        println!("this value: {}", self.a);
    }
}