// SPDX-License-Identifier: MIT
//
// the desktop's theme (../9front-ui PLAN stage 7): its colours and fonts as text,
// a line "name value" each (one starting with # a comment), as /lib/theme/* and the file
// theme hold them (wsys(4)); colours #rrggbb. What a line does not name stays.
// classic is wsys' own (rio's colours); ui and term read the same file.
// scale (../9front-ui PLAN stage 8): the desktop's factor, 1 = 96 dpi;
// "scale auto" lets wsys find it (the monitor's EDID), "scale auto 1.5" is
// what it found, "scale 1.5" a factor given. Programs size what they draw
// by it: scalesize(n) = (n·dpi + 48)/96, dpi = round(96·scale), the same
// formula libdraw's scalesize(2) has (/dev/dpi).
// Light and dark (9space): a line "dark.name #rrggbb" sets a colour of the
// dark palette ("name" one of the light), "mode light|dark" chooses; the
// colour fields are those of the palette chosen. A dark palette starts as
// the light one: what it does not name stays the light colour.

use std::fmt::Write;

#[derive(Clone, Debug, PartialEq)]
pub struct Theme {
    pub back: u32,           // the screen behind the windows
    pub border: u32,         // the current window's border
    pub borderinactive: u32, // the other windows' borders
    pub window: u32,         // a text window's and a menu's background
    pub text: u32,           // their text
    pub select: u32,         // a selection's background
    pub menuhigh: u32,       // a menu's item under the mouse
    pub scroll: u32,         // a text window's scroll bar
    pub hold: u32,           // text and border while holding (Esc), current window
    pub holdinactive: u32,   // the border of a window holding, not current
    pub accent: u32,         // what is on or chosen: a default button, a switch, the focus (ui)
    pub surface: u32,        // a control's or a group's face, a text field's inside (ui)
    pub secondary: u32,      // text that matters less: hints, a menu's shortcuts (ui)
    pub separator: u32,      // lines between things (ui)
    pub danger: u32,         // what destroys (ui)
    pub success: u32,
    pub warning: u32,
    pub dark: bool,          // mode dark: the colours above are the dark palette's
    pub light: Colours,      // the palettes as the file has them
    pub darkcolours: Option<Colours>,
    pub title: bool,         // windows with a title bar (stage 7, step 3)
    pub font: String,        // wsys' font (font(6)); empty: -f, $font or vga's
    pub uifont: String,      // ui's font (TrueType); empty: its own choice
    pub monofont: String,    // term's (TrueType); empty: Go Mono
    pub scale: f64,          // the factor (1: 96 dpi)
    pub scaleauto: bool,     // wsys finds it (scale above: what it found)
}

impl Default for Theme {
    fn default() -> Theme {
        Theme::classic()
    }
}

/// a theme's colours, one palette (light or dark): the names its file has
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Colours(pub [u32; NCOLOURS]);

pub const NCOLOURS: usize = 17;
pub const COLOURS: [&str; NCOLOURS] = [
    "back", "border", "borderinactive", "window", "text", "select", "menuhigh", "scroll", "hold", "holdinactive",
    "accent", "surface", "secondary", "separator", "danger", "success", "warning",
];

impl Colours {
    pub fn get(&self, name: &str) -> Option<u32> {
        COLOURS.iter().position(|n| *n == name).map(|i| self.0[i])
    }
}

/// a colour: #rrggbb (or rrggbb)
fn colour(v: &str) -> Option<u32> {
    let h = v.strip_prefix('#').unwrap_or(v);
    (h.len() == 6).then(|| u32::from_str_radix(h, 16).ok()).flatten()
}

impl Theme {
    /// rio's colours, as wsys had them
    pub fn classic() -> Theme {
        let mut t = Theme {
            back: 0x777777,
            border: 0x335588,
            borderinactive: 0xAAAAAA,
            window: 0xFFFFFF,
            text: 0x000000,
            select: 0xFFFFAA,
            menuhigh: 0x9EEEEE,
            scroll: 0x999999,
            hold: 0x000099,
            holdinactive: 0x4993DD,
            accent: 0x335588,
            surface: 0xFFFFFF,
            secondary: 0x555555,
            separator: 0xCCCCCC,
            danger: 0xBB2222,
            success: 0x227722,
            warning: 0x995500,
            dark: false,
            light: Colours([0; NCOLOURS]),
            darkcolours: None,
            title: false,
            font: String::new(),
            uifont: String::new(),
            monofont: String::new(),
            scale: 1.0,
            scaleauto: true,
        };
        t.light = t.fields();
        t
    }

    /// the colour fields as a palette
    fn fields(&self) -> Colours {
        Colours([
            self.back, self.border, self.borderinactive, self.window, self.text, self.select, self.menuhigh, self.scroll,
            self.hold, self.holdinactive, self.accent, self.surface, self.secondary, self.separator, self.danger,
            self.success, self.warning,
        ])
    }

