// SPDX-License-Identifier: MIT
//
// wsys(4)'s helpers kept going: what a supervised program is (Sup), and
// what its end means (Sup::ended). Starting it is wsys' (main.rs: runwin,
// runhelper, again).

use std::collections::VecDeque;

/// a program wsys keeps going: a window's (window -restart) or a helper's
/// without a window (wctl helper)
#[derive(Clone, Copy, PartialEq, Eq, Hash, Debug)]
pub enum Job {
    Win(u32),
    Helper(u32),
}

/// how a supervised program went: the window manager starts its helpers
/// again (wsys(4) "Helpers"). Killed by the pager for memory: again after a
/// pause, 1 s doubling to 30 while it keeps going within a minute. Another
/// end with a status (a crash): again, but said each time, and after 5 in 10
/// minutes not any more (wctl restart, menu 3 start it by hand). An end of
/// its own (no status) or by the user's note (hangup, interrupt, kill): done.
pub struct Sup {
    pub restart: bool,
    pub cmd: String,
    pub cd: Option<String>,
    pub pid: Option<u32>,
    pub started: std::time::Instant,
    pub running: bool,
    pub wait: u64,
    pub crashes: VecDeque<std::time::Instant>,
    pub gaveup: bool,
}

pub enum Next {
    Again(u64, String),
    GaveUp(String),
    Done,
}

const SUPMAXWAIT: u64 = 30;
const SUPCRASHES: usize = 5;
const SUPCRASHSPAN: std::time::Duration = std::time::Duration::from_secs(600);
const SUPSTEADY: std::time::Duration = std::time::Duration::from_secs(60);

impl Sup {
    pub fn new(restart: bool, cmd: String, cd: Option<String>) -> Sup {
        Sup {
            restart,
            cmd,
            cd,
            pid: None,
            started: std::time::Instant::now(),
            running: false,
            wait: 0,
            crashes: VecDeque::new(),
            gaveup: false,
        }
    }

    /// it ended with wait message msg (what: its name in a line)
    pub fn ended(&mut self, what: &str, msg: &str) -> Next {
        self.running = false;
        self.pid = None;
        let ran = self.started.elapsed();
        let note = msg.rsplit(": ").next().unwrap_or("");
        if !self.restart || msg.is_empty() || matches!(note, "hangup" | "interrupt" | "kill" | "killed") {
            return Next::Done;
        }
        self.wait = if ran < SUPSTEADY { (self.wait * 2).clamp(1, SUPMAXWAIT) } else { 1 };
        if msg.contains("Insufficient physical memory") {
            return Next::Again(self.wait, format!("wsys: {what}: killed for memory: again in {} s", self.wait));
        }
        let now = std::time::Instant::now();
        self.crashes.retain(|t| now.duration_since(*t) < SUPCRASHSPAN);
        self.crashes.push_back(now);
        if self.crashes.len() >= SUPCRASHES {
            self.gaveup = true;
            return Next::GaveUp(format!(
                "wsys: {what}: {msg}: {} times in 10 minutes: not started again (wctl restart or menu 3)",
                self.crashes.len()
            ));
        }
        Next::Again(self.wait, format!("wsys: {what}: {msg}: again in {} s", self.wait))
    }

    /// started by hand (wctl restart, menu 3): its count of crashes anew
    pub fn anew(&mut self) {
        self.crashes.clear();
        self.gaveup = false;
        self.wait = 0;
    }
}
