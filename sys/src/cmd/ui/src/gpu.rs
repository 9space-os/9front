// Copyright 2025 the Xilem Authors
// SPDX-License-Identifier: Apache-2.0
//
// ui's window on the GPU: Masonry's frame drawn by vello on wgpu's Vulkan
// (Mesa's libvulkan.a, VK_PLAN9_surface) into a texture of its own, copied
// onto the window's surface, presented. After masonry_winit's vello_util.rs
// and event_loop_runner.rs (the surface, its format and alpha, the
// intermediate texture vello's compute shaders need, the blit), reduced to
// one window and one device.

use std::future::Future;
use std::sync::Arc;
use std::task::{Context, Poll, Waker};

use masonry_imaging::PreparedFrame;
use masonry_imaging::texture_render::{RenderTarget, Renderer};
use wgpu::util::TextureBlitter;
use winit::window::Window;

pub struct Gpu {
    surface: wgpu::Surface<'static>,
    config: wgpu::SurfaceConfiguration,
    adapter: wgpu::Adapter,
    device: wgpu::Device,
    queue: wgpu::Queue,
    target: wgpu::Texture,
    view: wgpu::TextureView,
    part: Option<(u32, u32, wgpu::Texture, wgpu::TextureView)>, // a damaged part's (its size), drawn alone
    blitter: TextureBlitter,
    renderer: Renderer,
    pub name: String,
}

/// wgpu's futures on native backends are ready when first polled
fn now<F: Future>(f: F) -> F::Output {
    let mut f = std::pin::pin!(f);
    let mut cx = Context::from_waker(Waker::noop());
    loop {
        if let Poll::Ready(r) = f.as_mut().poll(&mut cx) {
            return r;
        }
        std::thread::yield_now();
    }
}

/// vello's texture: storage for its compute shaders, the blit's source
fn target(device: &wgpu::Device, w: u32, h: u32) -> (wgpu::Texture, wgpu::TextureView) {
    let t = device.create_texture(&wgpu::TextureDescriptor {
        label: Some("ui target"),
        size: wgpu::Extent3d { width: w, height: h, depth_or_array_layers: 1 },
        mip_level_count: 1,
        sample_count: 1,
        dimension: wgpu::TextureDimension::D2,
        usage: wgpu::TextureUsages::STORAGE_BINDING
            | wgpu::TextureUsages::TEXTURE_BINDING
            | wgpu::TextureUsages::COPY_DST
            | wgpu::TextureUsages::COPY_SRC
            | wgpu::TextureUsages::RENDER_ATTACHMENT,
        format: wgpu::TextureFormat::Rgba8Unorm,
        view_formats: &[],
    });
    let v = t.create_view(&wgpu::TextureViewDescriptor::default());
    (t, v)
}

impl Gpu {
    /// the window's surface on an adapter that can present to it; an error
    /// (no GPU, no Vulkan driver) leaves the window to the processor
    pub fn new(window: Arc<Window>) -> Result<Gpu, String> {
        let size = window.inner_size();
        let instance = wgpu::Instance::new(&wgpu::InstanceDescriptor {
            backends: wgpu::Backends::VULKAN,
            ..Default::default()
        });
        crate::mark("gpu: instance");
        let surface = instance.create_surface(window).map_err(|e| format!("surface: {e}"))?;
        crate::mark("gpu: surface");
        let adapter = now(instance.request_adapter(&wgpu::RequestAdapterOptions {
            power_preference: wgpu::PowerPreference::default(),
            force_fallback_adapter: false,
            compatible_surface: Some(&surface),
        }))
        .map_err(|e| {
            // what Vulkan has, for the message: none, or none that can present here
            let all: Vec<String> = now(instance.enumerate_adapters(wgpu::Backends::VULKAN))
                .iter()
                .map(|a| a.get_info().name)
                .collect();
            format!("adapter: {e}; Vulkan's adapters: {all:?}")
        })?;
        crate::mark("gpu: adapter");
        let features = adapter.features() & wgpu::Features::CLEAR_TEXTURE;
        let (device, queue) = now(adapter.request_device(&wgpu::DeviceDescriptor {
            label: Some("ui"),
            required_features: features,
            required_limits: wgpu::Limits::default(),
            ..Default::default()
        }))
        .map_err(|e| format!("device: {e}"))?;
        crate::mark("gpu: device");
        let caps = surface.get_capabilities(&adapter);
        let format = caps
            .formats
            .iter()
            .copied()
            .find(|f| matches!(f, wgpu::TextureFormat::Rgba8Unorm | wgpu::TextureFormat::Bgra8Unorm))
            .ok_or("no 8-bit unorm surface format")?;
        // the window is opaque: its pixels as they are
        let alpha_mode = if caps.alpha_modes.contains(&wgpu::CompositeAlphaMode::Opaque) {
            wgpu::CompositeAlphaMode::Opaque
        } else {
            wgpu::CompositeAlphaMode::Auto
        };
        let config = wgpu::SurfaceConfiguration {
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT,
            format,
            width: size.width.max(1),
            height: size.height.max(1),
            present_mode: wgpu::PresentMode::AutoVsync,
            desired_maximum_frame_latency: 2,
            alpha_mode,
            view_formats: vec![],
        };
        surface.configure(&device, &config);
        crate::mark("gpu: swapchain");
        let (target, view) = target(&device, config.width, config.height);
        let blitter = TextureBlitter::new(&device, format);
        let i = adapter.get_info();
        Ok(Gpu {
            surface,
            config,
            adapter,
            device,
            queue,
            target,
            view,
            part: None,
            blitter,
            renderer: Renderer::new(),
            name: i.name,
        })
    }

