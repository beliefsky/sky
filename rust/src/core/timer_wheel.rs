pub struct TimerWheel {
    num_wheels: u32,
    last_run: u64,
    max_ticks: u64,
}

trait Timeout {
    fn timeout(&self);
}

pub struct TimerWheelNode {
    entry: Slot,
    expire_at: u64,

}

struct Slot {
    // prev: Option<Slot>,
    // next: Option<Slot>,
}


impl TimerWheel {
    pub fn create(num_wheel: u32, now: u64) -> Self {
        return Self { num_wheels: 0, last_run: 0, max_ticks: 0 };
    }

    pub fn wake_at(self) -> u64 {
        return 0;
    }

    pub fn run(self, now: u64) {}
    pub fn expired(self) {}

    pub fn link(self) {}

    pub fn unlink(self) {}
}

impl Timeout for TimerWheelNode {
    fn timeout(&self) {}
}