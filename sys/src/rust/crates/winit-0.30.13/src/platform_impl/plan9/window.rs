//! 9front: the windows of winit's plan9 backend. The first is the process's
//! window of wsys or rio (the screen without either); under wsys more are
//! made through its clone file (p9win::Window): windows of their own,
//! dialogs of a parent window, popups without decorations. Size, place and
//! hiding go to the window's wctl (rio(4)); the window system's answer comes
//! back as a change the readers see (its mouse's r, a line of its wctl).

use std::collections::VecDeque;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};

use p9win::cursor::{self, Shape};
use p9win::draw::{Draw, Rect};
use p9win::{Inputs, Role};

use crate::cursor::Cursor;
use crate::dpi::{PhysicalPosition, PhysicalSize, Position, Size};
use crate::platform_impl::Fullscreen;
use crate::window::{CursorGrabMode, CursorIcon, ImePurpose};
use crate::{error, window};

use super::event_loop::{CursorShape, Shared, WinState};
use super::{ActiveEventLoop, MonitorHandle, OsError, WindowId};

pub struct Window {
    shared: Arc<Shared>,
    st: Arc<WinState>,
}

fn os(e: std::io::Error) -> error::OsError {
    os_error!(OsError::new(e))
}

fn wctl_in(w: &p9win::Window, msg: &str) {
    if let Err(e) = w.wctl(msg) {
        tracing::warn!("plan9: wctl {msg}: {e}");
    }
}

/// the shapes drawn in p9win for winit's cursor icons (the default: the arrow)
fn shape(icon: CursorIcon) -> Option<Shape> {
    Some(match icon {
        CursorIcon::Text | CursorIcon::VerticalText => Shape::Text,
        CursorIcon::Wait | CursorIcon::Progress => Shape::Wait,
        CursorIcon::Crosshair | CursorIcon::Cell => Shape::Crosshair,
        CursorIcon::Pointer | CursorIcon::Grab | CursorIcon::Grabbing => Shape::Pointer,
        CursorIcon::Move | CursorIcon::AllScroll | CursorIcon::AllResize => Shape::Move,
        CursorIcon::NotAllowed | CursorIcon::NoDrop => Shape::NotAllowed,
        CursorIcon::EResize | CursorIcon::WResize | CursorIcon::EwResize | CursorIcon::ColResize => {
            Shape::EwResize
        },
        CursorIcon::NResize | CursorIcon::SResize | CursorIcon::NsResize | CursorIcon::RowResize => {
            Shape::NsResize
        },
        CursorIcon::NeResize | CursorIcon::SwResize | CursorIcon::NeswResize => Shape::NeswResize,
        CursorIcon::NwResize | CursorIcon::SeResize | CursorIcon::NwseResize => Shape::NwseResize,
        _ => return None,
    })
}

