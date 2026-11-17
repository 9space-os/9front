/* Copyright (C) 2021 Google, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

struct driconf_option {
    const char *name;
    const char *value;
};

struct driconf_application {
    const char *name;
    const char *executable;
    const char *executable_regexp;
    const char *blake3;
    const char *application_name_match;
    const char *application_versions;
    unsigned num_options;
    const struct driconf_option *options;
};

struct driconf_engine {
    const char *engine_name_match;
    const char *engine_versions;
    unsigned num_options;
    const struct driconf_option *options;
};

struct driconf_device {
    const char *driver;
    const char *device;
    unsigned num_engines;
    const struct driconf_engine *engines;
    unsigned num_applications;
    const struct driconf_application *applications;
};



    
static const struct driconf_option engine_383_options[] = {
    { .name = "vk_x11_strict_image_count", .value = "true" },
};


static const struct driconf_engine device_1_engines[] = {
    { .engine_name_match = "UnrealEngine4.*",
      .engine_versions = "0:23",
      .num_options = 1,
      .options = engine_383_options,
    },
};

    
static const struct driconf_option application_2_options[] = {
    { .name = "allow_glsl_extension_directive_midshader", .value = "true" },
};

    
static const struct driconf_option application_4_options[] = {
    { .name = "force_glsl_extensions_warn", .value = "true" },
    { .name = "disable_blend_func_extended", .value = "true" },
    { .name = "disable_arb_gpu_shader5", .value = "true" },
};

    
static const struct driconf_option application_8_options[] = {
    { .name = "force_glsl_extensions_warn", .value = "true" },
    { .name = "disable_blend_func_extended", .value = "true" },
    { .name = "disable_arb_gpu_shader5", .value = "true" },
};

    
static const struct driconf_option application_12_options[] = {
    { .name = "allow_glsl_extension_directive_midshader", .value = "true" },
    { .name = "dual_color_blend_by_location", .value = "true" },
};

    
static const struct driconf_option application_15_options[] = {
    { .name = "allow_glsl_extension_directive_midshader", .value = "true" },
    { .name = "dual_color_blend_by_location", .value = "true" },
};

    
static const struct driconf_option application_18_options[] = {
    { .name = "allow_glsl_extension_directive_midshader", .value = "true" },
    { .name = "dual_color_blend_by_location", .value = "true" },
};

    
static const struct driconf_option application_21_options[] = {
    { .name = "allow_glsl_extension_directive_midshader", .value = "true" },
    { .name = "dual_color_blend_by_location", .value = "true" },
};

    
static const struct driconf_option application_24_options[] = {
    { .name = "allow_glsl_extension_directive_midshader", .value = "true" },
    { .name = "dual_color_blend_by_location", .value = "true" },
};

    
static const struct driconf_option application_27_options[] = {
    { .name = "allow_glsl_extension_directive_midshader", .value = "true" },
    { .name = "dual_color_blend_by_location", .value = "true" },
};

    
static const struct driconf_option application_30_options[] = {
    { .name = "disable_blend_func_extended", .value = "true" },
    { .name = "allow_glsl_extension_directive_midshader", .value = "true" },
};

    
static const struct driconf_option application_33_options[] = {
    { .name = "disable_blend_func_extended", .value = "true" },
    { .name = "allow_glsl_extension_directive_midshader", .value = "true" },
};

    
static const struct driconf_option application_36_options[] = {
    { .name = "disable_glsl_line_continuations", .value = "true" },
};

    
static const struct driconf_option application_38_options[] = {
    { .name = "disable_uniform_array_resize", .value = "true" },
};

    
static const struct driconf_option application_40_options[] = {
    { .name = "always_have_depth_buffer", .value = "true" },
};

    
static const struct driconf_option application_42_options[] = {
    { .name = "always_have_depth_buffer", .value = "true" },
};

    
static const struct driconf_option application_44_options[] = {
    { .name = "allow_glsl_extension_directive_midshader", .value = "true" },
    { .name = "allow_glsl_builtin_variable_redeclaration", .value = "true" },
};

    
static const struct driconf_option application_47_options[] = {
    { .name = "allow_glsl_builtin_variable_redeclaration", .value = "true" },
};

    
static const struct driconf_option application_49_options[] = {
    { .name = "allow_glsl_builtin_variable_redeclaration", .value = "true" },
    { .name = "force_glsl_extensions_warn", .value = "true" },
    { .name = "ignore_map_invalidate_buffer", .value = "true" },
};

    
static const struct driconf_option application_53_options[] = {
    { .name = "disable_uniform_array_resize", .value = "true" },
};

    
static const struct driconf_option application_55_options[] = {
    { .name = "disable_uniform_array_resize", .value = "true" },
    { .name = "alias_shader_extension", .value = "GL_ATI_shader_texture_lod:GL_ARB_shader_texture_lod" },
    { .name = "allow_vertex_texture_bias", .value = "true" },
    { .name = "force_glsl_version", .value = "130" },
};

    
static const struct driconf_option application_60_options[] = {
    { .name = "glsl_zero_init", .value = "true" },
};

    
static const struct driconf_option application_62_options[] = {
    { .name = "allow_glsl_builtin_variable_redeclaration", .value = "true" },
    { .name = "dual_color_blend_by_location", .value = "true" },
};

    
static const struct driconf_option application_65_options[] = {
    { .name = "glsl_zero_init", .value = "true" },
};

    
static const struct driconf_option application_67_options[] = {
    { .name = "allow_glsl_builtin_variable_redeclaration", .value = "true" },
};

    
static const struct driconf_option application_69_options[] = {
    { .name = "allow_glsl_builtin_variable_redeclaration", .value = "true" },
    { .name = "force_gl_map_buffer_synchronized", .value = "true" },
};

    
static const struct driconf_option application_72_options[] = {
    { .name = "allow_glsl_builtin_variable_redeclaration", .value = "true" },
    { .name = "force_gl_map_buffer_synchronized", .value = "true" },
};

    
static const struct driconf_option application_75_options[] = {
    { .name = "allow_glsl_extension_directive_midshader", .value = "true" },
};

    
static const struct driconf_option application_77_options[] = {
    { .name = "force_gl_depth_component_type_int", .value = "true" },
};

    
static const struct driconf_option application_79_options[] = {
    { .name = "allow_glsl_extension_directive_midshader", .value = "true" },
};

    
static const struct driconf_option application_81_options[] = {
    { .name = "allow_glsl_extension_directive_midshader", .value = "true" },
};

    
static const struct driconf_option application_83_options[] = {
    { .name = "glsl_zero_init", .value = "true" },
};

    
static const struct driconf_option application_85_options[] = {
    { .name = "allow_glsl_extension_directive_midshader", .value = "true" },
    { .name = "glsl_correct_derivatives_after_discard", .value = "true" },
    { .name = "force_gl_vendor", .value = "ATI Technologies, Inc." },
};

    
static const struct driconf_option application_89_options[] = {
    { .name = "allow_glsl_extension_directive_midshader", .value = "true" },
};

    
static const struct driconf_option application_91_options[] = {
    { .name = "allow_higher_compat_version", .value = "true" },
};

    
static const struct driconf_option application_93_options[] = {
    { .name = "allow_higher_compat_version", .value = "true" },
};

    
static const struct driconf_option application_95_options[] = {
    { .name = "allow_higher_compat_version", .value = "true" },
};

    
static const struct driconf_option application_97_options[] = {
    { .name = "allow_higher_compat_version", .value = "true" },
};

    
static const struct driconf_option application_99_options[] = {
    { .name = "allow_higher_compat_version", .value = "true" },
};

    
static const struct driconf_option application_101_options[] = {
    { .name = "allow_higher_compat_version", .value = "true" },
};

    
static const struct driconf_option application_103_options[] = {
    { .name = "force_explicit_uniform_loc_zero", .value = "fQualityFactor" },
    { .name = "force_compat_profile", .value = "true" },
};

    
static const struct driconf_option application_106_options[] = {
    { .name = "force_compat_shaders", .value = "true" },
};

    
static const struct driconf_option application_108_options[] = {
    { .name = "force_compat_shaders", .value = "true" },
};

    
static const struct driconf_option application_110_options[] = {
    { .name = "force_glsl_version", .value = "440" },
};

    
static const struct driconf_option application_112_options[] = {
    { .name = "force_glsl_abs_sqrt", .value = "true" },
};

    
static const struct driconf_option application_114_options[] = {
    { .name = "force_glsl_abs_sqrt", .value = "true" },
};

    
static const struct driconf_option application_116_options[] = {
    { .name = "force_compat_shaders", .value = "true" },
    { .name = "glx_clear_context_reset_isolation_bit", .value = "true" },
};

    
static const struct driconf_option application_119_options[] = {
    { .name = "force_compat_profile", .value = "true" },
};

    
static const struct driconf_option application_121_options[] = {
    { .name = "glsl_zero_init", .value = "true" },
};

    
static const struct driconf_option application_123_options[] = {
    { .name = "glsl_zero_init", .value = "true" },
};

    
static const struct driconf_option application_125_options[] = {
    { .name = "force_glsl_version", .value = "130" },
    { .name = "glsl_ignore_write_to_readonly_var", .value = "true" },
};

    
static const struct driconf_option application_128_options[] = {
    { .name = "allow_glsl_embedded_structure_declarations", .value = "true" },
};

    
static const struct driconf_option application_130_options[] = {
    { .name = "glsl_correct_derivatives_after_discard", .value = "true" },
};

    
static const struct driconf_option application_132_options[] = {
    { .name = "glsl_correct_derivatives_after_discard", .value = "true" },
};

    
static const struct driconf_option application_134_options[] = {
    { .name = "allow_glsl_cross_stage_interpolation_mismatch", .value = "true" },
};

    
static const struct driconf_option application_136_options[] = {
    { .name = "allow_glsl_cross_stage_interpolation_mismatch", .value = "true" },
};

    
static const struct driconf_option application_138_options[] = {
    { .name = "allow_glsl_120_subset_in_110", .value = "true" },
};

    
static const struct driconf_option application_140_options[] = {
    { .name = "allow_glsl_120_subset_in_110", .value = "true" },
};

    
static const struct driconf_option application_142_options[] = {
    { .name = "allow_glsl_cross_stage_interpolation_mismatch", .value = "true" },
};

    
static const struct driconf_option application_144_options[] = {
    { .name = "allow_glsl_cross_stage_interpolation_mismatch", .value = "true" },
};

    
static const struct driconf_option application_146_options[] = {
    { .name = "allow_glsl_cross_stage_interpolation_mismatch", .value = "true" },
};

    
static const struct driconf_option application_148_options[] = {
    { .name = "allow_glsl_builtin_const_expression", .value = "true" },
    { .name = "allow_glsl_relaxed_es", .value = "true" },
};

    
static const struct driconf_option application_151_options[] = {
    { .name = "allow_extra_pp_tokens", .value = "true" },
};

    
static const struct driconf_option application_153_options[] = {
    { .name = "force_compat_profile", .value = "true" },
};

    
static const struct driconf_option application_155_options[] = {
    { .name = "glsl_correct_derivatives_after_discard", .value = "true" },
};

    
static const struct driconf_option application_157_options[] = {
    { .name = "force_compat_profile", .value = "true" },
};

    
static const struct driconf_option application_159_options[] = {
    { .name = "glsl_zero_init", .value = "true" },
};

    
static const struct driconf_option application_161_options[] = {
    { .name = "glsl_zero_init", .value = "true" },
};

    
static const struct driconf_option application_163_options[] = {
    { .name = "force_integer_tex_nearest", .value = "true" },
};

    
static const struct driconf_option application_165_options[] = {
    { .name = "force_integer_tex_nearest", .value = "true" },
};

    
static const struct driconf_option application_167_options[] = {
    { .name = "vs_position_always_invariant", .value = "true" },
};

    
static const struct driconf_option application_169_options[] = {
    { .name = "force_integer_tex_nearest", .value = "true" },
};

    
static const struct driconf_option application_171_options[] = {
    { .name = "lower_depth_range_rate", .value = "0.8" },
};

    
static const struct driconf_option application_173_options[] = {
    { .name = "lower_depth_range_rate", .value = "0.8" },
};

    
static const struct driconf_option application_175_options[] = {
    { .name = "allow_multisampled_copyteximage", .value = "true" },
    { .name = "vertex_program_default_out", .value = "true" },
};

    
static const struct driconf_option application_178_options[] = {
    { .name = "allow_multisampled_copyteximage", .value = "true" },
    { .name = "vertex_program_default_out", .value = "true" },
};

    
static const struct driconf_option application_181_options[] = {
    { .name = "vblank_mode", .value = "0" },
    { .name = "glthread_nop_check_framebuffer_status", .value = "true" },
    { .name = "allow_glsl_extension_directive_midshader", .value = "true" },
    { .name = "allow_glsl_120_subset_in_110", .value = "true" },
    { .name = "force_gl_vendor", .value = "NVIDIA Corporation" },
    { .name = "force_glsl_extensions_warn", .value = "true" },
    { .name = "mesa_no_error", .value = "true" },
    { .name = "allow_rgb10_configs", .value = "false" },
    { .name = "allow_invalid_glx_destroy_window", .value = "true" },
    { .name = "allow_rgb16_configs", .value = "false" },
};

    
static const struct driconf_option application_192_options[] = {
    { .name = "ignore_map_unsynchronized", .value = "true" },
};

    
static const struct driconf_option application_194_options[] = {
    { .name = "zero_invalidated_buffers", .value = "true" },
};

    
static const struct driconf_option application_196_options[] = {
    { .name = "zero_invalidated_buffers", .value = "true" },
};

    
static const struct driconf_option application_198_options[] = {
    { .name = "force_integer_tex_nearest", .value = "true" },
    { .name = "allow_glsl_extension_directive_midshader", .value = "true" },
};

    
static const struct driconf_option application_201_options[] = {
    { .name = "glsl_correct_derivatives_after_discard", .value = "true" },
};

    
static const struct driconf_option application_203_options[] = {
    { .name = "allow_glsl_120_subset_in_110", .value = "true" },
};

    
static const struct driconf_option application_205_options[] = {
    { .name = "allow_glsl_120_subset_in_110", .value = "true" },
};

    
static const struct driconf_option application_207_options[] = {
    { .name = "allow_glsl_120_subset_in_110", .value = "true" },
};

    
static const struct driconf_option application_209_options[] = {
    { .name = "allow_glsl_120_subset_in_110", .value = "true" },
};

    
static const struct driconf_option application_211_options[] = {
    { .name = "allow_glsl_120_subset_in_110", .value = "true" },
};

    
static const struct driconf_option application_213_options[] = {
    { .name = "allow_glsl_120_subset_in_110", .value = "true" },
};

    
static const struct driconf_option application_215_options[] = {
    { .name = "allow_glsl_120_subset_in_110", .value = "true" },
};

    
static const struct driconf_option application_217_options[] = {
    { .name = "mesa_extension_override", .value = "+GL_EXT_geometry_shader4" },
};

    
static const struct driconf_option application_219_options[] = {
    { .name = "mesa_extension_override", .value = "-GL_MESA_pack_invert -GL_MESA_framebuffer_flip_y -GL_MESA_window_pos" },
};

    
static const struct driconf_option application_221_options[] = {
    { .name = "mesa_extension_override", .value = "-GL_MESA_pack_invert -GL_MESA_framebuffer_flip_y -GL_MESA_window_pos" },
    { .name = "do_dce_before_clip_cull_analysis", .value = "true" },
};

    
static const struct driconf_option application_224_options[] = {
    { .name = "mesa_extension_override", .value = "-GL_MESA_pack_invert -GL_MESA_framebuffer_flip_y -GL_MESA_window_pos" },
};

    
static const struct driconf_option application_226_options[] = {
    { .name = "force_glsl_extensions_warn", .value = "true" },
};

    
static const struct driconf_option application_228_options[] = {
    { .name = "force_direct_glx_context", .value = "true" },
};

    
static const struct driconf_option application_230_options[] = {
    { .name = "keep_native_window_glx_drawable", .value = "true" },
    { .name = "allow_rgb10_configs", .value = "false" },
};

    
static const struct driconf_option application_233_options[] = {
    { .name = "force_gl_vendor", .value = "ATI Technologies Inc." },
};

    
static const struct driconf_option application_235_options[] = {
    { .name = "force_compat_shaders", .value = "true" },
};

    
static const struct driconf_option application_237_options[] = {
    { .name = "mesa_glthread_app_profile", .value = "0" },
};

    
static const struct driconf_option application_239_options[] = {
    { .name = "vk_wsi_force_swapchain_to_current_extent", .value = "true" },
    { .name = "vk_x11_ignore_suboptimal", .value = "true" },
};

    
static const struct driconf_option application_242_options[] = {
    { .name = "vk_wsi_force_swapchain_to_current_extent", .value = "true" },
    { .name = "vk_x11_ignore_suboptimal", .value = "true" },
};

    
static const struct driconf_option application_245_options[] = {
    { .name = "vk_wsi_force_swapchain_to_current_extent", .value = "true" },
    { .name = "vk_x11_ignore_suboptimal", .value = "true" },
};

    
static const struct driconf_option application_248_options[] = {
    { .name = "vk_wsi_force_swapchain_to_current_extent", .value = "true" },
    { .name = "vk_x11_ignore_suboptimal", .value = "true" },
};

    
static const struct driconf_option application_251_options[] = {
    { .name = "vk_wsi_force_swapchain_to_current_extent", .value = "true" },
    { .name = "vk_x11_ignore_suboptimal", .value = "true" },
};

    
static const struct driconf_option application_254_options[] = {
    { .name = "vk_wsi_force_swapchain_to_current_extent", .value = "true" },
    { .name = "vk_x11_ignore_suboptimal", .value = "true" },
};

    
static const struct driconf_option application_257_options[] = {
    { .name = "mesa_glthread_app_profile", .value = "0" },
};

    
static const struct driconf_option application_259_options[] = {
    { .name = "mesa_glthread_app_profile", .value = "0" },
};

    
static const struct driconf_option application_261_options[] = {
    { .name = "mesa_glthread_app_profile", .value = "0" },
};

    
static const struct driconf_option application_263_options[] = {
    { .name = "adaptive_sync", .value = "false" },
};

    
static const struct driconf_option application_265_options[] = {
    { .name = "adaptive_sync", .value = "false" },
};

    
static const struct driconf_option application_267_options[] = {
    { .name = "adaptive_sync", .value = "false" },
};

    
static const struct driconf_option application_269_options[] = {
    { .name = "adaptive_sync", .value = "false" },
};

    
static const struct driconf_option application_271_options[] = {
    { .name = "adaptive_sync", .value = "false" },
};

    
static const struct driconf_option application_273_options[] = {
    { .name = "adaptive_sync", .value = "false" },
};

    
static const struct driconf_option application_275_options[] = {
    { .name = "adaptive_sync", .value = "false" },
};

    
static const struct driconf_option application_277_options[] = {
    { .name = "adaptive_sync", .value = "false" },
};

    
static const struct driconf_option application_279_options[] = {
    { .name = "adaptive_sync", .value = "false" },
};

    
static const struct driconf_option application_281_options[] = {
    { .name = "adaptive_sync", .value = "false" },
};

    
static const struct driconf_option application_283_options[] = {
    { .name = "adaptive_sync", .value = "false" },
};

    
static const struct driconf_option application_285_options[] = {
    { .name = "adaptive_sync", .value = "false" },
};

    
static const struct driconf_option application_287_options[] = {
    { .name = "adaptive_sync", .value = "false" },
};

    
static const struct driconf_option application_289_options[] = {
    { .name = "adaptive_sync", .value = "false" },
};

    
static const struct driconf_option application_291_options[] = {
    { .name = "adaptive_sync", .value = "false" },
};

    
static const struct driconf_option application_293_options[] = {
    { .name = "adaptive_sync", .value = "false" },
};

    
static const struct driconf_option application_295_options[] = {
    { .name = "adaptive_sync", .value = "false" },
};

    
static const struct driconf_option application_297_options[] = {
    { .name = "adaptive_sync", .value = "false" },
};

    
static const struct driconf_option application_299_options[] = {
    { .name = "adaptive_sync", .value = "false" },
};

    
static const struct driconf_option application_301_options[] = {
    { .name = "adaptive_sync", .value = "false" },
};

    
static const struct driconf_option application_303_options[] = {
    { .name = "adaptive_sync", .value = "false" },
};

    
static const struct driconf_option application_305_options[] = {
    { .name = "adaptive_sync", .value = "false" },
};

    
static const struct driconf_option application_307_options[] = {
    { .name = "adaptive_sync", .value = "false" },
};

    
static const struct driconf_option application_309_options[] = {
    { .name = "adaptive_sync", .value = "false" },
};

    
static const struct driconf_option application_311_options[] = {
    { .name = "adaptive_sync", .value = "false" },
};

    
static const struct driconf_option application_313_options[] = {
    { .name = "adaptive_sync", .value = "false" },
};

    
static const struct driconf_option application_315_options[] = {
    { .name = "adaptive_sync", .value = "false" },
};

    
static const struct driconf_option application_317_options[] = {
    { .name = "adaptive_sync", .value = "false" },
};

    
static const struct driconf_option application_319_options[] = {
    { .name = "adaptive_sync", .value = "false" },
};

    
static const struct driconf_option application_321_options[] = {
    { .name = "adaptive_sync", .value = "false" },
};

    
static const struct driconf_option application_323_options[] = {
    { .name = "adaptive_sync", .value = "false" },
};

    
static const struct driconf_option application_325_options[] = {
    { .name = "adaptive_sync", .value = "false" },
};

    
static const struct driconf_option application_327_options[] = {
    { .name = "adaptive_sync", .value = "false" },
};

    
static const struct driconf_option application_329_options[] = {
    { .name = "mesa_glthread_app_profile", .value = "0" },
    { .name = "vk_x11_override_min_image_count", .value = "2" },
    { .name = "vk_wsi_force_bgra8_unorm_first", .value = "true" },
};

    
static const struct driconf_option application_333_options[] = {
    { .name = "vk_x11_override_min_image_count", .value = "2" },
    { .name = "vk_x11_strict_image_count", .value = "true" },
};

    
static const struct driconf_option application_336_options[] = {
    { .name = "vk_x11_override_min_image_count", .value = "2" },
    { .name = "vk_x11_strict_image_count", .value = "true" },
};

    
static const struct driconf_option application_339_options[] = {
    { .name = "vk_x11_override_min_image_count", .value = "3" },
    { .name = "vk_x11_strict_image_count", .value = "true" },
};

    
static const struct driconf_option application_342_options[] = {
    { .name = "glx_extension_override", .value = "-GLX_OML_swap_method" },
    { .name = "allow_higher_compat_version", .value = "true" },
};

    
static const struct driconf_option application_345_options[] = {
    { .name = "glsl_correct_derivatives_after_discard", .value = "true" },
};

    
static const struct driconf_option application_347_options[] = {
    { .name = "glsl_correct_derivatives_after_discard", .value = "true" },
};

    
static const struct driconf_option application_349_options[] = {
    { .name = "vs_position_always_precise", .value = "true" },
};

    
static const struct driconf_option application_351_options[] = {
    { .name = "vs_position_always_precise", .value = "true" },
};

    
static const struct driconf_option application_353_options[] = {
    { .name = "vs_position_always_precise", .value = "true" },
};

    
static const struct driconf_option application_355_options[] = {
    { .name = "vs_position_always_precise", .value = "true" },
};

    
static const struct driconf_option application_357_options[] = {
    { .name = "glsl_zero_init", .value = "true" },
};

    
static const struct driconf_option application_359_options[] = {
    { .name = "ignore_discard_framebuffer", .value = "true" },
};

    
static const struct driconf_option application_361_options[] = {
    { .name = "vk_x11_strict_image_count", .value = "true" },
};

    
static const struct driconf_option application_363_options[] = {
    { .name = "vk_x11_ensure_min_image_count", .value = "true" },
};

    
static const struct driconf_option application_365_options[] = {
    { .name = "vk_zero_vram", .value = "true" },
};

    
static const struct driconf_option application_367_options[] = {
    { .name = "vk_zero_vram", .value = "true" },
};

    
static const struct driconf_option application_369_options[] = {
    { .name = "vk_zero_vram", .value = "true" },
};

    
static const struct driconf_option application_371_options[] = {
    { .name = "vk_x11_strict_image_count", .value = "true" },
};

    
static const struct driconf_option application_373_options[] = {
    { .name = "vk_x11_strict_image_count", .value = "true" },
};

    
static const struct driconf_option application_375_options[] = {
    { .name = "vk_x11_strict_image_count", .value = "true" },
};

    
static const struct driconf_option application_377_options[] = {
    { .name = "vk_x11_strict_image_count", .value = "true" },
};

    
static const struct driconf_option application_379_options[] = {
    { .name = "vk_x11_strict_image_count", .value = "true" },
};

    
static const struct driconf_option application_381_options[] = {
    { .name = "wgl_require_gdi_compat", .value = "true" },
};


static const struct driconf_application device_1_applications[] = {
    { .name = "Akka Arrh",
      .executable = "Project-A.exe",
      .num_options = 1,
      .options = application_2_options,
    },
    { .name = "Unigine Sanctuary",
      .executable = "Sanctuary",
      .num_options = 3,
      .options = application_4_options,
    },
    { .name = "Unigine Tropics",
      .executable = "Tropics",
      .num_options = 3,
      .options = application_8_options,
    },
    { .name = "Unigine Heaven (32-bit)",
      .executable = "heaven_x86",
      .num_options = 2,
      .options = application_12_options,
    },
    { .name = "Unigine Heaven (64-bit)",
      .executable = "heaven_x64",
      .num_options = 2,
      .options = application_15_options,
    },
    { .name = "Unigine Heaven (Windows)",
      .executable = "heaven.exe",
      .num_options = 2,
      .options = application_18_options,
    },
    { .name = "Unigine Valley (32-bit)",
      .executable = "valley_x86",
      .num_options = 2,
      .options = application_21_options,
    },
    { .name = "Unigine Valley (64-bit)",
      .executable = "valley_x64",
      .num_options = 2,
      .options = application_24_options,
    },
    { .name = "Unigine Valley (Windows)",
      .executable = "valley.exe",
      .num_options = 2,
      .options = application_27_options,
    },
    { .name = "Unigine OilRush (32-bit)",
      .executable = "OilRush_x86",
      .num_options = 2,
      .options = application_30_options,
    },
    { .name = "Unigine OilRush (64-bit)",
      .executable = "OilRush_x64",
      .num_options = 2,
      .options = application_33_options,
    },
    { .name = "Savage 2",
      .executable = "savage2.bin",
      .num_options = 1,
      .options = application_36_options,
    },
    { .name = "Team Fortress 2",
      .executable = "tf_linux64",
      .num_options = 1,
      .options = application_38_options,
    },
    { .name = "Topogun (32-bit)",
      .executable = "topogun32",
      .num_options = 1,
      .options = application_40_options,
    },
    { .name = "Topogun (64-bit)",
      .executable = "topogun64",
      .num_options = 1,
      .options = application_42_options,
    },
    { .name = "Dead Island (incl. Definitive Edition)",
      .executable = "DeadIslandGame",
      .num_options = 2,
      .options = application_44_options,
    },
    { .name = "Dead Island Riptide Definitive Edition",
      .executable = "DeadIslandRiptideGame",
      .num_options = 1,
      .options = application_47_options,
    },
    { .name = "Doom 3: BFG",
      .executable = "Doom3BFG.exe",
      .num_options = 3,
      .options = application_49_options,
    },
    { .name = "Dune: Spice Wars",
      .executable = "D4X.exe",
      .num_options = 1,
      .options = application_53_options,
    },
    { .name = "The Chronicles of Riddick: Assault on Dark Athena",
      .executable = "DarkAthena.exe",
      .num_options = 4,
      .options = application_55_options,
    },
    { .name = "Cursemark",
      .executable = "Cursemark.exe",
      .num_options = 1,
      .options = application_60_options,
    },
    { .name = "Dying Light",
      .executable = "DyingLightGame",
      .num_options = 2,
      .options = application_62_options,
    },
    { .name = "Exanima",
      .executable = "Exanima.exe",
      .num_options = 1,
      .options = application_65_options,
    },
    { .name = "Full Bore",
      .executable = "fullbore",
      .num_options = 1,
      .options = application_67_options,
    },
    { .name = "RAGE (64-bit)",
      .executable = "Rage64.exe",
      .num_options = 2,
      .options = application_69_options,
    },
    { .name = "RAGE (32-bit)",
      .executable = "Rage.exe",
      .num_options = 2,
      .options = application_72_options,
    },
    { .name = "Second Life",
      .executable = "do-not-directly-run-secondlife-bin",
      .num_options = 1,
      .options = application_75_options,
    },
    { .name = "TombRaider 4-5-6 Remastered",
      .executable = "tomb456.exe",
      .num_options = 1,
      .options = application_77_options,
    },
    { .name = "Warsow (32-bit)",
      .executable = "warsow.i386",
      .num_options = 1,
      .options = application_79_options,
    },
    { .name = "Warsow (64-bit)",
      .executable = "warsow.x86_64",
      .num_options = 1,
      .options = application_81_options,
    },
    { .name = "Rust",
      .executable = "rust",
      .num_options = 1,
      .options = application_83_options,
    },
    { .name = "Divinity: Original Sin Enhanced Edition",
      .executable = "EoCApp",
      .num_options = 3,
      .options = application_85_options,
    },
    { .name = "Metro 2033 Redux / Metro Last Night Redux",
      .executable = "metro",
      .num_options = 1,
      .options = application_89_options,
    },
    { .name = "Worms W.M.D",
      .executable = "Worms W.M.Dx64",
      .num_options = 1,
      .options = application_91_options,
    },
    { .name = "Crookz - The Big Heist",
      .executable = "Crookz",
      .num_options = 1,
      .options = application_93_options,
    },
    { .name = "Tropico 5",
      .executable = "Tropico5",
      .num_options = 1,
      .options = application_95_options,
    },
    { .name = "Faster than Light (32-bit)",
      .executable = "FTL.x86",
      .num_options = 1,
      .options = application_97_options,
    },
    { .name = "Faster than Light (64-bit)",
      .executable = "FTL.amd64",
      .num_options = 1,
      .options = application_99_options,
    },
    { .name = "Final Fantasy VIII: Remastered",
      .executable = "FFVIII.exe",
      .num_options = 1,
      .options = application_101_options,
    },
    { .name = "Lethis - Path Of Progress",
      .executable = "Lethis-Path_Of_Progress.exe",
      .num_options = 2,
      .options = application_103_options,
    },
    { .name = "SNK HEROINES Tag Team Frenzy",
      .executable = "SNKHEROINES.exe",
      .num_options = 1,
      .options = application_106_options,
    },
    { .name = "Metal Slug XX",
      .executable = "MSXX_main.exe",
      .num_options = 1,
      .options = application_108_options,
    },
    { .name = "The Culling",
      .executable = "Victory",
      .num_options = 1,
      .options = application_110_options,
    },
    { .name = "Spec Ops: The Line (32-bit)",
      .executable = "specops.i386",
      .num_options = 1,
      .options = application_112_options,
    },
    { .name = "Spec Ops: The Line (64-bit)",
      .executable = "specops",
      .num_options = 1,
      .options = application_114_options,
    },
    { .name = "Interstellar Rift",
      .executable = "IR.exe",
      .num_options = 2,
      .options = application_116_options,
    },
    { .name = "Jamestown+",
      .executable = "JamestownPlus.exe",
      .num_options = 1,
      .options = application_119_options,
    },
    { .name = "Kerbal Space Program (32-bit)",
      .executable = "KSP.x86",
      .num_options = 1,
      .options = application_121_options,
    },
    { .name = "Kerbal Space Program (64-bit)",
      .executable = "KSP.x86_64",
      .num_options = 1,
      .options = application_123_options,
    },
    { .name = "Luna Sky",
      .executable = "lunasky",
      .num_options = 2,
      .options = application_125_options,
    },
    { .name = "MDK2 HD",
      .executable = "mdk2hd.exe",
      .num_options = 1,
      .options = application_128_options,
    },
    { .name = "Rocket League",
      .executable = "RocketLeague",
      .num_options = 1,
      .options = application_130_options,
    },
    { .name = "The Witcher 2",
      .executable = "witcher2",
      .num_options = 1,
      .options = application_132_options,
    },
    { .name = "Unreal 4 Editor",
      .executable = "UE4Editor",
      .num_options = 1,
      .options = application_134_options,
    },
    { .name = "Observer",
      .executable = "TheObserver-Linux-Shipping",
      .num_options = 1,
      .options = application_136_options,
    },
    { .name = "Pixel Game Maker MV",
      .executable = "player.exe",
      .num_options = 1,
      .options = application_138_options,
    },
    { .name = "Eden Gamma",
      .executable = "EdenGammaGame-0.3.0.2.exe",
      .num_options = 1,
      .options = application_140_options,
    },
    { .name = "Steamroll",
      .executable = "Steamroll-Linux-Shipping",
      .num_options = 1,
      .options = application_142_options,
    },
    { .name = "Refunct",
      .executable = "Refunct-Linux-Shipping",
      .num_options = 1,
      .options = application_144_options,
    },
    { .name = "We Happy Few",
      .executable = "GlimpseGame",
      .num_options = 1,
      .options = application_146_options,
    },
    { .name = "Google Earth VR",
      .executable = "Earth.exe",
      .num_options = 2,
      .options = application_148_options,
    },
    { .name = "Champions of Regnum",
      .executable = "game",
      .num_options = 1,
      .options = application_151_options,
    },
    { .name = "Wolfenstein The Old Blood",
      .executable = "WolfOldBlood_x64.exe",
      .num_options = 1,
      .options = application_153_options,
    },
    { .name = "ARMA 3",
      .executable = "arma3.x86_64",
      .num_options = 1,
      .options = application_155_options,
    },
    { .name = "Epic Games Launcher",
      .executable = "EpicGamesLauncher.exe",
      .num_options = 1,
      .options = application_157_options,
    },
    { .name = "GpuTest",
      .executable = "GpuTest",
      .num_options = 1,
      .options = application_159_options,
    },
    { .name = "Curse of the Dead Gods",
      .executable = "Curse of the Dead Gods.exe",
      .num_options = 1,
      .options = application_161_options,
    },
    { .name = "GRID Autosport",
      .executable = "GridAutosport",
      .num_options = 1,
      .options = application_163_options,
    },
    { .name = "DIRT: Showdown",
      .executable = "dirt.i386",
      .num_options = 1,
      .options = application_165_options,
    },
    { .name = "DiRT Rally",
      .executable = "DirtRally",
      .num_options = 1,
      .options = application_167_options,
    },
    { .name = "Foundation",
      .executable = "foundation.exe",
      .num_options = 1,
      .options = application_169_options,
    },
    { .name = "Homerun Clash",
      .executable = "com.haegin.homerunclash",
      .num_options = 1,
      .options = application_171_options,
    },
    { .name = "The Spirit and The Mouse",
      .executable = "TheSpiritAndTheMouse.exe",
      .num_options = 1,
      .options = application_173_options,
    },
    { .name = "Penumbra: Overture (Windows)",
      .executable = "Penumbra.exe",
      .num_options = 2,
      .options = application_175_options,
    },
    { .name = "Penumbra: Overture",
      .executable = "penumbra.bin",
      .num_options = 2,
      .options = application_178_options,
    },
    { .name = "SPECviewperf13",
      .executable = "viewperf",
      .num_options = 10,
      .options = application_181_options,
    },
    { .name = "Dead-Cells",
      .executable = "com.playdigious.deadcells.mobile",
      .num_options = 1,
      .options = application_192_options,
    },
    { .name = "Little Inferno",
      .executable = "LittleInferno.bin.x86_64",
      .num_options = 1,
      .options = application_194_options,
    },
    { .name = "Little Inferno (32-bit)",
      .executable = "LittleInferno.bin.x86",
      .num_options = 1,
      .options = application_196_options,
    },
    { .name = "Teardown",
      .executable = "teardown.exe",
      .num_options = 2,
      .options = application_198_options,
    },
    { .name = "Golf With Your Friends",
      .executable = "Golf With Your Friends.x86_64",
      .num_options = 1,
      .options = application_201_options,
    },
    { .name = "Cossacks 3",
      .executable = "cossacks.exe",
      .num_options = 1,
      .options = application_203_options,
    },
    { .name = "Kaiju-A-Gogo",
      .executable = "kaiju.exe",
      .num_options = 1,
      .options = application_205_options,
    },
    { .name = "Captain Lycop: Invasion of the Heters (Wine)",
      .executable = "lycop.exe",
      .num_options = 1,
      .options = application_207_options,
    },
    { .name = "Captain Lycop: Invasion of the Heters",
      .executable = "lycop",
      .num_options = 1,
      .options = application_209_options,
    },
    { .name = "Joe Danger (Wine)",
      .executable = "JoeDanger.exe",
      .num_options = 1,
      .options = application_211_options,
    },
    { .name = "Joe Danger 2 (Wine)",
      .executable = "JoeDanger2.exe",
      .num_options = 1,
      .options = application_213_options,
    },
    { .name = "Investigation Stories : gunsound (Wine)",
      .executable = "gunsound.exe",
      .num_options = 1,
      .options = application_215_options,
    },
    { .name = "Blockland (Wine)",
      .executable = "Blockland.exe",
      .num_options = 1,
      .options = application_217_options,
    },
    { .name = "BETA CAE Systems - GL detect tool",
      .executable = "detect_opengl_tool",
      .num_options = 1,
      .options = application_219_options,
    },
    { .name = "BETA CAE Systems - ANSA",
      .executable = "ansa_linux_x86_64",
      .num_options = 2,
      .options = application_221_options,
    },
    { .name = "BETA CAE Systems - META",
      .executable = "meta_post_x86_64",
      .num_options = 1,
      .options = application_224_options,
    },
    { .name = "Mari",
      .executable_regexp = "Mari[0-9]+[.][0-9]+v[0-9]+",
      .num_options = 1,
      .options = application_226_options,
    },
    { .name = "Discovery Studio 2020",
      .executable = "DiscoveryStudio2020-bin",
      .num_options = 1,
      .options = application_228_options,
    },
    { .name = "Abaqus",
      .executable = "ABQcaeK",
      .num_options = 2,
      .options = application_230_options,
    },
    { .name = "Maya",
      .executable = "maya.bin",
      .num_options = 1,
      .options = application_233_options,
    },
    { .name = "SD Gundam G Generation Cross Rays",
      .executable = "togg.exe",
      .num_options = 1,
      .options = application_235_options,
    },
    { .name = "FINAL FANTASY XI",
      .executable = "pol.exe",
      .num_options = 1,
      .options = application_237_options,
    },
    { .name = "Talos Principle",
      .executable = "Talos",
      .num_options = 2,
      .options = application_239_options,
    },
    { .name = "Talos Principle (Unrestricted)",
      .executable = "Talos_Unrestricted",
      .num_options = 2,
      .options = application_242_options,
    },
    { .name = "Talos Principle VR",
      .executable = "Talos_VR",
      .num_options = 2,
      .options = application_245_options,
    },
    { .name = "Talos Principle VR (Unrestricted)",
      .executable = "Talos_Unrestricted_VR",
      .num_options = 2,
      .options = application_248_options,
    },
    { .name = "Serious Sam Fusion",
      .executable = "Sam2017",
      .num_options = 2,
      .options = application_251_options,
    },
    { .name = "Serious Sam Fusion (Unrestricted)",
      .executable = "Sam2017_Unrestricted",
      .num_options = 2,
      .options = application_254_options,
    },
    { .name = "DeusExMD",
      .executable = "DeusExMD",
      .num_options = 1,
      .options = application_257_options,
    },
    { .name = "F1 2015",
      .executable = "F12015",
      .num_options = 1,
      .options = application_259_options,
    },
    { .name = "KWin Wayland",
      .executable = "kwin_wayland",
      .num_options = 1,
      .options = application_261_options,
    },
    { .name = "gnome-shell",
      .executable = "gnome-shell",
      .num_options = 1,
      .options = application_263_options,
    },
    { .name = "Desktop — Plasma",
      .executable = "plasmashell",
      .num_options = 1,
      .options = application_265_options,
    },
    { .name = "budgie-wm",
      .executable = "budgie-wm",
      .num_options = 1,
      .options = application_267_options,
    },
    { .name = "kwin_x11",
      .executable = "kwin_x11",
      .num_options = 1,
      .options = application_269_options,
    },
    { .name = "ksmserver-logout-greeter",
      .executable = "ksmserver-logout-greeter",
      .num_options = 1,
      .options = application_271_options,
    },
    { .name = "ksmserver-switchuser-greeter",
      .executable = "ksmserver-switchuser-greeter",
      .num_options = 1,
      .options = application_273_options,
    },
    { .name = "kscreenlocker_greet",
      .executable = "kscreenlocker_greet",
      .num_options = 1,
      .options = application_275_options,
    },
    { .name = "startplasma",
      .executable = "startplasma",
      .num_options = 1,
      .options = application_277_options,
    },
    { .name = "sddm-greeter",
      .executable = "sddm-greeter",
      .num_options = 1,
      .options = application_279_options,
    },
    { .name = "krunner",
      .executable = "krunner",
      .num_options = 1,
      .options = application_281_options,
    },
    { .name = "spectacle",
      .executable = "spectacle",
      .num_options = 1,
      .options = application_283_options,
    },
    { .name = "marco",
      .executable = "marco",
      .num_options = 1,
      .options = application_285_options,
    },
    { .name = "compton",
      .executable = "compton",
      .num_options = 1,
      .options = application_287_options,
    },
    { .name = "picom",
      .executable = "picom",
      .num_options = 1,
      .options = application_289_options,
    },
    { .name = "xfwm4",
      .executable = "xfwm4",
      .num_options = 1,
      .options = application_291_options,
    },
    { .name = "Enlightenment",
      .executable = "enlightenment",
      .num_options = 1,
      .options = application_293_options,
    },
    { .name = "mutter",
      .executable = "mutter",
      .num_options = 1,
      .options = application_295_options,
    },
    { .name = "muffin",
      .executable = "muffin",
      .num_options = 1,
      .options = application_297_options,
    },
    { .name = "cinnamon",
      .executable = "cinnamon",
      .num_options = 1,
      .options = application_299_options,
    },
    { .name = "compiz",
      .executable = "compiz",
      .num_options = 1,
      .options = application_301_options,
    },
    { .name = "Firefox",
      .executable = "firefox",
      .num_options = 1,
      .options = application_303_options,
    },
    { .name = "Firefox ESR",
      .executable = "firefox-esr",
      .num_options = 1,
      .options = application_305_options,
    },
    { .name = "Chromium",
      .executable = "chromium",
      .num_options = 1,
      .options = application_307_options,
    },
    { .name = "Google Chrome",
      .executable = "chrome",
      .num_options = 1,
      .options = application_309_options,
    },
    { .name = "Iceweasel",
      .executable = "iceweasel",
      .num_options = 1,
      .options = application_311_options,
    },
    { .name = "Epiphany",
      .executable = "epiphany",
      .num_options = 1,
      .options = application_313_options,
    },
    { .name = "Konqueror",
      .executable = "konqueror",
      .num_options = 1,
      .options = application_315_options,
    },
    { .name = "Falkon",
      .executable = "falkon",
      .num_options = 1,
      .options = application_317_options,
    },
    { .name = "Seamonkey",
      .executable = "seamonkey",
      .num_options = 1,
      .options = application_319_options,
    },
    { .name = "Waterfox",
      .executable = "waterfox",
      .num_options = 1,
      .options = application_321_options,
    },
    { .name = "VLC Media Player",
      .executable = "vlc",
      .num_options = 1,
      .options = application_323_options,
    },
    { .name = "Totem",
      .executable = "totem",
      .num_options = 1,
      .options = application_325_options,
    },
    { .name = "Dragon Player",
      .executable = "dragon",
      .num_options = 1,
      .options = application_327_options,
    },
    { .name = "gfxbench",
      .executable = "testfw_app",
      .num_options = 3,
      .options = application_329_options,
    },
    { .name = "Rainbow Six Siege (Vulkan)",
      .executable = "RainbowSix_Vulkan.exe",
      .num_options = 2,
      .options = application_333_options,
    },
    { .name = "Rainbow Six Extraction (Wine)",
      .executable = "R6-Extraction.exe",
      .num_options = 2,
      .options = application_336_options,
    },
    { .name = "Hades",
      .executable = "Hades.exe",
      .num_options = 2,
      .options = application_339_options,
    },
    { .name = "Brink",
      .executable = "brink.exe",
      .num_options = 2,
      .options = application_342_options,
    },
    { .name = "Enter The Gungeon (32 bits)",
      .executable = "EtG.x86",
      .num_options = 1,
      .options = application_345_options,
    },
    { .name = "Enter The Gungeon (64 bits)",
      .executable = "EtG.x86_64",
      .num_options = 1,
      .options = application_347_options,
    },
    { .name = "Assault Android Cactus (32-bit)",
      .executable = "cactus_demo.x86",
      .num_options = 1,
      .options = application_349_options,
    },
    { .name = "Assault Android Cactus (64-bit)",
      .executable = "cactus_demo.x86_64",
      .num_options = 1,
      .options = application_351_options,
    },
    { .name = "Assault Android Cactus (32-bit)",
      .executable = "cactus.x86",
      .num_options = 1,
      .options = application_353_options,
    },
    { .name = "Assault Android Cactus (64-bit)",
      .executable = "cactus.x86_64",
      .num_options = 1,
      .options = application_355_options,
    },
    { .name = "Limbo",
      .executable = "limbo",
      .num_options = 1,
      .options = application_357_options,
    },
    { .name = "Genshin Impact",
      .executable = "com.miHoYo.GenshinImpact",
      .num_options = 1,
      .options = application_359_options,
    },
    { .name = "DOOM",
      .executable = "DOOMx64vk.exe",
      .num_options = 1,
      .options = application_361_options,
    },
    { .name = "DOOMEternal",
      .executable = "DOOMEternalx64vk.exe",
      .num_options = 1,
      .options = application_363_options,
    },
    { .name = "Path of Exile",
      .executable = "PathOfExile_x64Steam.exe",
      .num_options = 1,
      .options = application_365_options,
    },
    { .name = "Path of Exile",
      .executable = "PathOfExileSteam.exe",
      .num_options = 1,
      .options = application_367_options,
    },
    { .name = "X4 Foundations",
      .executable = "X4",
      .num_options = 1,
      .options = application_369_options,
    },
    { .name = "Wolfenstein: Youngblood(x64vk)",
      .executable = "Youngblood_x64vk.exe",
      .num_options = 1,
      .options = application_371_options,
    },
    { .name = "Wolfenstein II: The New Colossus",
      .executable = "NewColossus_x64vk.exe",
      .num_options = 1,
      .options = application_373_options,
    },
    { .name = "Metro: Exodus",
      .application_name_match = "metroexodus",
      .num_options = 1,
      .options = application_375_options,
    },
    { .name = "Atlas Fallen",
      .executable = "AtlasFallen (VK).exe",
      .num_options = 1,
      .options = application_377_options,
    },
    { .name = "Detroit Become Human",
      .application_name_match = "DetroitBecomeHuman",
      .num_options = 1,
      .options = application_379_options,
    },
    { .name = "文香白板",
      .executable = "WxBoardMain.exe",
      .num_options = 1,
      .options = application_381_options,
    },
};

static const struct driconf_device device_1 = {
    .num_engines = 1,
    .engines = device_1_engines,
    .num_applications = 164,
    .applications = device_1_applications,
};
    
static const struct driconf_option engine_505_options[] = {
    { .name = "vk_zero_vram", .value = "true" },
    { .name = "radv_disable_aniso_single_level", .value = "true" },
    { .name = "radv_disable_trunc_coord", .value = "true" },
    { .name = "radv_cooperative_matrix2_nv", .value = "true" },
    { .name = "radv_device_coherent_memory", .value = "true" },
    { .name = "radv_enable_float16_gfx8", .value = "true" },
};

    
static const struct driconf_option engine_512_options[] = {
    { .name = "radv_disable_aniso_single_level", .value = "true" },
    { .name = "radv_disable_trunc_coord", .value = "true" },
};

    
static const struct driconf_option engine_515_options[] = {
    { .name = "radv_disable_aniso_single_level", .value = "true" },
};

    
static const struct driconf_option engine_517_options[] = {
    { .name = "vk_zero_vram", .value = "true" },
    { .name = "radv_disable_tc_compat_htile_general", .value = "true" },
    { .name = "radv_app_layer", .value = "quanticdream" },
};

    
static const struct driconf_option engine_521_options[] = {
    { .name = "radv_flush_before_query_copy", .value = "true" },
    { .name = "radv_flush_before_timestamp_write", .value = "true" },
    { .name = "vk_zero_vram", .value = "true" },
};

    
static const struct driconf_option engine_525_options[] = {
    { .name = "radv_app_layer", .value = "ue5" },
};

    
static const struct driconf_option engine_527_options[] = {
    { .name = "radv_tex_non_uniform", .value = "true" },
};

    
static const struct driconf_option engine_529_options[] = {
    { .name = "vk_wsi_disable_unordered_submits", .value = "true" },
};


static const struct driconf_engine device_385_engines[] = {
    { .engine_name_match = "vkd3d",
      .num_options = 6,
      .options = engine_505_options,
    },
    { .engine_name_match = "DXVK",
      .num_options = 2,
      .options = engine_512_options,
    },
    { .engine_name_match = "mesa zink",
      .num_options = 1,
      .options = engine_515_options,
    },
    { .engine_name_match = "Quantic Dream Engine",
      .num_options = 3,
      .options = engine_517_options,
    },
    { .engine_name_match = "^UnrealEngine",
      .num_options = 3,
      .options = engine_521_options,
    },
    { .engine_name_match = "UnrealEngine5.*",
      .engine_versions = "0:8",
      .num_options = 1,
      .options = engine_525_options,
    },
    { .engine_name_match = "DXVK_NvRemix",
      .num_options = 1,
      .options = engine_527_options,
    },
    { .engine_name_match = "GTK",
      .engine_versions = "16777216:16859138",
      .num_options = 1,
      .options = engine_529_options,
    },
};

    
static const struct driconf_option application_386_options[] = {
    { .name = "radv_report_llvm9_version_string", .value = "true" },
    { .name = "radv_gfx12_hiz_wa", .value = "partial" },
};

    
static const struct driconf_option application_389_options[] = {
    { .name = "radv_split_fma", .value = "true" },
};

    
static const struct driconf_option application_391_options[] = {
    { .name = "radv_enable_mrt_output_nan_fixup", .value = "true" },
    { .name = "radv_app_layer", .value = "rage2" },
    { .name = "vk_zero_vram", .value = "true" },
};

    
static const struct driconf_option application_395_options[] = {
    { .name = "radv_disable_shrink_image_store", .value = "true" },
    { .name = "vk_zero_vram", .value = "true" },
};

    
static const struct driconf_option application_398_options[] = {
    { .name = "radv_override_uniform_offset_alignment", .value = "16" },
    { .name = "radv_disable_shrink_image_store", .value = "true" },
};

    
static const struct driconf_option application_401_options[] = {
    { .name = "radv_no_dynamic_bounds", .value = "true" },
};

    
static const struct driconf_option application_403_options[] = {
    { .name = "vk_zero_vram", .value = "true" },
};

    
static const struct driconf_option application_405_options[] = {
    { .name = "vk_zero_vram", .value = "true" },
    { .name = "radv_disable_dcc_stores", .value = "true" },
    { .name = "radv_emulate_rt", .value = "true" },
    { .name = "radv_enable_float16_gfx8", .value = "true" },
    { .name = "radv_gfx12_hiz_wa", .value = "partial" },
};

    
static const struct driconf_option application_411_options[] = {
    { .name = "radv_disable_dcc", .value = "true" },
};

    
static const struct driconf_option application_413_options[] = {
    { .name = "vk_zero_vram", .value = "true" },
    { .name = "radv_disable_dcc_stores", .value = "true" },
    { .name = "radv_no_implicit_varying_subgroup_size", .value = "true" },
    { .name = "radv_force_nan_preserve_min_max", .value = "true" },
};

    
static const struct driconf_option application_418_options[] = {
    { .name = "radv_disable_dcc", .value = "true" },
    { .name = "radv_disable_aniso_single_level", .value = "true" },
};

    
static const struct driconf_option application_421_options[] = {
    { .name = "radv_disable_sinking_load_input_fs", .value = "true" },
};

    
static const struct driconf_option application_423_options[] = {
    { .name = "radv_disable_sinking_load_input_fs", .value = "true" },
};

    
static const struct driconf_option application_425_options[] = {
    { .name = "vk_zero_vram", .value = "true" },
};

    
static const struct driconf_option application_427_options[] = {
    { .name = "radv_enable_unified_heap_on_apu", .value = "true" },
    { .name = "radv_hide_rebar_on_dgpu", .value = "true" },
    { .name = "vk_zero_vram", .value = "true" },
    { .name = "radv_disable_dcc_mips", .value = "true" },
    { .name = "radv_gfx12_hiz_wa", .value = "partial" },
};

    
static const struct driconf_option application_433_options[] = {
    { .name = "radv_app_layer", .value = "metroexodus" },
};

    
static const struct driconf_option application_435_options[] = {
    { .name = "radv_rt_wave64", .value = "true" },
};

    
static const struct driconf_option application_437_options[] = {
    { .name = "radv_ssbo_non_uniform", .value = "true" },
};

    
static const struct driconf_option application_439_options[] = {
    { .name = "radv_ssbo_non_uniform", .value = "true" },
};

    
static const struct driconf_option application_441_options[] = {
    { .name = "radv_disable_ngg_gs", .value = "true" },
};

    
static const struct driconf_option application_443_options[] = {
    { .name = "radv_disable_dcc", .value = "true" },
};

    
static const struct driconf_option application_445_options[] = {
    { .name = "vk_zero_vram", .value = "true" },
};

    
static const struct driconf_option application_447_options[] = {
    { .name = "radv_gfx12_hiz_wa", .value = "partial" },
};

    
static const struct driconf_option application_449_options[] = {
    { .name = "radv_gfx12_hiz_wa", .value = "partial" },
};

    
static const struct driconf_option application_451_options[] = {
    { .name = "radv_gfx12_hiz_wa", .value = "partial" },
    { .name = "radv_app_layer", .value = "strange_brigade" },
};

    
static const struct driconf_option application_454_options[] = {
    { .name = "radv_gfx12_hiz_wa", .value = "partial" },
};

    
static const struct driconf_option application_456_options[] = {
    { .name = "radv_gfx12_hiz_wa", .value = "partial" },
    { .name = "radv_app_layer", .value = "strange_brigade" },
    { .name = "vk_wsi_disable_unordered_submits", .value = "true" },
};

    
static const struct driconf_option application_460_options[] = {
    { .name = "radv_gfx12_hiz_wa", .value = "partial" },
};

    
static const struct driconf_option application_462_options[] = {
    { .name = "radv_gfx12_hiz_wa", .value = "partial" },
    { .name = "radv_wait_for_vm_map_updates", .value = "true" },
};

    
static const struct driconf_option application_465_options[] = {
    { .name = "radv_wait_for_vm_map_updates", .value = "true" },
    { .name = "radv_force_64_byte_sampled_image", .value = "true" },
};

    
static const struct driconf_option application_468_options[] = {
    { .name = "radv_gfx12_hiz_wa", .value = "partial" },
};

    
static const struct driconf_option application_470_options[] = {
    { .name = "radv_gfx12_hiz_wa", .value = "partial" },
};

    
static const struct driconf_option application_472_options[] = {
    { .name = "radv_gfx12_hiz_wa", .value = "partial" },
};

    
static const struct driconf_option application_474_options[] = {
    { .name = "radv_prefer_2d_swizzle_for_3d_storage", .value = "true" },
};

    
static const struct driconf_option application_476_options[] = {
    { .name = "vk_zero_vram", .value = "true" },
};

    
static const struct driconf_option application_478_options[] = {
    { .name = "vk_zero_vram", .value = "true" },
};

    
static const struct driconf_option application_480_options[] = {
    { .name = "vk_zero_vram", .value = "true" },
};

    
static const struct driconf_option application_482_options[] = {
    { .name = "vk_zero_vram", .value = "true" },
};

    
static const struct driconf_option application_484_options[] = {
    { .name = "vk_zero_vram", .value = "true" },
};

    
static const struct driconf_option application_486_options[] = {
    { .name = "vk_zero_vram", .value = "true" },
};

    
static const struct driconf_option application_488_options[] = {
    { .name = "vk_zero_vram", .value = "true" },
};

    
static const struct driconf_option application_490_options[] = {
    { .name = "vk_zero_vram", .value = "true" },
};

    
static const struct driconf_option application_492_options[] = {
    { .name = "vk_zero_vram", .value = "true" },
};

    
static const struct driconf_option application_494_options[] = {
    { .name = "vk_zero_vram", .value = "true" },
};

    
static const struct driconf_option application_496_options[] = {
    { .name = "radv_app_layer", .value = "no_mans_sky" },
    { .name = "radv_force_exact_glsl_fma", .value = "true" },
};

    
static const struct driconf_option application_499_options[] = {
    { .name = "radv_force_64_byte_sampled_image", .value = "true" },
};

    
static const struct driconf_option application_501_options[] = {
    { .name = "radv_app_layer", .value = "gfxbench5" },
};

    
static const struct driconf_option application_503_options[] = {
    { .name = "radv_disable_binning_gfx11", .value = "true" },
};


static const struct driconf_application device_385_applications[] = {
    { .name = "Shadow Of The Tomb Raider (Native)",
      .application_name_match = "ShadowOfTheTomb",
      .num_options = 2,
      .options = application_386_options,
    },
    { .name = "Shadow Of The Tomb Raider (DX11/DX12)",
      .application_name_match = "SOTTR.exe",
      .num_options = 1,
      .options = application_389_options,
    },
    { .name = "RAGE 2",
      .application_name_match = "Rage 2",
      .num_options = 3,
      .options = application_391_options,
    },
    { .name = "The Surge 2",
      .application_name_match = "Fledge",
      .num_options = 2,
      .options = application_395_options,
    },
    { .name = "World War Z (and World War Z: Aftermath)",
      .application_name_match = "WWZ|wwz",
      .num_options = 2,
      .options = application_398_options,
    },
    { .name = "DOOM VFR",
      .application_name_match = "DOOM_VFR",
      .num_options = 1,
      .options = application_401_options,
    },
    { .name = "DOOM Eternal",
      .application_name_match = "DOOMEternal",
      .num_options = 1,
      .options = application_403_options,
    },
    { .name = "Indiana Jones: The Great Circle",
      .application_name_match = "TheGreatCircle",
      .num_options = 5,
      .options = application_405_options,
    },
    { .name = "DOOM (2016)",
      .application_name_match = "DOOM$",
      .num_options = 1,
      .options = application_411_options,
    },
    { .name = "DOOM: The Dark Ages",
      .application_name_match = "DOOMTheDarkAges",
      .num_options = 4,
      .options = application_413_options,
    },
    { .name = "Wolfenstein II",
      .application_name_match = "Wolfenstein II The New Colossus",
      .num_options = 2,
      .options = application_418_options,
    },
    { .name = "Grid Autosport",
      .application_name_match = "GRIDAutosport_avx.exe",
      .num_options = 1,
      .options = application_421_options,
    },
    { .name = "Grid Autosport",
      .application_name_match = "GRIDAutosport.exe",
      .num_options = 1,
      .options = application_423_options,
    },
    { .name = "Hammerting",
      .application_name_match = "TDTD",
      .num_options = 1,
      .options = application_425_options,
    },
    { .name = "RDR2",
      .application_name_match = "Red Dead Redemption 2",
      .num_options = 5,
      .options = application_427_options,
    },
    { .name = "Metro Exodus (Linux native)",
      .application_name_match = "metroexodus",
      .num_options = 1,
      .options = application_433_options,
    },
    { .name = "Hellblade: Senua's Sacrifice",
      .application_name_match = "HellbladeGame-Win64-Shipping.exe",
      .num_options = 1,
      .options = application_435_options,
    },
    { .name = "Crysis 2 Remastered",
      .executable = "Crysis2Remastered.exe",
      .num_options = 1,
      .options = application_437_options,
    },
    { .name = "Crysis 3 Remastered",
      .executable = "Crysis3Remastered.exe",
      .num_options = 1,
      .options = application_439_options,
    },
    { .name = "Persona 3 Reload",
      .application_name_match = "P3R.exe",
      .num_options = 1,
      .options = application_441_options,
    },
    { .name = "WWE 2k23",
      .application_name_match = "WWE2K23_x64.exe",
      .num_options = 1,
      .options = application_443_options,
    },
    { .name = "GPUScore: Breaking Limit",
      .application_name_match = "GPUScoreVulkan",
      .num_options = 1,
      .options = application_445_options,
    },
    { .name = "DiRT4",
      .application_name_match = "[Dd]i[Rr][Tt]4",
      .num_options = 1,
      .options = application_447_options,
    },
    { .name = "Strange Brigade (DX12)",
      .application_name_match = "StrangeBrigade_DX12.exe",
      .num_options = 1,
      .options = application_449_options,
    },
    { .name = "Strange Brigade (VK)",
      .application_name_match = "Strange",
      .num_options = 2,
      .options = application_451_options,
    },
    { .name = "Sniper Elite 5 (DX12)",
      .application_name_match = "sniper5_dx12.exe",
      .num_options = 1,
      .options = application_454_options,
    },
    { .name = "Sniper Elite 5 (VK)",
      .application_name_match = "Sniper5",
      .num_options = 3,
      .options = application_456_options,
    },
    { .name = "Counter Strike 2",
      .application_name_match = "csgo",
      .num_options = 1,
      .options = application_460_options,
    },
    { .name = "Forza Horizon 5",
      .application_name_match = "ForzaHorizon5.exe",
      .num_options = 2,
      .options = application_462_options,
    },
    { .name = "Forza Horizon 6",
      .application_name_match = "forzahorizon6.exe",
      .num_options = 2,
      .options = application_465_options,
    },
    { .name = "Far Cry 2",
      .application_name_match = "FarCry2.exe",
      .num_options = 1,
      .options = application_468_options,
    },
    { .name = "DiRT Rally 2.0",
      .application_name_match = "dirtrally2.exe",
      .num_options = 1,
      .options = application_470_options,
    },
    { .name = "Valheim",
      .application_name_match = "Valheim",
      .num_options = 1,
      .options = application_472_options,
    },
    { .name = "The Last of Us Part I",
      .application_name_match = "tlou-i.exe",
      .num_options = 1,
      .options = application_474_options,
    },
    { .name = "Black Geyser: Couriers of Darkness",
      .executable = "BlackGeyser.x86_64",
      .num_options = 1,
      .options = application_476_options,
    },
    { .name = "7 Days to Die",
      .executable = "7DaysToDie.x86_64",
      .num_options = 1,
      .options = application_478_options,
    },
    { .name = "7 Days to Die",
      .executable = "7DaysToDie.x86",
      .num_options = 1,
      .options = application_480_options,
    },
    { .name = "any runner executable",
      .executable = "runner",
      .num_options = 1,
      .options = application_482_options,
    },
    { .name = "American Truck Simulator",
      .executable = "amtrucks",
      .num_options = 1,
      .options = application_484_options,
    },
    { .name = "Counter-Strike Global Offensive",
      .executable = "csgo_linux64",
      .num_options = 1,
      .options = application_486_options,
    },
    { .name = "Exanima",
      .executable = "Exanima.exe",
      .num_options = 1,
      .options = application_488_options,
    },
    { .name = "Rocket League",
      .executable = "RocketLeague",
      .num_options = 1,
      .options = application_490_options,
    },
    { .name = "Crystal Project",
      .executable = "Crystal Project.bin.x86_64",
      .num_options = 1,
      .options = application_492_options,
    },
    { .name = "Enshrouded",
      .executable = "enshrouded.exe",
      .num_options = 1,
      .options = application_494_options,
    },
    { .name = "No Man's Sky",
      .application_name_match = "No Man's Sky",
      .num_options = 2,
      .options = application_496_options,
    },
    { .name = "Crimson Desert",
      .application_name_match = "CrimsonDesert.exe",
      .num_options = 1,
      .options = application_499_options,
    },
    { .name = "GFXBench 5.0",
      .executable = "testfw_app",
      .num_options = 1,
      .options = application_501_options,
    },
    { .name = "Sunset Overdrive",
      .executable = "Sunset.exe",
      .num_options = 1,
      .options = application_503_options,
    },
};

static const struct driconf_device device_385 = {
    .driver = "radv",
    .num_engines = 8,
    .engines = device_385_engines,
    .num_applications = 48,
    .applications = device_385_applications,
};
    
static const struct driconf_option engine_560_options[] = {
    { .name = "vk_wsi_disable_unordered_submits", .value = "true" },
};

    
static const struct driconf_option engine_562_options[] = {
    { .name = "no_16bit", .value = "true" },
};


static const struct driconf_engine device_531_engines[] = {
    { .engine_name_match = "GTK",
      .engine_versions = "16777216:16859138",
      .num_options = 1,
      .options = engine_560_options,
    },
    { .engine_name_match = "mesa zink",
      .num_options = 1,
      .options = engine_562_options,
    },
};

    
static const struct driconf_option application_532_options[] = {
    { .name = "anv_assume_full_subgroups", .value = "32" },
};

    
static const struct driconf_option application_534_options[] = {
    { .name = "hasvk_report_vk_1_3_version", .value = "true" },
};

    
static const struct driconf_option application_536_options[] = {
    { .name = "anv_assume_full_subgroups", .value = "32" },
};

    
static const struct driconf_option application_538_options[] = {
    { .name = "anv_assume_full_subgroups", .value = "32" },
};

    
static const struct driconf_option application_540_options[] = {
    { .name = "limit_trig_input_range", .value = "true" },
};

    
static const struct driconf_option application_542_options[] = {
    { .name = "limit_trig_input_range", .value = "true" },
};

    
static const struct driconf_option application_544_options[] = {
    { .name = "anv_sample_mask_out_opengl_behaviour", .value = "true" },
};

    
static const struct driconf_option application_546_options[] = {
    { .name = "limit_trig_input_range", .value = "true" },
};

    
static const struct driconf_option application_548_options[] = {
    { .name = "limit_trig_input_range", .value = "true" },
};

    
static const struct driconf_option application_550_options[] = {
    { .name = "limit_trig_input_range", .value = "true" },
};

    
static const struct driconf_option application_552_options[] = {
    { .name = "vk_lower_terminate_to_discard", .value = "true" },
};

    
static const struct driconf_option application_554_options[] = {
    { .name = "limit_trig_input_range", .value = "true" },
};

    
static const struct driconf_option application_556_options[] = {
    { .name = "limit_trig_input_range", .value = "true" },
};

    
static const struct driconf_option application_558_options[] = {
    { .name = "limit_trig_input_range", .value = "true" },
};


static const struct driconf_application device_531_applications[] = {
    { .name = "Aperture Desk Job",
      .executable = "deskjob",
      .num_options = 1,
      .options = application_532_options,
    },
    { .name = "Brawlhalla",
      .executable = "BrawlhallaGame.exe",
      .num_options = 1,
      .options = application_534_options,
    },
    { .name = "Detroit: Become Human",
      .executable = "DetroitBecomeHuman.exe",
      .num_options = 1,
      .options = application_536_options,
    },
    { .name = "DOOMEternal",
      .executable = "DOOMEternalx64vk.exe",
      .num_options = 1,
      .options = application_538_options,
    },
    { .name = "Farming Simulator 2022",
      .executable = "FarmingSimulator2022Game.exe",
      .num_options = 1,
      .options = application_540_options,
    },
    { .name = "Wolfenstein: Youngblood(x64vk)",
      .executable = "Youngblood_x64vk.exe",
      .num_options = 1,
      .options = application_542_options,
    },
    { .name = "Batman™: Arkham Knight",
      .executable = "BatmanAK.exe",
      .num_options = 1,
      .options = application_544_options,
    },
    { .name = "Company of Heroes 3",
      .executable = "RelicCoH3.exe",
      .num_options = 1,
      .options = application_546_options,
    },
    { .name = "Rise of the Tomb Raider",
      .executable = "RiseOfTheTombRaider",
      .num_options = 1,
      .options = application_548_options,
    },
    { .name = "Rise of the Tomb Raider",
      .executable = "ROTTR.exe",
      .num_options = 1,
      .options = application_550_options,
    },
    { .name = "Sky: Children of the Light",
      .executable = "Sky.exe",
      .num_options = 1,
      .options = application_552_options,
    },
    { .name = "NieR Replicant ver.1.22474487139",
      .executable = "NieR Replicant ver.1.22474487139.exe",
      .num_options = 1,
      .options = application_554_options,
    },
    { .name = "NieR:Automata",
      .executable = "NieRAutomata.exe",
      .num_options = 1,
      .options = application_556_options,
    },
    { .name = "Valheim",
      .executable = "valheim.x86_64",
      .num_options = 1,
      .options = application_558_options,
    },
};

static const struct driconf_device device_531 = {
    .driver = "hasvk",
    .num_engines = 2,
    .engines = device_531_engines,
    .num_applications = 14,
    .applications = device_531_applications,
};




static const struct driconf_device device_564 = {
    .driver = "venus",
    .num_engines = 0,
    .num_applications = 0,
};


    
static const struct driconf_option application_566_options[] = {
    { .name = "mesa_glthread_app_profile", .value = "0" },
};

    
static const struct driconf_option application_568_options[] = {
    { .name = "glsl_correct_derivatives_after_discard", .value = "true" },
};

    
static const struct driconf_option application_570_options[] = {
    { .name = "mesa_extension_override", .value = "+GL_EXT_shader_image_load_store" },
};

    
static const struct driconf_option application_572_options[] = {
    { .name = "zink_emulate_point_smooth", .value = "true" },
};

    
static const struct driconf_option application_574_options[] = {
    { .name = "zink_emulate_point_smooth", .value = "true" },
};

    
static const struct driconf_option application_576_options[] = {
    { .name = "zink_emulate_point_smooth", .value = "true" },
};

    
static const struct driconf_option application_578_options[] = {
    { .name = "zink_shader_object_enable", .value = "true" },
};


static const struct driconf_application device_565_applications[] = {
    { .name = "Hyperdimension Neptunia Re;Birth1",
      .executable = "NeptuniaReBirth1.exe",
      .num_options = 1,
      .options = application_566_options,
    },
    { .name = "Borderlands 2 (Native, OpenGL, 32bit)",
      .executable = "Borderlands2",
      .num_options = 1,
      .options = application_568_options,
    },
    { .name = "SPECviewperf13",
      .executable = "viewperf",
      .num_options = 1,
      .options = application_570_options,
    },
    { .name = "Quake II",
      .executable = "quake2-engine",
      .num_options = 1,
      .options = application_572_options,
    },
    { .name = "Quake II (yamagi)",
      .executable = "yamagi-quake2",
      .num_options = 1,
      .options = application_574_options,
    },
    { .name = "Quake II (wine)",
      .executable = "quake2.exe",
      .num_options = 1,
      .options = application_576_options,
    },
    { .name = "Tomb Raider 2013",
      .executable = "TombRaider",
      .num_options = 1,
      .options = application_578_options,
    },
};

static const struct driconf_device device_565 = {
    .driver = "zink",
    .num_engines = 0,
    .num_applications = 7,
    .applications = device_565_applications,
};

static const struct driconf_device *driconf[] = {
    &device_1,
    &device_385,
    &device_531,
    &device_564,
    &device_565,
};
