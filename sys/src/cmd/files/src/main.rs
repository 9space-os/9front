// SPDX-License-Identifier: MIT
//
// files: a file manager in a window of ui(4) (uiview). One directory at a
// time: its path in a text box (enter goes there), up and home; its
// directories first, then its files, each a button with its size. A click
// selects an entry, a second click on it, or open, opens it: a directory is
// gone into, a file plumbed (plumb(6): the editor, page, ... as the rules say).
// The selection can be renamed (a name in the same directory, as wstat
// does) or deleted (a directory only when empty), after a yes. A new
// directory by its name. The listing follows the directory: read again
// every 2 s, what other programs made or took away shows.

use std::io;
use std::path::{Path, PathBuf};
#[cfg(target_os = "plan9")]
use std::time::Duration;
use uiview::*;

#[derive(Clone, Debug, PartialEq)]
struct Entry {
    name: String,
    dir: bool,
    size: u64,
}

struct State {
    dir: PathBuf,
    path: String,            // the path box
    entries: Vec<Entry>,
    sel: Option<String>,     // the file selected
    newname: String,         // the rename box
    mkname: String,          // the new directory box
    confirm: bool,           // delete asked: yes or no
    status: String,          // what the last action did, or why it failed
}

/// the directory's entries: directories first, each part by name
fn load(dir: &Path) -> io::Result<Vec<Entry>> {
    let mut v = Vec::new();
    for e in std::fs::read_dir(dir)? {
        let e = e?;
        let m = e.metadata()?;
        v.push(Entry { name: e.file_name().to_string_lossy().into_owned(), dir: m.is_dir(), size: m.len() });
    }
    v.sort_by(|a, b| b.dir.cmp(&a.dir).then_with(|| a.name.cmp(&b.name)));
    Ok(v)
}

/// p made absolute against dir and cleaned as cleanname(2) does: no ., no
/// .. but at the root, no double slashes (there are no symbolic links)
fn clean(dir: &Path, p: &str) -> PathBuf {
    let full = if p.starts_with('/') { PathBuf::from(p) } else { dir.join(p) };
    let mut parts: Vec<String> = Vec::new();
    for c in full.to_string_lossy().split('/') {
        match c {
            "" | "." => {}
            ".." => {
                parts.pop();
            }
            c => parts.push(c.to_string()),
        }
    }
    PathBuf::from(format!("/{}", parts.join("/")))
}

/// a name in this directory: not empty, no slash, not . or ..
fn plainname(n: &str) -> Result<&str, String> {
    let n = n.trim();
    if n.is_empty() || n == "." || n == ".." || n.contains('/') {
        return Err(format!("{n:?}: a name in this directory, without /"));
    }
    Ok(n)
}

/// a plumb message (plumb(6)) from files, its directory the one shown
fn plumbmsg(wdir: &Path, data: &str) -> String {
    format!("files\n\n{}\ntext\n\n{}\n{data}", wdir.display(), data.len())
}

fn size(n: u64) -> String {
    match n {
        n if n < 10 * 1024 => format!("{n}"),
        n if n < 10 * 1024 * 1024 => format!("{}K", n / 1024),
        n => format!("{}M", n / (1024 * 1024)),
    }
}

impl State {
    fn new(dir: &str) -> State {
        let mut s = State {
            dir: PathBuf::from("/"),
            path: String::new(),
            entries: Vec::new(),
            sel: None,
            newname: String::new(),
            mkname: String::new(),
            confirm: false,
            status: String::new(),
        };
        let here = std::env::current_dir().unwrap_or_else(|_| PathBuf::from("/"));
        s.dir = here.clone();
        s.go(&clean(&here, dir).to_string_lossy());
        s
    }

    /// to the directory p (relative to the one shown); it stays where it
    /// is if p cannot be read
    fn go(&mut self, p: &str) {
        let d = clean(&self.dir, p);
        match load(&d) {
            Ok(v) => {
                self.dir = d;
                self.entries = v;
                self.unselect();
                self.status.clear();
            }
            Err(e) => self.status = format!("{}: {e}", d.display()),
        }
        self.path = self.dir.to_string_lossy().into_owned();
    }