impl Window {
    pub(crate) fn new(
        el: &ActiveEventLoop,
        attrs: window::WindowAttributes,
    ) -> Result<Self, error::OsError> {
        let shared = el.shared.clone();
        // the first window is the process's; after it, windows of wsys' clone
        // file (rio has none: there a program has its one window)
        let first = shared.wins.lock().unwrap().is_empty();
        let win = if first {
            p9win::Window::process()
        } else {
            let parent = match attrs.parent_window() {
                Some(rwh_06::RawWindowHandle::Plan9(h)) => Some(p9win::Window::of(h.window)),
                _ => None,
            };
            let role = if !attrs.decorations {
                // a menu takes the focus (active), a tooltip leaves it
                Role::Popup { grab: attrs.active }
            } else if parent.is_some() {
                Role::Dialog { modal: attrs.platform_specific.modal }
            } else {
                Role::Toplevel
            };
            // a popup or dialog of the process's window when no parent is named
            let parent = parent.or_else(|| (role != Role::Toplevel).then(p9win::Window::process));
            p9win::Window::create(parent.as_ref(), role).map_err(|e| {
                os(std::io::Error::other(format!("plan9: another window (wsys' clone): {e}")))
            })?
        };
        // its id: wsys' number (the process's window under rio: 1)
        let id = WindowId(if win.id() != 0 { win.id() as u64 } else { 1 });
        let scale = win.dpi() as f64 / 96.0;
        let mut draw = Draw::open_in(win.dir()).map_err(os)?;
        let b = draw.border();
        let screen = draw.screen();
        let mut restore = None;
        let mut fullscreen = false;
        if first {
            if attrs.fullscreen.is_some() {
                fullscreen = true;
                // wsys' fullscreen (no border, no title bar, above its bars), else
                // rio's way: the whole screen, the border outside it
                if win.wctl("fullscreen").is_err() {
                    restore = Some(draw.content().inset(-b));
                    wctl_in(&win, &format!(
                        "resize -r {} {} {} {}",
                        screen.x0 - b,
                        screen.y0 - b,
                        screen.x1 + b,
                        screen.y1 + b
                    ));
                }
            } else {
                // logical sizes at the window system's scale (wsys' dpi)
                if let Some(size) = attrs.inner_size {
                    let (w, h): (u32, u32) = size.to_physical::<u32>(scale).into();
                    wctl_in(&win, &format!("resize -dx {} -dy {}", w as i32 + 2 * b, h as i32 + 2 * b));
                }
                if let Some(pos) = attrs.position {
                    let (x, y): (i32, i32) = pos.to_physical::<i32>(scale).into();
                    wctl_in(&win, &format!("move -minx {x} -miny {y}"));
                }
            }
        } else {
            // its frame: where the program puts it (a popup: in the screen's
            // coordinates, as winit's positions are), else where wsys did
            let (fx0, fy0, fx1, fy1) = win.rect().unwrap_or((screen.x0, screen.y0, screen.x0 + 400, screen.y0 + 300));
            let (w, h) = match attrs.inner_size {
                Some(size) => {
                    let (w, h): (u32, u32) = size.to_physical::<u32>(scale).into();
                    (w as i32 + 2 * b, h as i32 + 2 * b)
                },
                None => (fx1 - fx0, fy1 - fy0),
            };
            let (x, y) = match attrs.position {
                Some(pos) => pos.to_physical::<i32>(scale).into(),
                None => (fx0, fy0),
            };
            if let Err(e) = win.set_rect(x, y, x + w, y + h) {
                tracing::warn!("plan9: the window's place: {e}");
            }
            // the user's close asks the program (CloseRequested)
            if attrs.platform_specific.ask_close != Some(false) {
                let _ = win.ask_close(true);
            }
            if attrs.fullscreen.is_some() {
                fullscreen = win.wctl("fullscreen").is_ok();
            }
        }
        draw.attach().map_err(os)?;
        let content = draw.content();
        if !attrs.title.is_empty() {
            let _ = win.set_label(&attrs.title);
        }

        let feed = shared.clone();
        let inputs = Inputs::start_in(win.dir(), move |input| feed.poke(|q| q.inputs.push_back((id, input))))
            .map_err(os)?;
        let st = Arc::new(WinState {
            id,
            win,
            draw: Mutex::new(draw),
            inputs,
            content: Mutex::new(content),
            cursor: Mutex::new(CursorShape::Arrow),
            cursor_visible: AtomicBool::new(true),
            grab: Mutex::new(CursorGrabMode::None),
            focused: AtomicBool::new(first || attrs.active),
            hidden: AtomicBool::new(false),
            invisible: AtomicBool::new(false),
            restore: Mutex::new(restore),
            fullscreen: AtomicBool::new(fullscreen),
            scale: Mutex::new(scale),
        });
        shared.wins.lock().unwrap().insert(id, st.clone());
        let w = Window { shared: shared.clone(), st };
        w.set_cursor(attrs.cursor.clone());
        if attrs.maximized && !fullscreen {
            w.set_maximized(true);
        }
        if !attrs.visible {
            w.set_visible(false);
        } else if !first {
            // a window of the clone file comes on the screen with its first
            // picture or top: drawn through draw(3) (softbuffer) it has none
            if let Err(e) = w.st.win.show() {
                tracing::warn!("plan9: the window shown: {e}");
            }
        }
        if attrs.active && first {
            w.wctl("current");
        }
        shared.poke(|q| {
            q.created.push(id);
            q.redraw(id);
        });
        Ok(w)
    }

    /// a message to the window's wctl
    fn wctl(&self, msg: &str) {
        wctl_in(&self.st.win, msg);
    }

    /// its number in wsys (0: the process's window)
    pub(crate) fn wsys_window(&self) -> u32 {
        if self.st.win.is_process() { 0 } else { self.st.win.id() }
    }

    pub(crate) fn maybe_queue_on_main(&self, f: impl FnOnce(&Self) + Send + 'static) {
        f(self)
    }

