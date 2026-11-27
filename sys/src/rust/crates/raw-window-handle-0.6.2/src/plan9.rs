// 9front: handles for 9front's window systems (rio, wsys) and draw(3).
// SPDX-License-Identifier: MIT OR Apache-2.0 OR Zlib (as the crate)

use super::DisplayHandle;

/// Raw display handle for 9front: draw(3) of the process's name space.
///
/// It names nothing: a process has one display, the draw(3) device its name
/// space has.
#[non_exhaustive]
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct Plan9DisplayHandle {}

impl Plan9DisplayHandle {
    /// Create a new display handle.
    ///
    /// # Example
    ///
    /// ```
    /// # use raw_window_handle::Plan9DisplayHandle;
    /// let handle = Plan9DisplayHandle::new();
    /// ```
    pub fn new() -> Self {
        Self {}
    }
}

impl DisplayHandle<'static> {
    /// Create a 9front display handle.
    ///
    /// As no data is borrowed by this handle, it is completely safe to create.
    pub fn plan9() -> Self {
        // SAFETY: No data is borrowed.
        unsafe { Self::borrow_raw(Plan9DisplayHandle::new().into()) }
    }
}

/// Raw window handle for 9front: one of the process's windows.
///
/// A process of rio or wsys has its window, the one its name space's
/// `/dev/winname` names (the screen without a window system): `window` 0.
/// Under wsys it may have more (wsys(4)'s `clone`: dialogs, popups, a second
/// main window): `window` n, the program's number for it (p9win's key, from
/// 1), its directory of files at `/mnt/wsyswin.n`, which stays when the window
/// is made again in the next wsys. Vulkan's `VK_PLAN9_surface` and EGL's
/// plan9 platform take the same window (its `winname`, in that directory).
#[non_exhaustive]
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct Plan9WindowHandle {
    /// 0: the process's window (`/dev`); n: the window at `/mnt/wsyswin.n`
    pub window: u32,
}

impl Plan9WindowHandle {
    /// Create a new handle to the process's window.
    ///
    /// # Example
    ///
    /// ```
    /// # use raw_window_handle::Plan9WindowHandle;
    /// let handle = Plan9WindowHandle::new();
    /// ```
    pub fn new() -> Self {
        Self { window: 0 }
    }

    /// A handle to the process's window n (`/mnt/wsyswin.n`; 0: its own, `/dev`).
    pub fn with_window(window: u32) -> Self {
        Self { window }
    }
}