    fn unselect(&mut self) {
        self.sel = None;
        self.newname.clear();
        self.confirm = false;
    }

    /// read again (the timer, after a change): the selection stays if its
    /// file does
    fn reload(&mut self) {
        match load(&self.dir) {
            Ok(v) => {
                if v != self.entries {
                    self.entries = v;
                    if let Some(n) = &self.sel {
                        if !self.entries.iter().any(|e| &e.name == n) {
                            self.unselect();
                        }
                    }
                }
            }
            Err(e) => self.status = format!("{}: {e}", self.dir.display()),
        }
    }

    /// a click on an entry's button: selects it, opens it when it is
    fn click(&mut self, name: &str) {
        let Some(e) = self.entries.iter().find(|e| e.name == name) else {
            return;
        };
        if self.sel.as_deref() == Some(name) {
            self.open();
            return;
        }
        self.status = if e.dir { format!("{name}/") } else { format!("{name}: {} bytes", e.size) };
        self.sel = Some(name.to_string());
        self.newname = name.to_string();
        self.confirm = false;
    }

    /// the selection opened: a directory gone into, a file plumbed
    fn open(&mut self) {
        let Some(n) = self.sel.clone() else { return };
        if self.dir.join(&n).is_dir() {
            self.go(&n);
            return;
        }
        let msg = plumbmsg(&self.dir, &n);
        let r = std::fs::OpenOptions::new().write(true).open("/mnt/plumb/send").and_then(|mut f| {
            use std::io::Write;
            f.write_all(msg.as_bytes())
        });
        self.status = match r {
            Ok(()) => format!("{n}: plumbed"),
            Err(e) => format!("plumb {n}: {e}"),
        };
    }

    fn rename(&mut self) {
        let Some(old) = self.sel.clone() else { return };
        let new = match plainname(&self.newname) {
            Ok(n) => n.to_string(),
            Err(e) => {
                self.status = e;
                return;
            }
        };
        if new == old {
            return;
        }
        match std::fs::rename(self.dir.join(&old), self.dir.join(&new)) {
            Ok(()) => {
                self.status = format!("{old}: now {new}");
                self.reload();
                self.sel = Some(new.clone());
                self.newname = new;
            }
            Err(e) => self.status = format!("rename {old}: {e}"),
        }
    }

    /// delete: the first time it asks (yes or no)
    fn delete(&mut self, yes: bool) {
        let Some(n) = self.sel.clone() else { return };
        if !self.confirm {
            self.confirm = true;
            return;
        }
        self.confirm = false;
        if !yes {
            return;
        }
        let p = self.dir.join(&n);
        let r = if p.is_dir() { std::fs::remove_dir(&p) } else { std::fs::remove_file(&p) };
        match r {
            Ok(()) => {
                self.status = format!("{n}: deleted");
                self.unselect();
                self.reload();
            }
            Err(e) => self.status = format!("delete {n}: {e}"),
        }
    }

    fn mkdir(&mut self) {
        let n = match plainname(&self.mkname) {
            Ok(n) => n.to_string(),
            Err(e) => {
                self.status = e;
                return;
            }
        };
        match std::fs::create_dir(self.dir.join(&n)) {
            Ok(()) => {
                self.status = format!("{n}/: made");
                self.mkname.clear();
                self.reload();
            }
            Err(e) => self.status = format!("mkdir {n}: {e}"),
        }
    }
}

fn entry(e: &Entry, sel: bool) -> impl UiView<State> + use<> {
    let n = e.name.clone();
    let slash = if e.dir { "/" } else { "" };
    let text = if sel { format!("» {}{slash}", e.name) } else { format!("{}{slash}", e.name) };
    row((
        button(text, move |s: &mut State| s.click(&n)).flex(1.0),
        sized(label(if e.dir { String::new() } else { size(e.size) })).width(64.0),
    ))
    .gap(8.0)
    .align("center")
}

