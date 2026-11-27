// SPDX-License-Identifier: MIT
//
// The files of a ui (ui(4)): mounted from its /srv entry, in a namespace of
// the program's own (it does not change its shell's); ui started in a new
// window of rio or wsys (window(1)) first, as libui's uinew does (ui(2)).

use std::ffi::{CString, c_char, c_int, c_long, c_void};
use std::fs::File;
use std::io::{self, Read, Write};
use std::sync::{Arc, mpsc};
use std::time::{Duration, Instant};

use xilem_core::{MessageResult, View};

use crate::{Cx, Event, Node};

unsafe extern "C" {
    #[link_name = "__p9_mount"]
    fn p9mount(fd: c_int, afd: c_int, old: *const c_char, flag: c_int, aname: *const c_char) -> c_int;
    #[link_name = "__p9_unmount"]
    fn p9unmount(name: *const c_char, old: *const c_char) -> c_int;
    #[link_name = "__p9_rfork"]
    fn p9rfork(flags: c_int) -> c_int;
    #[link_name = "__p9_open"]
    fn p9open(file: *const c_char, mode: c_int) -> c_int;
    #[link_name = "__p9_close"]
    fn p9close(fd: c_int) -> c_int;
    #[link_name = "__p9_rerrstr"]
    fn p9rerrstr(buf: *mut c_char, n: u32);
    #[link_name = "__p9_create"]
    fn p9create(file: *const c_char, mode: c_int, perm: u32) -> c_int;
    #[link_name = "__p9_write"]
    fn p9write(fd: c_int, buf: *const c_void, n: c_long) -> c_long;
    #[link_name = "__p9_pipe"]
    fn p9pipe(fd: *mut c_int) -> c_int;
}

const OREAD: c_int = 0;
const OWRITE: c_int = 1;
const ORDWR: c_int = 2;
const OCEXEC: c_int = 32;
const ORCLOSE: c_int = 64;
const MREPL: c_int = 0;
const RFNAMEG: c_int = 1 << 0;

fn err(what: &str) -> io::Error {
    let mut b = [0u8; 256];
    unsafe { p9rerrstr(b.as_mut_ptr() as *mut c_char, b.len() as u32) };
    let n = b.iter().position(|&c| c == 0).unwrap_or(b.len());
    io::Error::other(format!("{what}: {}", String::from_utf8_lossy(&b[..n])))
}

fn cstr(s: &str) -> io::Result<CString> {
    CString::new(s).map_err(|_| io::Error::other("nul in name"))
}

/// The files of a ui: its ctl (transactions) and event (what the user did).
pub struct Ui {
    pub srv: String,
    pub mtpt: String,
    ctl: File,
    events: Option<Events>, // taken by run_on_with's reader
    life: Option<[c_int; 2]>, // new's: ui's lifeline (ui -c), its /srv entry and our end
}

/// ui's lifeline (ui -c): a pipe, one end posted as /srv/name (ORCLOSE: the
/// entry goes with this program), the other kept here; ui reads the posted
/// end and gets end of file when this program ends, however it ends (or the
/// Ui is dropped): it ends then too. Both close-on-exec: no program this one
/// starts keeps ui alive.
fn lifeline(name: &str) -> io::Result<[c_int; 2]> {
    let mut p = [0 as c_int; 2];
    if unsafe { p9pipe(p.as_mut_ptr()) } < 0 {
        return Err(err("pipe"));
    }
    let close = |fd: c_int| unsafe { p9close(fd) };
    let s = cstr(&format!("/srv/{name}"))?;
    let srv = unsafe { p9create(s.as_ptr(), OWRITE | ORCLOSE | OCEXEC, 0o600) };
    if srv < 0 {
        let e = err(&format!("/srv/{name}"));
        close(p[0]);
        close(p[1]);
        return Err(e);
    }
    let n = p[1].to_string();
    let w = unsafe { p9write(srv, n.as_ptr() as *const c_void, n.len() as c_long) };
    close(p[1]);
    if w != n.len() as c_long {
        let e = err(&format!("/srv/{name}"));
        close(srv);
        close(p[0]);
        return Err(e);
    }
    let d = cstr(&format!("#d/{}", p[0]))?;
    let end = unsafe { p9open(d.as_ptr(), OREAD | OCEXEC) };
    close(p[0]);
    if end < 0 {
        let e = err("the lifeline's end");
        close(srv);
        return Err(e);
    }
    Ok([srv, end])
}