    pub(crate) fn maybe_wait_on_main<R: Send>(&self, f: impl FnOnce(&Self) -> R + Send) -> R {
        f(self)
    }

    fn content(&self) -> Rect {
        *self.st.content.lock().unwrap()
    }

    fn border(&self) -> i32 {
        self.st.draw.lock().unwrap().border()
    }

    fn screen(&self) -> Rect {
        self.st.draw.lock().unwrap().screen()
    }

    #[inline]
    pub fn id(&self) -> WindowId {
        self.st.id
    }

    #[inline]
    pub fn primary_monitor(&self) -> Option<MonitorHandle> {
        Some(MonitorHandle)
    }

    #[inline]
    pub fn available_monitors(&self) -> VecDeque<MonitorHandle> {
        let mut v = VecDeque::with_capacity(1);
        v.push_back(MonitorHandle);
        v
    }

    #[inline]
    pub fn current_monitor(&self) -> Option<MonitorHandle> {
        Some(MonitorHandle)
    }

    #[inline]
    pub fn scale_factor(&self) -> f64 {
        *self.st.scale.lock().unwrap()
    }

    #[inline]
    pub fn request_redraw(&self) {
        self.shared.poke(|q| q.redraw(self.st.id));
    }

    #[inline]
    pub fn pre_present_notify(&self) {}

    #[inline]
    pub fn reset_dead_keys(&self) {}

    #[inline]
    pub fn inner_position(&self) -> Result<PhysicalPosition<i32>, error::NotSupportedError> {
        let c = self.content();
        Ok((c.x0, c.y0).into())
    }

    #[inline]
    pub fn outer_position(&self) -> Result<PhysicalPosition<i32>, error::NotSupportedError> {
        let (c, b) = (self.content(), self.border());
        Ok((c.x0 - b, c.y0 - b).into())
    }

    #[inline]
    pub fn set_outer_position(&self, position: Position) {
        let (x, y): (i32, i32) = position.to_physical::<i32>(self.scale_factor()).into();
        self.wctl(&format!("move -minx {x} -miny {y}"));
        self.st.relook();
    }

    #[inline]
    pub fn inner_size(&self) -> PhysicalSize<u32> {
        let c = self.content();
        (c.dx().max(0) as u32, c.dy().max(0) as u32).into()
    }

    #[inline]
    pub fn request_inner_size(&self, size: Size) -> Option<PhysicalSize<u32>> {
        let (w, h): (u32, u32) = size.to_physical::<u32>(self.scale_factor()).into();
        let b = self.border();
        self.wctl(&format!("resize -dx {} -dy {}", w as i32 + 2 * b, h as i32 + 2 * b));
        // the window system's answer comes as a resize the event loop sees
        None
    }

    #[inline]
    pub fn outer_size(&self) -> PhysicalSize<u32> {
        let (c, b) = (self.content(), self.border());
        ((c.dx() + 2 * b).max(0) as u32, (c.dy() + 2 * b).max(0) as u32).into()
    }

    #[inline]
    pub fn set_min_inner_size(&self, _: Option<Size>) {}

    #[inline]
    pub fn set_max_inner_size(&self, _: Option<Size>) {}

    #[inline]
    pub fn title(&self) -> String {
        std::fs::read_to_string(self.st.win.path("label")).unwrap_or_default()
    }

    #[inline]
    pub fn set_title(&self, title: &str) {
        let _ = self.st.win.set_label(title);
    }

    #[inline]
    pub fn set_transparent(&self, _transparent: bool) {}

    #[inline]
    pub fn set_blur(&self, _blur: bool) {}

    #[inline]
    pub fn set_visible(&self, visible: bool) {
        self.st.invisible.store(!visible, Ordering::Relaxed);
        self.wctl(if visible { "unhide" } else { "hide" });
    }

    #[inline]
    pub fn is_visible(&self) -> Option<bool> {
        Some(!self.st.invisible.load(Ordering::Relaxed) && !self.st.hidden.load(Ordering::Relaxed))
    }

    #[inline]
    pub fn resize_increments(&self) -> Option<PhysicalSize<u32>> {
        None
    }

    #[inline]
    pub fn set_resize_increments(&self, _increments: Option<Size>) {}

    #[inline]
    pub fn set_resizable(&self, _resizeable: bool) {}

    #[inline]
    pub fn is_resizable(&self) -> bool {
        true
    }

