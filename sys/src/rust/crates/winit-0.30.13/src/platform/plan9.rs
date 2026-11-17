//! 9front: what only wsys(4) does with a program's windows.
//!
//! A process has its window (the one its name space's `/dev` is); under wsys
//! it may have more: `Window`s made after the first are windows of their own
//! (wsys' `clone`), dialogs of their parent window
//! (`WindowAttributes::with_parent_window`), or popups without decorations
//! (`with_decorations(false)`: menus, tooltips).

use crate::window::{Window, WindowAttributes};

/// window attributes only wsys knows
pub trait WindowAttributesExtPlan9 {
    /// a dialog that takes its parent's clicks while it is shown (wsys' modal):
    /// a click on the parent brings it up instead
    fn with_modal(self, modal: bool) -> Self;
}

impl WindowAttributesExtPlan9 for WindowAttributes {
    fn with_modal(mut self, modal: bool) -> Self {
        self.platform_specific.modal = modal;
        self
    }
}

/// what wsys says of a window
pub trait WindowExtPlan9 {
    /// its number in wsys (0: the process's window, or none: rio)
    fn wsys_window(&self) -> u32;
}

impl WindowExtPlan9 for Window {
    fn wsys_window(&self) -> u32 {
        self.window.maybe_wait_on_main(|w| w.wsys_window())
    }
}