    /// the colour fields from the palette the mode chooses
    fn choose(&mut self) {
        let c = if self.dark { self.darkcolours.unwrap_or(self.light) } else { self.light };
        let v = c.0;
        (self.back, self.border, self.borderinactive, self.window, self.text) = (v[0], v[1], v[2], v[3], v[4]);
        (self.select, self.menuhigh, self.scroll, self.hold, self.holdinactive) = (v[5], v[6], v[7], v[8], v[9]);
        (self.accent, self.surface, self.secondary, self.separator) = (v[10], v[11], v[12], v[13]);
        (self.danger, self.success, self.warning) = (v[14], v[15], v[16]);
    }

    /// the dots per inch the factor makes: round(96·scale)
    pub fn dpi(&self) -> i32 {
        dpi(self.scale)
    }

    /// n pixels at 96 dpi, at this theme's (libdraw's scalesize)
    pub fn scalesize(&self, n: i32) -> i32 {
        scalesize(self.dpi(), n)
    }

    /// the lines of text applied to this theme; the first line it does not know
    /// is an error, and then nothing is changed
    pub fn apply(&mut self, text: &str) -> Result<(), String> {
        let mut t = self.clone();
        for (i, line) in text.lines().enumerate() {
            // a line that starts with # is a comment (a colour's # is not: "border #335588")
            let mut f = line.split_whitespace();
            let Some(name) = f.next() else { continue };
            if name.starts_with('#') {
                continue;
            }
            let value = f.collect::<Vec<_>>().join(" ");
            let bad = || format!("theme: line {}: {name} {value}", i + 1);
            let c = |v: &str| colour(v).ok_or_else(bad);
            if let Some(i) = COLOURS.iter().position(|n| *n == name) {
                t.light.0[i] = c(&value)?;
                continue;
            }
            if let Some(i) = name.strip_prefix("dark.").and_then(|d| COLOURS.iter().position(|n| *n == d)) {
                let v = c(&value)?;
                t.darkcolours.get_or_insert(t.light).0[i] = v;
                continue;
            }
            match name {
                "mode" => {
                    t.dark = match value.as_str() {
                        "dark" => true,
                        "light" => false,
                        _ => return Err(bad()),
                    }
                }
                "title" => t.title = matches!(value.as_str(), "1" | "on" | "yes"),
                "font" => t.font = value,
                "uifont" => t.uifont = value,
                "monofont" => t.monofont = value,
                "scale" => {
                    let mut f = value.split_whitespace();
                    let num = |v: &str| v.parse::<f64>().ok().filter(|s| (0.5..=8.0).contains(s));
                    match (f.next(), f.next(), f.next()) {
                        (Some("auto"), None, None) => t.scaleauto = true,
                        (Some("auto"), Some(v), None) => (t.scaleauto, t.scale) = (true, num(v).ok_or_else(bad)?),
                        (Some(v), None, None) => (t.scaleauto, t.scale) = (false, num(v).ok_or_else(bad)?),
                        _ => return Err(bad()),
                    }
                }
                _ => return Err(format!("theme: line {}: unknown {name}", i + 1)),
            }
        }
        t.choose();
        *self = t;
        Ok(())
    }

    /// the theme as its file has it
    pub fn text(&self) -> String {
        let mut s = String::new();
        for (n, v) in COLOURS.iter().zip(self.light.0) {
            let _ = writeln!(s, "{n} #{v:06x}");
        }
        if let Some(d) = &self.darkcolours {
            for ((n, v), l) in COLOURS.iter().zip(d.0).zip(self.light.0) {
                if v != l {
                    let _ = writeln!(s, "dark.{n} #{v:06x}");
                }
            }
        }
        let _ = writeln!(s, "mode {}", if self.dark { "dark" } else { "light" });
        let _ = writeln!(s, "title {}", self.title as u8);
        let _ = writeln!(s, "scale {}{}", if self.scaleauto { "auto " } else { "" }, self.scale);
        for (n, v) in [("font", &self.font), ("uifont", &self.uifont), ("monofont", &self.monofont)] {
            if !v.is_empty() {
                let _ = writeln!(s, "{n} {v}");
            }
        }
        s
    }
}

/// the dots per inch of a factor: round(96·scale)
pub fn dpi(scale: f64) -> i32 {
    (96.0 * scale).round() as i32
}

/// n pixels at 96 dpi at dpi (libdraw's scalesize(2): rounded)
pub fn scalesize(dpi: i32, n: i32) -> i32 {
    if dpi <= 0 { n } else { (n * dpi + 48) / 96 }
}

/// the factor for a screen of px pixels across mm millimetres (an EDID's):
/// 1 below 120 dpi, else dpi/96 down to a quarter
pub fn autoscale(px: i32, mm: i32) -> f64 {
    if px <= 0 || mm <= 0 {
        return 1.0;
    }
    let dpi = px as f64 * 25.4 / mm as f64;
    if dpi < 120.0 { 1.0 } else { ((dpi / 96.0 * 4.0).floor() / 4.0).max(1.0) }
}

/// the theme of the window system the program runs in (its /dev/theme: wsys');
/// classic without one (rio)
pub fn current() -> Theme {
    let mut t = Theme::classic();
    if let Ok(s) = std::fs::read_to_string("/dev/theme") {
        let _ = t.apply(&s);
    }
    t
}