    #[inline]
    pub fn set_minimized(&self, minimized: bool) {
        // the window system's hide: what its menu does
        self.wctl(if minimized { "hide" } else { "unhide" });
        self.st.hidden.store(minimized, Ordering::Relaxed);
    }

    #[inline]
    pub fn is_minimized(&self) -> Option<bool> {
        Some(self.st.hidden.load(Ordering::Relaxed) && !self.st.invisible.load(Ordering::Relaxed))
    }

    #[inline]
    pub fn set_maximized(&self, maximized: bool) {
        let mut restore = self.st.restore.lock().unwrap();
        if maximized && restore.is_none() {
            let s = self.screen();
            *restore = Some(self.content().inset(-self.border()));
            self.wctl(&format!("resize -r {} {} {} {}", s.x0, s.y0, s.x1, s.y1));
        } else if !maximized {
            if let Some(r) = restore.take() {
                self.wctl(&format!("resize -r {} {} {} {}", r.x0, r.y0, r.x1, r.y1));
            }
        }
        drop(restore);
        self.st.relook();
    }

    #[inline]
    pub fn is_maximized(&self) -> bool {
        self.st.restore.lock().unwrap().is_some() && !self.st.fullscreen.load(Ordering::Relaxed)
    }

    #[inline]
    pub(crate) fn set_fullscreen(&self, monitor: Option<Fullscreen>) {
        let mut restore = self.st.restore.lock().unwrap();
        let full = self.st.fullscreen.load(Ordering::Relaxed);
        if monitor.is_some() {
            // wsys' fullscreen, else rio's way: the border outside the screen
            if !full && self.st.win.wctl("fullscreen").is_err() {
                let (s, b) = (self.screen(), self.border());
                if restore.is_none() {
                    *restore = Some(self.content().inset(-b));
                }
                self.wctl(&format!("resize -r {} {} {} {}", s.x0 - b, s.y0 - b, s.x1 + b, s.y1 + b));
            }
            self.st.fullscreen.store(true, Ordering::Relaxed);
        } else if let Some(r) = restore.take() {
            self.wctl(&format!("resize -r {} {} {} {}", r.x0, r.y0, r.x1, r.y1));
            self.st.fullscreen.store(false, Ordering::Relaxed);
        } else if full {
            self.wctl("unfullscreen");
            self.st.fullscreen.store(false, Ordering::Relaxed);
        }
        drop(restore);
        self.st.relook();
    }

    #[inline]
    pub(crate) fn fullscreen(&self) -> Option<Fullscreen> {
        self.st.fullscreen.load(Ordering::Relaxed).then(|| Fullscreen::Borderless(Some(MonitorHandle)))
    }

    #[inline]
    pub fn set_decorations(&self, _decorations: bool) {}

    #[inline]
    pub fn is_decorated(&self) -> bool {
        self.border() > 0
    }

    #[inline]
    pub fn set_window_level(&self, _level: window::WindowLevel) {}

    #[inline]
    pub fn set_window_icon(&self, _window_icon: Option<crate::icon::Icon>) {}

    #[inline]
    pub fn set_ime_cursor_area(&self, _position: Position, _size: Size) {}

    #[inline]
    pub fn set_ime_allowed(&self, _allowed: bool) {}

    #[inline]
    pub fn set_ime_purpose(&self, _purpose: ImePurpose) {}

    #[inline]
    pub fn focus_window(&self) {
        self.wctl("current");
    }

    #[inline]
    pub fn request_user_attention(&self, _request_type: Option<window::UserAttentionType>) {}

    #[inline]
    pub fn set_cursor(&self, c: Cursor) {
        let shape = match c {
            Cursor::Icon(icon) => match shape(icon) {
                Some(s) => CursorShape::Shape(cursor::shape(s)),
                None => CursorShape::Arrow,
            },
            Cursor::Custom(c) => {
                let i = &c.inner.0;
                CursorShape::Shape(cursor::from_rgba(
                    &i.rgba,
                    i.width as usize,
                    i.height as usize,
                    i.hotspot_x as i32,
                    i.hotspot_y as i32,
                ))
            },
        };
        *self.st.cursor.lock().unwrap() = shape;
        self.st.show_cursor();
    }

