// SPDX-License-Identifier: MIT
//
// uiwidgets: every widget of ui(4) in a window, from Rust (uiview: views of
// the program's state, as Xilem makes them); the same scene as rcwidgets
// (rc), gowidgets (Go) and cxxwidgets (C++). A switch makes the desktop
// dark or light (the theme's mode, /dev/theme); a dialog, a second window
// and a popup menu are windows of wsys (ui's window nodes, in the tree where
// they belong); what the user does shows in the status line. Prints
// "ready srv" once its scene is there, then an event a line.
// Written for 9space; uiview builds on xilem_core (a crate: Copyright 2022
// the Xilem Authors, Apache-2.0). The image is 9front's face, read at run
// time from /lib/face (9front's, MIT: /lib/legal/mit).

use std::io::Write;
use std::rc::Rc;
use uiview::*;

const SVG: &str = r##"<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64"><circle cx="32" cy="32" r="30" fill="#7a5a28"/><circle cx="32" cy="32" r="18" fill="#eeee9e"/><circle cx="32" cy="32" r="6" fill="#4a6628"/></svg>"##;

const PROSE: &str = "Prose: text to select and copy, not to edit; button 3 plumbs it. It sits in a scroll, which moves it when it is longer than the room it has: the wheel, or the bar at its side. Plan 9 takes text as the stuff of work: what is shown can be selected, sent and plumbed, and the window is a file.";

const SECTIONS: [&str; 4] = ["Controls", "Text", "Lists", "Layout"];
const COLOURS: [&str; 4] = ["Umber", "Ochre", "Sand", "Moss"];

struct State {
    section: usize,
    dark: bool,
    check: bool,
    switch: bool,
    radio: usize,
    slider: f64,
    stepper: f64,
    choice: usize,
    text: String,
    lines: String,
    weight: f64,
    page: usize,
    dialog: bool,
    second: bool,
    popup: Option<(f64, f64)>,
    mtpt: String,
    status: String,
    svg: Rc<[u8]>,
    face: Rc<[u8]>,
}

impl State {
    fn say(&mut self, s: impl Into<String>) {
        self.status = s.into();
        println!("event {}", self.status);
    }

    fn set_dark(&mut self, on: bool) {
        self.dark = on;
        // one write: wsys takes a theme's lines as they come
        if let Ok(mut f) = std::fs::File::options().write(true).open("/dev/theme") {
            let _ = f.write_all(format!("mode {}\n", if on { "dark" } else { "light" }).as_bytes());
        }
        self.say(format!("dark {}", on as u8));
    }

    // under the menu button: ui's window on the screen and the button in it
    fn menu_at(&self) -> Option<(f64, f64)> {
        let nums = |f: &str| -> Vec<f64> {
            std::fs::read_to_string(format!("{}/{f}", self.mtpt)).unwrap_or_default().split_whitespace().filter_map(|x| x.parse().ok()).collect()
        };
        let (o, r) = (nums("ctl"), nums("nodes/bmenu/rect"));
        (o.len() >= 2 && r.len() >= 4).then(|| (o[0] + r[0], o[1] + r[3]))
    }
}