fn selection(s: &State) -> Option<impl UiView<State> + use<>> {
    let n = s.sel.clone()?;
    let ask: Option<_> = s.confirm.then(|| {
        row((
            label(format!("delete {n}?")),
            button("yes", |s: &mut State| s.delete(true)),
            button("no", |s: &mut State| s.delete(false)),
        ))
        .gap(8.0)
        .align("center")
    });
    Some(column((
        row((
            button("open", |s: &mut State| s.open()),
            textbox(&s.newname, |s: &mut State, t| s.newname = t)
                .on_enter(|s: &mut State, t| {
                    s.newname = t;
                    s.rename();
                })
                .flex(1.0),
            button("rename", |s: &mut State| s.rename()),
            button("delete", |s: &mut State| s.delete(false)),
        ))
        .gap(8.0),
        ask,
    ))
    .gap(4.0))
}

fn app(s: &mut State) -> impl UiView<State> + use<> {
    let entries: Vec<_> = s.entries.iter().map(|e| entry(e, s.sel.as_deref() == Some(e.name.as_str()))).collect();
    column((
        row((
            button("up", |s: &mut State| s.go("..")),
            button("home", |s: &mut State| {
                let h = std::env::var("home").unwrap_or_else(|_| "/".into());
                s.go(&h)
            }),
            textbox(&s.path, |s: &mut State, t| s.path = t)
                .on_enter(|s: &mut State, t| s.go(&t))
                .flex(1.0),
        ))
        .gap(8.0),
        list(entries).gap(2.0).flex(1.0),
        selection(s),
        row((
            textbox(&s.mkname, |s: &mut State, t| s.mkname = t)
                .placeholder("new directory")
                .on_enter(|s: &mut State, t| {
                    s.mkname = t;
                    s.mkdir();
                })
                .flex(1.0),
            button("make", |s: &mut State| s.mkdir()),
        ))
        .gap(8.0),
        label(if s.status.is_empty() {
            format!("{} entries", s.entries.len())
        } else {
            format!("{} entries — {}", s.entries.len(), s.status)
        }),
    ))
    .gap(8.0)
    .flex(1.0) // the window's height: the list takes what the rest leaves
}

#[cfg(target_os = "plan9")]
fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let dir = match args.as_slice() {
        [] => ".".to_string(),
        [d] if !d.starts_with('-') => d.clone(),
        _ => {
            eprintln!("usage: files [dir]");
            std::process::exit(1);
        }
    };
    let st = State::new(&dir);
    let start = |p: Proxy<State>| p.every(Duration::from_secs(2), |s: &mut State| s.reload());
    if let Err(e) = uiview::run_with("files", st, start, app) {
        eprintln!("files: {e}");
        std::process::exit(1);
    }
}

/// elsewhere (the host's tests) there is no ui(4)
#[cfg(not(target_os = "plan9"))]
fn main() {
    eprintln!("files: runs on Plan 9, in a window of ui(4)");
    std::process::exit(1);
}

#[cfg(test)]
mod tests {
    use super::*;
    use uiview::xilem_core::View;