    #[inline]
    pub fn set_cursor_position(&self, position: Position) -> Result<(), error::ExternalError> {
        let (x, y): (i32, i32) = position.to_physical::<i32>(self.scale_factor()).into();
        let c = self.content();
        self.st
            .inputs
            .warp(c.x0 + x, c.y0 + y)
            .map_err(|e| error::ExternalError::Os(os(e)))
    }

    #[inline]
    pub fn set_cursor_grab(&self, mode: CursorGrabMode) -> Result<(), error::ExternalError> {
        // Locked: the pointer kept in the middle, its motion as device events;
        // Confined: nothing to confine it with
        *self.st.grab.lock().unwrap() = mode;
        if mode == CursorGrabMode::Locked {
            let c = self.content();
            let _ = self.st.inputs.warp(c.x0 + c.dx() / 2, c.y0 + c.dy() / 2);
        }
        Ok(())
    }

    #[inline]
    pub fn set_cursor_visible(&self, visible: bool) {
        self.st.cursor_visible.store(visible, Ordering::Relaxed);
        self.st.show_cursor();
    }

    #[inline]
    pub fn drag_window(&self) -> Result<(), error::ExternalError> {
        Err(error::ExternalError::NotSupported(error::NotSupportedError::new()))
    }

    #[inline]
    pub fn drag_resize_window(
        &self,
        _direction: window::ResizeDirection,
    ) -> Result<(), error::ExternalError> {
        Err(error::ExternalError::NotSupported(error::NotSupportedError::new()))
    }

    #[inline]
    pub fn show_window_menu(&self, _position: Position) {}

    #[inline]
    pub fn set_cursor_hittest(&self, _hittest: bool) -> Result<(), error::ExternalError> {
        Err(error::ExternalError::NotSupported(error::NotSupportedError::new()))
    }

    #[cfg(feature = "rwh_06")]
    #[inline]
    pub fn raw_window_handle_rwh_06(&self) -> Result<rwh_06::RawWindowHandle, rwh_06::HandleError> {
        Ok(rwh_06::RawWindowHandle::Plan9(rwh_06::Plan9WindowHandle::with_window(self.wsys_window())))
    }

    #[cfg(feature = "rwh_06")]
    #[inline]
    pub fn raw_display_handle_rwh_06(
        &self,
    ) -> Result<rwh_06::RawDisplayHandle, rwh_06::HandleError> {
        Ok(rwh_06::RawDisplayHandle::Plan9(rwh_06::Plan9DisplayHandle::new()))
    }

    #[inline]
    pub fn set_enabled_buttons(&self, _buttons: window::WindowButtons) {}

    #[inline]
    pub fn enabled_buttons(&self) -> window::WindowButtons {
        window::WindowButtons::all()
    }

    #[inline]
    pub fn theme(&self) -> Option<window::Theme> {
        desktop_theme().map(|(t, _)| t)
    }

    #[inline]
    pub fn has_focus(&self) -> bool {
        self.st.focused.load(Ordering::Relaxed)
    }

    #[inline]
    pub fn set_theme(&self, _theme: Option<window::Theme>) {}

    pub fn set_content_protected(&self, _protected: bool) {}
}

impl Drop for Window {
    fn drop(&mut self) {
        // a window of the clone file goes with its WinState: out of the loop's
        // map now, the clone file closed when this last Arc goes (the loop
        // says Destroyed in its next round)
        let (id, dir) = (self.st.id, self.st.win.dir().to_string());
        p9win::cursor::forget(&dir);
        self.shared.wins.lock().unwrap().remove(&id);
        self.shared.poke(|q| q.destroyed.push(id));
    }
}

/// the desktop's theme (wsys(4)'s /dev/theme: its text, light or dark by its
/// windows' colour); None without one (rio)
pub(crate) fn desktop_theme() -> Option<(window::Theme, String)> {
    let text = std::fs::read_to_string("/dev/theme").ok()?;
    let back = text.lines().find_map(|l| {
        let mut f = l.split_whitespace();
        (f.next() == Some("window")).then(|| f.next()).flatten()
    });
    let rgb = back.and_then(|v| u32::from_str_radix(v.trim_start_matches('#'), 16).ok()).unwrap_or(0xFFFFFF);
    let luma = 299 * ((rgb >> 16) & 0xFF) + 587 * ((rgb >> 8) & 0xFF) + 114 * (rgb & 0xFF);
    let t = if luma >= 128_000 { window::Theme::Light } else { window::Theme::Dark };
    Some((t, text))
}