/// a ui's event file, read a line at a time
struct Events {
    event: File,
    buf: Vec<u8>,
}

impl Events {
    fn next(&mut self) -> Option<String> {
        loop {
            if let Some(i) = self.buf.iter().position(|&c| c == b'\n') {
                let line: Vec<u8> = self.buf.drain(..=i).collect();
                return Some(String::from_utf8_lossy(&line[..i]).into_owned());
            }
            let mut b = [0u8; 8192];
            match self.event.read(&mut b) {
                Ok(0) | Err(_) => return None,
                Ok(n) => self.buf.extend_from_slice(&b[..n]),
            }
        }
    }
}

impl Ui {
    /// the ui posted as /srv/srv, its files on mtpt (the program's own
    /// namespace from here on)
    pub fn open(srv: &str, mtpt: &str) -> io::Result<Ui> {
        let s = cstr(&format!("/srv/{srv}"))?;
        let fd = unsafe { p9open(s.as_ptr(), ORDWR) };
        if fd < 0 {
            return Err(err(&format!("/srv/{srv}")));
        }
        let m = cstr(mtpt)?;
        let none = cstr("")?;
        // a mount that works closes fd (bind(2))
        let r = unsafe {
            if p9rfork(RFNAMEG) < 0 {
                -1
            } else {
                p9mount(fd, -1, m.as_ptr(), MREPL, none.as_ptr())
            }
        };
        if r < 0 {
            let e = err(&format!("mount {mtpt}"));
            unsafe { p9close(fd) };
            return Err(e);
        }
        let ctl = File::options().write(true).open(format!("{mtpt}/ctl"))?;
        let event = File::open(format!("{mtpt}/event"))?;
        Ok(Ui { srv: srv.into(), mtpt: mtpt.into(), ctl, events: Some(Events { event, buf: Vec::new() }), life: None })
    }

    /// ui in a new window, titled
    pub fn new(title: &str) -> io::Result<Ui> {
        let user = std::env::var("user").unwrap_or_else(|_| "none".into());
        let srv = format!("ui.{user}.{}.0", std::process::id());
        let life = lifeline(&format!("{srv}.life"))?;
        let gone = |e: io::Error| {
            for fd in life {
                unsafe { p9close(fd) };
            }
            e
        };
        // window(1) returns once the window is there; ui posts soon after
        std::process::Command::new("/bin/window").arg(format!("/bin/ui -c {srv}.life -s {srv}")).status().map_err(gone)?;
        let t = Instant::now();
        while std::fs::metadata(format!("/srv/{srv}")).is_err() {
            if t.elapsed() > Duration::from_secs(10) {
                return Err(gone(io::Error::other(format!("no /srv/{srv} from a new ui"))));
            }
            std::thread::sleep(Duration::from_millis(100));
        }
        let mut u = Ui::open(&srv, "/mnt/ui").map_err(gone)?;
        u.life = Some(life);
        u.ctl(&format!("title {}\n", crate::quote(title)))?;
        Ok(u)
    }

    /// files of the nodes the last transaction made: their pictures
    /// (nodes/id/image, shown when closed); one that fails is said, not fatal
    pub fn files(&self, files: Vec<(String, std::rc::Rc<[u8]>)>) {
        for (name, data) in files {
            let path = format!("{}/{name}", self.mtpt);
            let r = File::options().write(true).open(&path).and_then(|mut f| {
                for c in data.chunks(8192) {
                    f.write_all(c)?;
                }
                Ok(())
            });
            if let Err(e) = r {
                eprintln!("uiview: {path}: {e}");
            }
        }
    }

    /// a transaction: its lines all or none; framed by the lines begin and
    /// end (ui(4)), so that it stays one when the mount cuts the write
    pub fn ctl(&mut self, t: &str) -> io::Result<()> {
        if t.is_empty() {
            return Ok(());
        }
        let nl = if t.ends_with('\n') { "" } else { "\n" };
        let f = format!("begin\n{t}{nl}end\n");
        let n = self.ctl.write(f.as_bytes())?;
        if n != f.len() {
            return Err(io::Error::other("short write of ctl"));
        }
        Ok(())
    }

    /// the next line of event, waiting for it; None when ui is gone
    pub fn event(&mut self) -> Option<String> {
        self.events.as_mut()?.next()
    }
}

/// what comes to the program's loop: what the user did, or a change of the
/// state from elsewhere (a Proxy's)
enum Msg<S> {
    Event(String),
    Gone,
    Call(Box<dyn FnOnce(&mut S) + Send>),
}