    fn tmp(name: &str) -> PathBuf {
        let d = std::env::temp_dir().join(format!("files-test-{name}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&d);
        std::fs::create_dir_all(d.join("sub")).unwrap();
        std::fs::write(d.join("b.txt"), "bb").unwrap();
        std::fs::write(d.join("a.txt"), "a").unwrap();
        d
    }

    #[test]
    fn cleaned() {
        let d = Path::new("/usr/glenda");
        assert_eq!(clean(d, ".."), PathBuf::from("/usr"));
        assert_eq!(clean(d, "../../.."), PathBuf::from("/"));
        assert_eq!(clean(d, "lib//./x/"), PathBuf::from("/usr/glenda/lib/x"));
        assert_eq!(clean(d, "/tmp/../sys"), PathBuf::from("/sys"));
        assert!(plainname("a/b").is_err() && plainname("..").is_err() && plainname(" ").is_err());
        assert_eq!(plainname(" x "), Ok("x"));
        assert_eq!(plumbmsg(Path::new("/tmp"), "notes"), "files\n\n/tmp\ntext\n\n5\nnotes");
        assert_eq!((size(512), size(20 * 1024), size(30 << 20)), ("512".into(), "20K".into(), "30M".into()));
    }

    #[test]
    fn browse_select_rename_delete() {
        let d = tmp("ops");
        let mut s = State::new(&d.to_string_lossy());
        let names: Vec<_> = s.entries.iter().map(|e| e.name.as_str()).collect();
        assert_eq!(names, ["sub", "a.txt", "b.txt"], "directories first, by name");
        assert_eq!(s.path, d.to_string_lossy());

        // a file: selected, its name in the rename box
        s.click("b.txt");
        assert_eq!(s.sel.as_deref(), Some("b.txt"));
        assert_eq!(s.newname, "b.txt");
        s.newname = "c.txt".into();
        s.rename();
        assert!(d.join("c.txt").exists() && !d.join("b.txt").exists());
        assert_eq!(s.sel.as_deref(), Some("c.txt"));
        s.newname = "../x".into();
        s.rename();
        assert!(s.status.contains("without /"), "{}", s.status);

        // delete asks first; no keeps it, yes deletes
        s.delete(false);
        assert!(s.confirm);
        s.delete(false);
        assert!(!s.confirm && d.join("c.txt").exists());
        s.delete(false);
        s.delete(true);
        assert!(!d.join("c.txt").exists());
        assert_eq!(s.sel, None);

        // a new directory, selected and renamed; a directory not empty is not deleted
        s.mkname = "new".into();
        s.mkdir();
        assert!(d.join("new").is_dir());
        s.click("new");
        s.newname = "newer".into();
        s.rename();
        assert!(d.join("newer").is_dir() && s.dir == d, "selected, not gone into");
        std::fs::write(d.join("sub/in"), "").unwrap();
        s.click("sub");
        s.delete(false);
        s.delete(true);
        assert!(d.join("sub").exists() && s.status.starts_with("delete sub"), "{}", s.status);

        // a second click goes into a directory, up again; a path that is none keeps the place
        s.click("a.txt");
        s.click("sub");
        assert_eq!((s.dir.as_path(), s.sel.as_deref()), (d.as_path(), Some("sub")));
        s.click("sub");
        assert_eq!(s.dir, d.join("sub"));
        assert_eq!(s.entries.len(), 1);
        s.go("..");
        assert_eq!(s.dir, d);
        s.go("nowhere");
        assert_eq!(s.dir, d);
        assert!(s.status.contains("nowhere"));

        // changed from outside: the timer's reload shows it
        std::fs::write(d.join("z"), "").unwrap();
        s.reload();
        assert!(s.entries.iter().any(|e| e.name == "z"));
        std::fs::remove_dir_all(&d).unwrap();
    }

    #[test]
    fn views() {
        let d = tmp("views");
        let mut s = State::new(&d.to_string_lossy());
        let mut cx = Cx::new();
        let v = app(&mut s);
        let (mut root, mut vs) = v.build(&mut cx, &mut s);
        let out = cx.take();
        for t in ["text sub/\n", "text a.txt\n", "text b.txt\n", "text 2\n", "text 3 entries\n"] {
            assert!(out.contains(t), "{t:?} not in {out}");
        }
        assert!(!out.contains("text open\n"), "no selection, no open: {out}");
        // the button of b.txt: a click selects it
        let id = out
            .lines()
            .filter_map(|l| l.strip_prefix("set ").and_then(|l| l.strip_suffix(" text b.txt")))
            .next()
            .unwrap_or_else(|| panic!("no b.txt in {out}"))
            .to_string();
        cx.message(&v, &mut vs, &mut root, &mut s, Event::parse(&format!("click {id}")).unwrap());
        assert_eq!(s.sel.as_deref(), Some("b.txt"));
        let next = app(&mut s);
        cx.rebuild(&next, &v, &mut vs, &mut root, &mut s);
        let out = cx.take();
        assert!(out.contains(&format!("set {id} text » b.txt\n")) && out.contains("text open\n"), "{out}");
        std::fs::remove_dir_all(&d).unwrap();
    }
}
