// SPDX-License-Identifier: MIT
//
// multiwin: one program's several windows in wsys through winit (its plan9
// backend: wsys' clone, p9win::Window): the main window (the process's,
// red), a modal dialog of it (with_parent_window, with_modal: blue) and a
// popup of it without decorations (green), each with softbuffer's pixels
// read back from its own window file (/dev/wsys/n/window), their numbers in
// wsys, then the popup dropped: its directory gone, the others still there.
// Prints "ok ..." / "FAIL ..." lines and a summary.

use std::collections::HashMap;
use std::num::NonZeroU32;
use std::rc::Rc;
use std::time::{Duration, Instant};

use winit::application::ApplicationHandler;
use winit::dpi::{LogicalPosition, LogicalSize};
use winit::event::WindowEvent;
use winit::event_loop::{ActiveEventLoop, ControlFlow, EventLoop};
use winit::platform::plan9::{WindowAttributesExtPlan9, WindowExtPlan9};
use winit::raw_window_handle::HasWindowHandle;
use winit::window::{Window, WindowId};

mod common;
use common::{pixel_in, Report};

struct Win {
    w: Rc<Window>,
    surface: softbuffer::Surface<Rc<Window>, Rc<Window>>,
    colour: u32,
    drawn: bool,
}

#[derive(Default)]
struct App {
    r: Report,
    wins: HashMap<WindowId, Win>,
    order: Vec<WindowId>, // main, dialog, popup
    checked: bool,
    started: Option<Instant>,
}

impl App {
    fn make(&mut self, el: &ActiveEventLoop, attrs: winit::window::WindowAttributes, colour: u32, what: &str) -> bool {
        let w = match el.create_window(attrs) {
            Ok(w) => Rc::new(w),
            Err(e) => {
                self.r.fail(&format!("{what}: create_window: {e}"));
                return false;
            },
        };
        let ctx = softbuffer::Context::new(w.clone()).unwrap();
        let surface = match softbuffer::Surface::new(&ctx, w.clone()) {
            Ok(s) => s,
            Err(e) => {
                self.r.fail(&format!("{what}: softbuffer: {e}"));
                return false;
            },
        };
        self.r.ok(&format!("{what}: window {:?}, wsys' {}", w.id(), w.wsys_window()));
        self.order.push(w.id());
        self.wins.insert(w.id(), Win { w, surface, colour, drawn: false });
        true
    }

    fn draw(&mut self, id: WindowId) {
        let Some(win) = self.wins.get_mut(&id) else { return };
        let s = win.w.inner_size();
        let (Some(nw), Some(nh)) = (NonZeroU32::new(s.width), NonZeroU32::new(s.height)) else { return };
        if win.surface.resize(nw, nh).is_err() {
            return;
        }
        let Ok(mut buf) = win.surface.buffer_mut() else { return };
        buf.fill(win.colour);
        if buf.present().is_ok() {
            win.drawn = true;
        }
    }

    /// all drawn: each window's pixels in its own window file
    fn check(&mut self) {
        self.checked = true;
        let names = ["main", "dialog", "popup"];
        let mut nums = vec![];
        for (i, id) in self.order.clone().into_iter().enumerate() {
            let win = &self.wins[&id];
            let n = win.w.wsys_window();
            nums.push(n);
            let (file, border) = if n == 0 { ("/dev/window".to_string(), 4) } else { (format!("/dev/wsys/{n}/window"), if i == 2 { 0 } else { 4 }) };
            match pixel_in(&file, border, 10, 10) {
                Ok(p) if p == win.colour => self.r.ok(&format!("{}: its pixels in {file}: {p:06x}", names[i])),
                Ok(p) => self.r.fail(&format!("{}: {file} has {p:06x}, want {:06x}", names[i], win.colour)),
                Err(e) => self.r.fail(&format!("{}: {e}", names[i])),
            }
        }
        if nums[0] == 0 && nums[1] != 0 && nums[2] != 0 && nums[1] != nums[2] {
            self.r.ok("three windows: the process's and two of wsys' clone");
        } else {
            self.r.fail(&format!("window numbers {nums:?}"));
        }
        // the popup dropped: its window gone in wsys
        let p = self.order.pop().unwrap();
        self.wins.remove(&p);
        let dir = format!("/dev/wsys/{}", nums[2]);
        let t = Instant::now();
        while std::path::Path::new(&dir).exists() && t.elapsed() < Duration::from_secs(2) {
            std::thread::sleep(Duration::from_millis(50));
        }
        if !std::path::Path::new(&dir).exists() {
            self.r.ok("the popup dropped: its window gone");
        } else {
            self.r.fail(&format!("the popup dropped: {dir} still there"));
        }
        if std::path::Path::new(&format!("/dev/wsys/{}", nums[1])).exists() {
            self.r.ok("the dialog stays");
        } else {
            self.r.fail("the dialog went with the popup");
        }
    }
}

impl ApplicationHandler for App {
    fn resumed(&mut self, el: &ActiveEventLoop) {
        if !self.wins.is_empty() {
            return;
        }
        self.started = Some(Instant::now());
        let main = Window::default_attributes().with_title("multiwin main").with_inner_size(LogicalSize::new(300, 200));
        if !self.make(el, main, 0xff0000, "main") {
            el.exit();
            return;
        }
        let m = self.wins[&self.order[0]].w.clone();
        let raw = m.window_handle().unwrap().as_raw();
        let pos = m.inner_position().unwrap_or_default();
        let dialog = unsafe { Window::default_attributes().with_parent_window(Some(raw)) }
            .with_title("multiwin dialog")
            .with_modal(true)
            .with_inner_size(LogicalSize::new(160, 100))
            .with_position(LogicalPosition::new(pos.x + 60, pos.y + 40));
        let popup = unsafe { Window::default_attributes().with_parent_window(Some(raw)) }
            .with_decorations(false)
            .with_active(false)
            .with_inner_size(LogicalSize::new(80, 40))
            .with_position(LogicalPosition::new(pos.x + 200, pos.y + 150));
        if !self.make(el, dialog, 0x0000ff, "dialog") || !self.make(el, popup, 0x00ff00, "popup") {
            el.exit();
        }
    }

    fn window_event(&mut self, el: &ActiveEventLoop, id: WindowId, ev: WindowEvent) {
        match ev {
            WindowEvent::RedrawRequested => self.draw(id),
            WindowEvent::Resized(_) => {
                if let Some(w) = self.wins.get(&id) {
                    w.w.request_redraw();
                }
            },
            WindowEvent::CloseRequested => el.exit(),
            _ => {},
        }
    }

    fn about_to_wait(&mut self, el: &ActiveEventLoop) {
        if !self.checked && self.wins.len() == 3 && self.wins.values().all(|w| w.drawn) {
            // a moment for wsys to show them
            std::thread::sleep(Duration::from_millis(300));
            self.check();
            el.exit();
            return;
        }
        if self.started.is_some_and(|t| t.elapsed() > Duration::from_secs(20)) {
            self.r.fail("not all three windows drawn in 20 s");
            el.exit();
            return;
        }
        el.set_control_flow(ControlFlow::WaitUntil(Instant::now() + Duration::from_millis(100)));
    }
}

fn main() {
    let el = EventLoop::new().expect("event loop");
    let mut app = App::default();
    if let Err(e) = el.run_app(&mut app) {
        app.r.fail(&format!("run_app: {e}"));
    }
    std::process::exit(app.r.summary("multiwin"));
}