fn controls(s: &mut State) -> impl UiView<State> + use<> {
    column((
        row((
            button("Press me", |s: &mut State| s.say("clicked press")),
            button("Dialog…", |s: &mut State| {
                s.dialog = true;
                s.say("clicked bdlg");
            })
            .id("bdlg"),
            button("Window…", |s: &mut State| {
                s.second = true;
                s.say("clicked bwin");
            })
            .id("bwin"),
            button("Menu…", |s: &mut State| {
                s.popup = s.menu_at();
                s.say("clicked bmenu");
            })
            .id("bmenu"),
        ))
        .gap(8.0),
        row((
            checkbox("Check me", s.check, |s: &mut State, c| {
                s.check = c;
                s.say(format!("ck {}", c as u8));
            }),
            switch(s.switch, |s: &mut State, c| {
                s.switch = c;
                s.say(format!("sw {}", c as u8));
            }),
            label("a switch"),
        ))
        .gap(16.0)
        .align("center"),
        radiogroup(
            row((
                radio("Umber", s.radio == 0, |s: &mut State| s.radio = 0),
                radio("Ochre", s.radio == 1, |s: &mut State| s.radio = 1),
                radio("Sand", s.radio == 2, |s: &mut State| s.radio = 2),
            ))
            .gap(16.0),
        ),
        row((
            label("Slider"),
            slider(0.0, 1.0, s.slider, |s: &mut State, v| {
                s.slider = v;
                s.say(format!("sl {v}"));
            })
            .flex(1.0),
            progress(Some(s.slider)).flex(1.0),
            spinner(),
        ))
        .gap(12.0)
        .align("center"),
        row((
            label("Stepper"),
            stepper(0.0, 10.0, s.stepper, |s: &mut State, v| {
                s.stepper = v;
                s.say(format!("stp {v}"));
            }),
            select(&COLOURS, s.choice, |s: &mut State, i| {
                s.choice = i;
                s.say(format!("sel {i}"));
            }),
            badged("3", button("Inbox", |s: &mut State| s.say("clicked inbox"))),
            badge_count(12),
        ))
        .gap(12.0)
        .align("center"),
    ))
    .gap(12.0)
}

fn text(s: &mut State) -> impl UiView<State> + use<> {
    let echo = if s.text.is_empty() { "(what you type)".to_string() } else { s.text.clone() };
    column((
        textbox(&s.text, |s: &mut State, t| s.text = t).placeholder("type here").id("tb"),
        label(echo).id("echo"),
        textarea(&s.lines, |s: &mut State, t| s.lines = t).placeholder("lines of text: Enter makes a new one").flex(1.0),
        scroll(prose(PROSE)).flex(1.0),
        row((
            varlabel("weight", s.weight).prop("animate", 200),
            slider(100.0, 900.0, s.weight, |s: &mut State, v| s.weight = v).flex(1.0),
        ))
        .gap(12.0)
        .align("center"),
    ))
    .gap(12.0)
}

fn lists(s: &mut State) -> impl UiView<State> + use<> {
    let cells = ["Colour", "Hex", "Use", "Umber", "#6b5a3a", "border", "Ochre", "#7a5a28", "accent"];
    let p = s.page;
    let rows: Vec<_> = (1..=20).map(|i| label(format!("row {}", p * 20 + i))).collect();
    column((
        grid(3, cells.iter().map(|c| label(*c)).collect::<Vec<_>>()),
        collapse("More", true, label("folded away until asked")),
        pages(5, s.page, |s: &mut State, p| {
            s.page = p;
            s.say(format!("page {p}"));
        }),
        list(rows).flex(1.0),
    ))
    .gap(12.0)
}

fn layout(s: &mut State) -> impl UiView<State> + use<> {
    split(
        0.35,
        (
            label("left of the split (drag the bar)"),
            column((
                align("center", label("centred")),
                divider(false),
                zstack((
                    sized(()).width(160.0).height(48.0).prop("background", "#d8cfb2").prop("radius", 8),
                    label("over a box"),
                ))
                .align("center"),
                row((svg(s.svg.clone()), image(s.face.clone()))).gap(12.0).align("center"),
            ))
            .gap(8.0),
        ),
    )
    .prop("draggable", 1)
    .flex(1.0)
}

fn dialog(s: &mut State) -> Option<impl UiView<State> + use<>> {
    s.dialog.then(|| {
        window(
            "dialog",
            "A dialog",
            column((
                label("Discard the changes?"),
                row((
                    button("Cancel", |s: &mut State| {
                        s.dialog = false;
                        s.say("clicked dcancel");
                    }),
                    button("Discard", |s: &mut State| {
                        s.dialog = false;
                        s.say("clicked dok");
                    })
                    .id("dok"),
                ))
                .gap(8.0),
            ))
            .prop("padding", 16)
            .gap(12.0),
        )
        .id("dlg")
        .prop("modal", 1)
        .width(320.0)
        .height(120.0)
        .on_close(|s: &mut State| {
            s.dialog = false;
            s.say("closed dlg");
        })
    })
}