/// Changes of the program's state from other threads (background work,
/// timers): each runs in the program's loop, and the views follow, as after an
/// event.
pub struct Proxy<S> {
    tx: mpsc::Sender<Msg<S>>,
}

impl<S> Clone for Proxy<S> {
    fn clone(&self) -> Self {
        Proxy { tx: self.tx.clone() }
    }
}

impl<S: 'static> Proxy<S> {
    /// f changes the state; false when the window is gone
    pub fn update(&self, f: impl FnOnce(&mut S) + Send + 'static) -> bool {
        self.tx.send(Msg::Call(Box::new(f))).is_ok()
    }

    /// f every period, from a thread of its own, while the window lasts
    pub fn every(&self, period: Duration, f: impl Fn(&mut S) + Send + Sync + 'static) {
        let p = self.clone();
        let f = Arc::new(f);
        std::thread::spawn(move || {
            loop {
                std::thread::sleep(period);
                let f = f.clone();
                if !p.update(move |s| f(s)) {
                    break;
                }
            }
        });
    }
}

impl Drop for Ui {
    fn drop(&mut self) {
        if let Ok(m) = cstr(&self.mtpt) {
            unsafe { p9unmount(std::ptr::null(), m.as_ptr()) };
        }
        // new's ui ends
        for fd in self.life.take().into_iter().flatten() {
            unsafe { p9close(fd) };
        }
    }
}

/// The program's window: ui in a new one, the views of app(state) in it,
/// until the window is closed.
pub fn run<S: 'static, V>(title: &str, state: S, app: impl FnMut(&mut S) -> V) -> io::Result<()>
where
    V: View<S, (), Cx, Element = Node>,
{
    run_on(Ui::new(title)?, state, app)
}

/// as run, in a ui's window already there
pub fn run_on<S: 'static, V>(ui: Ui, state: S, app: impl FnMut(&mut S) -> V) -> io::Result<()>
where
    V: View<S, (), Cx, Element = Node>,
{
    run_on_with(ui, state, |_| {}, app)
}

/// as run; start gets a Proxy first, for background work and timers
pub fn run_with<S: 'static, V>(
    title: &str,
    state: S,
    start: impl FnOnce(Proxy<S>),
    app: impl FnMut(&mut S) -> V,
) -> io::Result<()>
where
    V: View<S, (), Cx, Element = Node>,
{
    run_on_with(Ui::new(title)?, state, start, app)
}

/// as run_on, with a Proxy for start
pub fn run_on_with<S: 'static, V>(
    mut ui: Ui,
    mut state: S,
    start: impl FnOnce(Proxy<S>),
    mut app: impl FnMut(&mut S) -> V,
) -> io::Result<()>
where
    V: View<S, (), Cx, Element = Node>,
{
    let mut cx = Cx::new();
    let mut view = app(&mut state);
    let (mut root, mut vs) = view.build(&mut cx, &mut state);
    let t = cx.take();
    // the app's one view fills the window (root's children stretched across it)
    ui.ctl(&format!("clear\nset root align stretch\n{t}"))?;
    ui.files(cx.take_files());
    // the events on a thread of their own, into the loop with the Proxies' changes
    let (tx, rx) = mpsc::channel::<Msg<S>>();
    let mut events = ui.events.take().ok_or_else(|| io::Error::other("the ui's events are read already"))?;
    let etx = tx.clone();
    std::thread::spawn(move || {
        while let Some(line) = events.next() {
            if etx.send(Msg::Event(line)).is_err() {
                return;
            }
        }
        let _ = etx.send(Msg::Gone);
    });
    start(Proxy { tx });
    while let Ok(m) = rx.recv() {
        match m {
            Msg::Gone => break,
            Msg::Call(f) => f(&mut state),
            Msg::Event(line) => {
                if line == "close" {
                    break;
                }
                let Some(ev) = Event::parse(&line) else { continue };
                match cx.message(&view, &mut vs, &mut root, &mut state, ev) {
                    MessageResult::Stale | MessageResult::Nop => continue,
                    MessageResult::Action(()) | MessageResult::RequestRebuild => {}
                }
            }
        }
        let next = app(&mut state);
        cx.rebuild(&next, &view, &mut vs, &mut root, &mut state);
        view = next;
        ui.ctl(&cx.take())?;
        ui.files(cx.take_files());
    }
    Ok(())
}