    fn resize(&mut self, w: u32, h: u32) {
        self.config.width = w;
        self.config.height = h;
        self.surface.configure(&self.device, &self.config);
        (self.target, self.view) = target(&self.device, w, h);
        self.part = None;
    }

    /// the frame w x h into the window: all of it, or only part (x y w h:
    /// what changed), drawn alone and copied into the picture kept (target)
    pub fn render(&mut self, frame: PreparedFrame<'_>, w: u32, h: u32, part: Option<(u32, u32, u32, u32)>) -> Result<(), String> {
        let mut part = part;
        if (w, h) != (self.config.width, self.config.height) {
            self.resize(w, h);
            part = None; // a new target: all of it
        }
        let tex = match self.surface.get_current_texture() {
            Ok(t) => t,
            Err(wgpu::SurfaceError::Outdated | wgpu::SurfaceError::Lost) => {
                self.resize(w, h);
                self.surface.get_current_texture().map_err(|e| format!("surface: {e}"))?
            }
            Err(e) => return Err(format!("surface: {e}")),
        };
        let mut enc = self.device.create_command_encoder(&wgpu::CommandEncoderDescriptor { label: Some("ui blit") });
        match part {
            None => self
                .renderer
                .render_to_texture(
                    RenderTarget {
                        adapter: &self.adapter,
                        device: &self.device,
                        queue: &self.queue,
                        texture: &self.target,
                        view: &self.view,
                    },
                    frame,
                )
                .map_err(|e| format!("vello: {e}"))?,
            Some((x, y, pw, ph)) => {
                // the part alone (vello's work as big as it), then into the picture kept
                if !matches!(&self.part, Some((cw, ch, _, _)) if (*cw, *ch) == (pw, ph)) {
                    let (t, v) = target(&self.device, pw, ph);
                    self.part = Some((pw, ph, t, v));
                }
                let (_, _, pt, pv) = self.part.as_ref().unwrap();
                self.renderer
                    .render_to_texture(
                        RenderTarget { adapter: &self.adapter, device: &self.device, queue: &self.queue, texture: pt, view: pv },
                        frame.region(x, y, pw, ph),
                    )
                    .map_err(|e| format!("vello: {e}"))?;
                enc.copy_texture_to_texture(
                    pt.as_image_copy(),
                    wgpu::TexelCopyTextureInfo {
                        texture: &self.target,
                        mip_level: 0,
                        origin: wgpu::Origin3d { x, y, z: 0 },
                        aspect: wgpu::TextureAspect::All,
                    },
                    wgpu::Extent3d { width: pw, height: ph, depth_or_array_layers: 1 },
                );
            }
        }
        let out = tex.texture.create_view(&wgpu::TextureViewDescriptor::default());
        self.blitter.copy(&self.device, &mut enc, &self.view, &out);
        self.queue.submit([enc.finish()]);
        tex.present();
        Ok(())
    }
}