fn second(s: &mut State) -> Option<impl UiView<State> + use<>> {
    s.second.then(|| {
        window(
            "window",
            "A second window",
            column((
                label("A window of its own"),
                button("Close", |s: &mut State| {
                    s.second = false;
                    s.say("clicked w2close");
                })
                .id("w2close"),
            ))
            .prop("padding", 16)
            .gap(12.0),
        )
        .id("w2")
        .width(300.0)
        .height(160.0)
        .on_close(|s: &mut State| s.second = false)
    })
}

fn popup(s: &mut State) -> Option<impl UiView<State> + use<>> {
    let (x, y) = s.popup?;
    let item = |c: &'static str| {
        button(c, move |s: &mut State| {
            s.popup = None;
            s.choice = COLOURS.iter().position(|k| *k == c).unwrap_or(0);
            s.say(format!("clicked m{c}"));
        })
        .id(format!("m{c}"))
    };
    Some(
        window("popup", "menu", column((item("Umber"), item("Ochre"), item("Sand"))).prop("padding", 4).gap(2.0))
            .id("pop")
            .prop("x", x)
            .prop("y", y)
            .width(140.0)
            .height(108.0)
            .on_close(|s: &mut State| s.popup = None),
    )
}

fn app(s: &mut State) -> impl UiView<State> + use<> {
    let sections: Vec<_> = SECTIONS
        .iter()
        .enumerate()
        .map(|(i, n)| {
            button(*n, move |s: &mut State| {
                s.section = i;
                s.say(format!("clicked s{i}"));
            })
            .id(format!("s{i}"))
        })
        .collect();
    column((
        row((
            varlabel("9space widgets", 700.0),
            badge("Rust"),
            label(" ").flex(1.0),
            label("dark"),
            switch(s.dark, |s: &mut State, on| s.set_dark(on)).id("dark"),
        ))
        .gap(12.0)
        .align("center"),
        row(sections).gap(8.0),
        divider(false),
        stack(s.section, (controls(s), text(s), lists(s), layout(s))).id("st").flex(1.0),
        label(s.status.clone()),
        dialog(s),
        second(s),
        popup(s),
    ))
    .prop("padding", 16)
    .gap(12.0)
    // its rows as wide as it (uiview stretches it across the window): the
    // head's spacer takes the room; as high as the window
    .align("stretch")
    .flex(1.0)
}

fn main() {
    let mut ui = match Ui::new("widgets: Rust") {
        Ok(u) => u,
        Err(e) => {
            eprintln!("uiwidgets: {e}");
            std::process::exit(1);
        }
    };
    let srv = ui.srv.clone();
    let face: Rc<[u8]> = std::fs::read("/lib/face/48x48x8/n/9front.1").unwrap_or_default().into();
    let st = State {
        section: 0,
        dark: std::fs::read_to_string("/dev/theme").is_ok_and(|t| t.lines().any(|l| l == "mode dark")),
        check: false,
        switch: false,
        radio: 0,
        slider: 0.3,
        stepper: 3.0,
        choice: 0,
        text: String::new(),
        lines: String::new(),
        weight: 400.0,
        page: 0,
        dialog: false,
        second: false,
        popup: None,
        mtpt: ui.mtpt.clone(),
        status: "ready".into(),
        svg: SVG.as_bytes().into(),
        face,
    };
    // its window as big as its widest section needs (logical pixels: ui
    // does not fit the window to the contents)
    if let Err(e) = ui.ctl("set root width 980\nset root height 560") {
        eprintln!("uiwidgets: {e}");
    }
    // ready once the scene is there
    if let Err(e) = uiview::run_on_with(ui, st, move |_| println!("ready {srv}"), app) {
        eprintln!("uiwidgets: {e}");
        std::process::exit(1);
    }
}
