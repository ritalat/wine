@ stdcall wined3d_mutex_lock()
@ stdcall wined3d_mutex_unlock()

@ cdecl wined3d_calculate_format_pitch(ptr long long)
@ cdecl wined3d_check_depth_stencil_match(ptr long long long long)
@ cdecl wined3d_check_device_format(ptr ptr long long long long long long)
@ cdecl wined3d_check_device_format_conversion(ptr long long long)
@ cdecl wined3d_check_device_multisample_type(ptr long long long long ptr)
@ cdecl wined3d_check_device_type(ptr ptr long long long long)
@ cdecl wined3d_create(long)
@ cdecl wined3d_decref(ptr)
@ cdecl wined3d_get_adapter(ptr long)
@ cdecl wined3d_get_adapter_count(ptr)
@ cdecl wined3d_get_device_caps(ptr long ptr)
@ cdecl wined3d_get_renderer()
@ cdecl wined3d_incref(ptr)
@ cdecl wined3d_register_software_device(ptr ptr)
@ cdecl wined3d_register_window(ptr ptr ptr long)
@ cdecl wined3d_restore_display_modes(ptr)
@ cdecl wined3d_unregister_windows(ptr)

@ cdecl wined3d_adapter_get_identifier(ptr long ptr)
@ cdecl wined3d_adapter_get_output(ptr long)
@ cdecl wined3d_adapter_get_output_count(ptr)
@ cdecl wined3d_adapter_get_video_memory_info(ptr long long ptr)
@ cdecl wined3d_adapter_register_budget_change_notification(ptr ptr ptr)
@ cdecl wined3d_adapter_unregister_budget_change_notification(long)

@ cdecl wined3d_blend_state_create(ptr ptr ptr ptr ptr)
@ cdecl wined3d_blend_state_decref(ptr)
@ cdecl wined3d_blend_state_get_parent(ptr)
@ cdecl wined3d_blend_state_incref(ptr)

@ cdecl wined3d_buffer_create(ptr ptr ptr ptr ptr ptr)
@ cdecl wined3d_buffer_decref(ptr)
@ cdecl wined3d_buffer_get_parent(ptr)
@ cdecl wined3d_buffer_get_resource(ptr)
@ cdecl wined3d_buffer_incref(ptr)

@ cdecl wined3d_command_list_decref(ptr)
@ cdecl wined3d_command_list_incref(ptr)

@ cdecl wined3d_deferred_context_create(ptr ptr)
@ cdecl wined3d_deferred_context_destroy(ptr)
@ cdecl wined3d_deferred_context_record_command_list(ptr long ptr)

@ cdecl wined3d_depth_stencil_state_create(ptr ptr ptr ptr ptr)
@ cdecl wined3d_depth_stencil_state_decref(ptr)
@ cdecl wined3d_depth_stencil_state_get_parent(ptr)
@ cdecl wined3d_depth_stencil_state_incref(ptr)

@ cdecl wined3d_device_acquire_focus_window(ptr ptr)
@ cdecl wined3d_device_apply_stateblock(ptr ptr)
@ cdecl wined3d_device_begin_scene(ptr)
@ cdecl wined3d_device_clear(ptr long ptr long ptr float long)
@ cdecl wined3d_device_create(ptr ptr long ptr long long ptr long ptr ptr)
@ cdecl wined3d_device_decref(ptr)
@ cdecl wined3d_device_end_scene(ptr)
@ cdecl wined3d_device_evict_managed_resources(ptr)
@ cdecl wined3d_device_get_available_texture_mem(ptr)
@ cdecl wined3d_device_get_clip_status(ptr ptr)
@ cdecl wined3d_device_get_creation_parameters(ptr ptr)
@ cdecl wined3d_device_get_device_caps(ptr ptr)
@ cdecl wined3d_device_get_display_mode(ptr long ptr ptr)
@ cdecl wined3d_device_get_gamma_ramp(ptr long ptr)
@ cdecl wined3d_device_get_immediate_context(ptr)
@ cdecl wined3d_device_get_max_frame_latency(ptr)
@ cdecl wined3d_device_get_npatch_mode(ptr)
@ cdecl wined3d_device_get_raster_status(ptr long ptr)
@ cdecl wined3d_device_get_software_vertex_processing(ptr)
@ cdecl wined3d_device_get_state(ptr)
@ cdecl wined3d_device_get_swapchain(ptr long)
@ cdecl wined3d_device_get_swapchain_count(ptr)
@ cdecl wined3d_device_get_wined3d(ptr)
@ cdecl wined3d_device_incref(ptr)
@ cdecl wined3d_device_process_vertices(ptr long long long ptr ptr long long)
@ cdecl wined3d_device_release_focus_window(ptr)
@ cdecl wined3d_device_reset(ptr ptr ptr ptr long)
@ cdecl wined3d_device_set_clip_status(ptr ptr)
@ cdecl wined3d_device_set_cursor_position(ptr long long long)
@ cdecl wined3d_device_set_cursor_properties(ptr long long ptr long)
@ cdecl wined3d_device_set_dialog_box_mode(ptr long)
@ cdecl wined3d_device_set_gamma_ramp(ptr long long ptr)
@ cdecl wined3d_device_set_max_frame_latency(ptr long)
@ cdecl wined3d_device_set_multithreaded(ptr)
@ cdecl wined3d_device_set_npatch_mode(ptr float)
@ cdecl wined3d_device_set_software_vertex_processing(ptr long)
@ cdecl wined3d_device_show_cursor(ptr long)
@ cdecl wined3d_device_update_texture(ptr ptr ptr)
@ cdecl wined3d_device_validate_device(ptr ptr ptr)

@ cdecl wined3d_device_context_blt(ptr ptr long ptr ptr long ptr long ptr long)
@ cdecl wined3d_device_context_clear_rendertarget_view(ptr ptr ptr long ptr float long)
@ cdecl wined3d_device_context_clear_uav_float(ptr ptr ptr)
@ cdecl wined3d_device_context_clear_uav_uint(ptr ptr ptr)
@ cdecl wined3d_device_context_copy_resource(ptr ptr ptr)
@ cdecl wined3d_device_context_copy_sub_resource_region(ptr ptr long long long long ptr long ptr long)
@ cdecl wined3d_device_context_copy_uav_counter(ptr ptr long ptr)
@ cdecl wined3d_device_context_dispatch(ptr long long long)
@ cdecl wined3d_device_context_dispatch_indirect(ptr ptr long)
@ cdecl wined3d_device_context_draw(ptr long long long long)
@ cdecl wined3d_device_context_draw_indexed(ptr long long long long long)
@ cdecl wined3d_device_context_draw_indirect(ptr ptr long long)
@ cdecl wined3d_device_context_execute_command_list(ptr ptr long)
@ cdecl wined3d_device_context_flush(ptr)
@ cdecl wined3d_device_context_generate_mipmaps(ptr ptr)
@ cdecl wined3d_device_context_get_blend_state(ptr ptr ptr)
@ cdecl wined3d_device_context_get_constant_buffer(ptr long long ptr)
@ cdecl wined3d_device_context_get_depth_stencil_state(ptr ptr)
@ cdecl wined3d_device_context_get_depth_stencil_view(ptr)
@ cdecl wined3d_device_context_get_index_buffer(ptr ptr ptr)
@ cdecl wined3d_device_context_get_predication(ptr ptr)
@ cdecl wined3d_device_context_get_primitive_type(ptr ptr ptr)
@ cdecl wined3d_device_context_get_rasterizer_state(ptr)
@ cdecl wined3d_device_context_get_rendertarget_view(ptr long)
@ cdecl wined3d_device_context_get_sampler(ptr long long)
@ cdecl wined3d_device_context_get_scissor_rects(ptr ptr ptr)
@ cdecl wined3d_device_context_get_shader(ptr long)
@ cdecl wined3d_device_context_get_shader_resource_view(ptr long long)
@ cdecl wined3d_device_context_get_stream_output(ptr long ptr)
@ cdecl wined3d_device_context_get_stream_source(ptr long ptr ptr ptr)
@ cdecl wined3d_device_context_get_unordered_access_view(ptr long long)
@ cdecl wined3d_device_context_get_vertex_declaration(ptr)
@ cdecl wined3d_device_context_get_viewports(ptr ptr ptr)
@ cdecl wined3d_device_context_issue_query(ptr ptr long)
@ cdecl wined3d_device_context_map(ptr ptr long ptr ptr long)
@ cdecl wined3d_device_context_reset_state(ptr)
@ cdecl wined3d_device_context_resolve_sub_resource(ptr ptr long ptr long long)
@ cdecl wined3d_device_context_set_blend_state(ptr ptr ptr long)
@ cdecl wined3d_device_context_set_constant_buffers(ptr long long long ptr)
@ cdecl wined3d_device_context_set_depth_stencil_state(ptr ptr long)
@ cdecl wined3d_device_context_set_depth_stencil_view(ptr ptr)
@ cdecl wined3d_device_context_set_index_buffer(ptr ptr long long)
@ cdecl wined3d_device_context_set_predication(ptr ptr long)
@ cdecl wined3d_device_context_set_primitive_type(ptr long long)
@ cdecl wined3d_device_context_set_rasterizer_state(ptr ptr)
@ cdecl wined3d_device_context_set_render_targets_and_unordered_access_views(ptr long ptr ptr long ptr ptr)
@ cdecl wined3d_device_context_set_rendertarget_views(ptr long long ptr long)
@ cdecl wined3d_device_context_set_samplers(ptr long long long ptr)
@ cdecl wined3d_device_context_set_scissor_rects(ptr long ptr)
@ cdecl wined3d_device_context_set_shader(ptr long ptr)
@ cdecl wined3d_device_context_set_shader_resource_views(ptr long long long ptr)
@ cdecl wined3d_device_context_set_state(ptr ptr)
@ cdecl wined3d_device_context_set_stream_outputs(ptr ptr)
@ cdecl wined3d_device_context_set_stream_sources(ptr long long ptr)
@ cdecl wined3d_device_context_set_unordered_access_views(ptr long long long ptr ptr)
@ cdecl wined3d_device_context_set_vertex_declaration(ptr ptr)
@ cdecl wined3d_device_context_set_viewports(ptr long ptr)
@ cdecl wined3d_device_context_unmap(ptr ptr long)
@ cdecl wined3d_device_context_update_sub_resource(ptr ptr long ptr ptr long long long)

@ cdecl wined3d_output_find_closest_matching_mode(ptr ptr)
@ cdecl wined3d_output_get_adapter(ptr)
@ cdecl wined3d_output_get_desc(ptr ptr)
@ cdecl wined3d_output_get_display_mode(ptr ptr ptr)
@ cdecl wined3d_output_get_mode(ptr long long long ptr long)
@ cdecl wined3d_output_get_mode_count(ptr long long long)
@ cdecl wined3d_output_get_raster_status(ptr ptr)
@ cdecl wined3d_output_release_ownership(ptr)
@ cdecl wined3d_output_set_display_mode(ptr ptr)
@ cdecl wined3d_output_set_gamma_ramp(ptr ptr)
@ cdecl wined3d_output_take_ownership(ptr long)

@ cdecl wined3d_palette_apply_to_dc(ptr ptr)
@ cdecl wined3d_palette_create(ptr long long ptr ptr)
@ cdecl wined3d_palette_decref(ptr)
@ cdecl wined3d_palette_get_entries(ptr long long long ptr)
@ cdecl wined3d_palette_incref(ptr)
@ cdecl wined3d_palette_set_entries(ptr long long long ptr)

@ cdecl wined3d_query_create(ptr long ptr ptr ptr)
@ cdecl wined3d_query_decref(ptr)
@ cdecl wined3d_query_get_data(ptr ptr long long)
@ cdecl wined3d_query_get_data_size(ptr)
@ cdecl wined3d_query_get_parent(ptr)
@ cdecl wined3d_query_get_type(ptr)
@ cdecl wined3d_query_incref(ptr)
@ cdecl wined3d_query_issue(ptr long)

@ cdecl wined3d_rasterizer_state_create(ptr ptr ptr ptr ptr)
@ cdecl wined3d_rasterizer_state_decref(ptr)
@ cdecl wined3d_rasterizer_state_get_parent(ptr)
@ cdecl wined3d_rasterizer_state_incref(ptr)

@ cdecl wined3d_rendertarget_view_create(ptr ptr ptr ptr ptr)
@ cdecl wined3d_rendertarget_view_create_from_sub_resource(ptr long ptr ptr ptr)
@ cdecl wined3d_rendertarget_view_decref(ptr)
@ cdecl wined3d_rendertarget_view_get_parent(ptr)
@ cdecl wined3d_rendertarget_view_get_resource(ptr)
@ cdecl wined3d_rendertarget_view_get_sub_resource_parent(ptr)
@ cdecl wined3d_rendertarget_view_incref(ptr)
@ cdecl wined3d_rendertarget_view_set_parent(ptr ptr ptr)

@ cdecl wined3d_resource_get_desc(ptr ptr)
@ cdecl wined3d_resource_get_parent(ptr)
@ cdecl wined3d_resource_get_priority(ptr)
@ cdecl wined3d_resource_map(ptr long ptr ptr long)
@ cdecl wined3d_resource_preload(ptr)
@ cdecl wined3d_resource_set_parent(ptr ptr ptr)
@ cdecl wined3d_resource_set_priority(ptr long)
@ cdecl wined3d_resource_unmap(ptr long)

@ cdecl wined3d_sampler_create(ptr ptr ptr ptr ptr)
@ cdecl wined3d_sampler_decref(ptr)
@ cdecl wined3d_sampler_get_parent(ptr)
@ cdecl wined3d_sampler_incref(ptr)

@ cdecl wined3d_shader_create_cs(ptr ptr ptr ptr ptr)
@ cdecl wined3d_shader_create_ds(ptr ptr ptr ptr ptr)
@ cdecl wined3d_shader_create_gs(ptr ptr ptr ptr ptr ptr)
@ cdecl wined3d_shader_create_hs(ptr ptr ptr ptr ptr)
@ cdecl wined3d_shader_create_ps(ptr ptr ptr ptr ptr)
@ cdecl wined3d_shader_create_vs(ptr ptr ptr ptr ptr)
@ cdecl wined3d_shader_decref(ptr)
@ cdecl wined3d_shader_get_byte_code(ptr ptr ptr)
@ cdecl wined3d_shader_get_parent(ptr)
@ cdecl wined3d_shader_incref(ptr)
@ cdecl wined3d_shader_set_local_constants_float(ptr long ptr long)

@ cdecl wined3d_shader_resource_view_create(ptr ptr ptr ptr ptr)
@ cdecl wined3d_shader_resource_view_decref(ptr)
@ cdecl wined3d_shader_resource_view_get_parent(ptr)
@ cdecl wined3d_shader_resource_view_incref(ptr)

@ cdecl wined3d_state_create(ptr ptr long ptr)
@ cdecl wined3d_state_destroy(ptr)
@ cdecl wined3d_state_get_feature_level(ptr)

@ cdecl wined3d_stateblock_apply(ptr ptr)
@ cdecl wined3d_stateblock_capture(ptr ptr)
@ cdecl wined3d_stateblock_create(ptr ptr long ptr)
@ cdecl wined3d_stateblock_decref(ptr)
@ cdecl wined3d_stateblock_get_light(ptr long ptr ptr)
@ cdecl wined3d_stateblock_get_ps_consts_b(ptr long long ptr)
@ cdecl wined3d_stateblock_get_ps_consts_f(ptr long long ptr)
@ cdecl wined3d_stateblock_get_ps_consts_i(ptr long long ptr)
@ cdecl wined3d_stateblock_get_state(ptr)
@ cdecl wined3d_stateblock_get_vs_consts_b(ptr long long ptr)
@ cdecl wined3d_stateblock_get_vs_consts_f(ptr long long ptr)
@ cdecl wined3d_stateblock_get_vs_consts_i(ptr long long ptr)
@ cdecl wined3d_stateblock_incref(ptr)
@ cdecl wined3d_stateblock_init_contained_states(ptr)
@ cdecl wined3d_stateblock_multiply_transform(ptr long ptr)
@ cdecl wined3d_stateblock_reset(ptr)
@ cdecl wined3d_stateblock_set_base_vertex_index(ptr long)
@ cdecl wined3d_stateblock_set_clip_plane(ptr long ptr)
@ cdecl wined3d_stateblock_set_index_buffer(ptr ptr long)
@ cdecl wined3d_stateblock_set_light(ptr long ptr)
@ cdecl wined3d_stateblock_set_light_enable(ptr long long)
@ cdecl wined3d_stateblock_set_material(ptr ptr)
@ cdecl wined3d_stateblock_set_pixel_shader(ptr ptr)
@ cdecl wined3d_stateblock_set_ps_consts_b(ptr long long ptr)
@ cdecl wined3d_stateblock_set_ps_consts_f(ptr long long ptr)
@ cdecl wined3d_stateblock_set_ps_consts_i(ptr long long ptr)
@ cdecl wined3d_stateblock_set_render_state(ptr long long)
@ cdecl wined3d_stateblock_set_sampler_state(ptr long long long)
@ cdecl wined3d_stateblock_set_scissor_rect(ptr ptr)
@ cdecl wined3d_stateblock_set_stream_source(ptr long ptr long long)
@ cdecl wined3d_stateblock_set_stream_source_freq(ptr long long)
@ cdecl wined3d_stateblock_set_texture(ptr long ptr)
@ cdecl wined3d_stateblock_set_texture_lod(ptr ptr long)
@ cdecl wined3d_stateblock_set_texture_stage_state(ptr long long long)
@ cdecl wined3d_stateblock_set_transform(ptr long ptr)
@ cdecl wined3d_stateblock_set_vertex_declaration(ptr ptr)
@ cdecl wined3d_stateblock_set_vertex_shader(ptr ptr)
@ cdecl wined3d_stateblock_set_viewport(ptr ptr)
@ cdecl wined3d_stateblock_set_vs_consts_b(ptr long long ptr)
@ cdecl wined3d_stateblock_set_vs_consts_f(ptr long long ptr)
@ cdecl wined3d_stateblock_set_vs_consts_i(ptr long long ptr)

@ cdecl wined3d_streaming_buffer_map(ptr ptr long long ptr ptr)
@ cdecl wined3d_streaming_buffer_unmap(ptr)
@ cdecl wined3d_streaming_buffer_upload(ptr ptr ptr long long ptr)

@ cdecl wined3d_swapchain_create(ptr ptr ptr ptr ptr ptr)
@ cdecl wined3d_swapchain_decref(ptr)
@ cdecl wined3d_swapchain_get_back_buffer(ptr long)
@ cdecl wined3d_swapchain_get_desc(ptr ptr)
@ cdecl wined3d_swapchain_get_device(ptr)
@ cdecl wined3d_swapchain_get_display_mode(ptr ptr ptr)
@ cdecl wined3d_swapchain_get_front_buffer(ptr)
@ cdecl wined3d_swapchain_get_front_buffer_data(ptr ptr long)
@ cdecl wined3d_swapchain_get_gamma_ramp(ptr ptr)
@ cdecl wined3d_swapchain_get_parent(ptr)
@ cdecl wined3d_swapchain_get_raster_status(ptr ptr)
@ cdecl wined3d_swapchain_get_state(ptr)
@ cdecl wined3d_swapchain_incref(ptr)
@ cdecl wined3d_swapchain_present(ptr ptr ptr ptr long long)
@ cdecl wined3d_swapchain_resize_buffers(ptr long long long long long long)
@ cdecl wined3d_swapchain_set_gamma_ramp(ptr long ptr)
@ cdecl wined3d_swapchain_set_palette(ptr ptr)
@ cdecl wined3d_swapchain_set_window(ptr ptr)

@ cdecl wined3d_swapchain_state_create(ptr ptr ptr ptr ptr)
@ cdecl wined3d_swapchain_state_destroy(ptr)
@ cdecl wined3d_swapchain_state_get_size(ptr ptr ptr)
@ cdecl wined3d_swapchain_state_is_windowed(ptr)
@ cdecl wined3d_swapchain_state_resize_target(ptr ptr)
@ cdecl wined3d_swapchain_state_set_fullscreen(ptr ptr ptr)

@ cdecl wined3d_texture_acquire_identity_srv(ptr)
@ cdecl wined3d_texture_add_dirty_region(ptr long ptr)
@ cdecl wined3d_texture_create(ptr ptr long long long ptr ptr ptr ptr)
@ cdecl wined3d_texture_decref(ptr)
@ cdecl wined3d_texture_from_resource(ptr)
@ cdecl wined3d_texture_get_dc(ptr long ptr)
@ cdecl wined3d_texture_get_level_count(ptr)
@ cdecl wined3d_texture_get_lod(ptr)
@ cdecl wined3d_texture_get_overlay_position(ptr long ptr ptr)
@ cdecl wined3d_texture_get_parent(ptr)
@ cdecl wined3d_texture_get_pitch(ptr long ptr ptr)
@ cdecl wined3d_texture_get_resource(ptr)
@ cdecl wined3d_texture_get_sub_resource_desc(ptr long ptr)
@ cdecl wined3d_texture_get_sub_resource_parent(ptr long)
@ cdecl wined3d_texture_get_swapchain(ptr)
@ cdecl wined3d_texture_incref(ptr)
@ cdecl wined3d_texture_release_dc(ptr long ptr)
@ cdecl wined3d_texture_set_color_key(ptr long ptr)
@ cdecl wined3d_texture_set_overlay_position(ptr long long long)
@ cdecl wined3d_texture_set_sub_resource_parent(ptr long ptr ptr)
@ cdecl wined3d_texture_update_desc(ptr long ptr long)
@ cdecl wined3d_texture_update_overlay(ptr long ptr ptr long ptr long)

@ cdecl wined3d_unordered_access_view_create(ptr ptr ptr ptr ptr)
@ cdecl wined3d_unordered_access_view_decref(ptr)
@ cdecl wined3d_unordered_access_view_get_parent(ptr)
@ cdecl wined3d_unordered_access_view_incref(ptr)

@ cdecl wined3d_vertex_declaration_create(ptr ptr long ptr ptr ptr)
@ cdecl wined3d_vertex_declaration_create_from_fvf(ptr long ptr ptr ptr)
@ cdecl wined3d_vertex_declaration_decref(ptr)
@ cdecl wined3d_vertex_declaration_get_parent(ptr)
@ cdecl wined3d_vertex_declaration_incref(ptr)

@ cdecl vkd3d_acquire_vk_queue(ptr)
@ cdecl vkd3d_create_device(ptr ptr ptr)
@ cdecl vkd3d_create_image_resource(ptr ptr ptr)
@ cdecl vkd3d_create_instance(ptr ptr)
@ cdecl vkd3d_create_root_signature_deserializer(ptr long ptr ptr)
@ cdecl vkd3d_create_versioned_root_signature_deserializer(ptr long ptr ptr)
@ cdecl vkd3d_get_device_parent(ptr)
@ cdecl vkd3d_get_dxgi_format(long)
@ cdecl vkd3d_get_vk_device(ptr)
@ cdecl vkd3d_get_vk_format(long)
@ cdecl vkd3d_get_vk_physical_device(ptr)
@ cdecl vkd3d_get_vk_queue_family_index(ptr)
@ cdecl vkd3d_instance_decref(ptr)
@ cdecl vkd3d_instance_from_device(ptr)
@ cdecl vkd3d_instance_get_vk_instance(ptr)
@ cdecl vkd3d_instance_incref(ptr)
@ cdecl vkd3d_release_vk_queue(ptr)
@ cdecl vkd3d_resource_decref(ptr)
@ cdecl vkd3d_resource_incref(ptr)
@ cdecl vkd3d_serialize_root_signature(ptr long ptr ptr)
@ cdecl vkd3d_serialize_versioned_root_signature(ptr ptr ptr)

@ cdecl vkd3d_shader_compile(ptr ptr ptr)
@ cdecl vkd3d_shader_convert_root_signature(ptr long ptr)
@ cdecl vkd3d_shader_find_signature_element(ptr ptr long long)
@ cdecl vkd3d_shader_free_dxbc(ptr)
@ cdecl vkd3d_shader_free_messages(ptr)
@ cdecl vkd3d_shader_free_root_signature(ptr)
@ cdecl vkd3d_shader_free_scan_descriptor_info(ptr)
@ cdecl vkd3d_shader_free_shader_code(ptr)
@ cdecl vkd3d_shader_free_shader_signature(ptr)
@ cdecl vkd3d_shader_get_supported_source_types(ptr)
@ cdecl vkd3d_shader_get_supported_target_types(long ptr)
@ cdecl vkd3d_shader_get_version(ptr ptr)
@ cdecl vkd3d_shader_parse_dxbc(ptr long ptr ptr)
@ cdecl vkd3d_shader_parse_input_signature(ptr ptr ptr)
@ cdecl vkd3d_shader_parse_root_signature(ptr ptr ptr)
@ cdecl vkd3d_shader_preprocess(ptr ptr ptr)
@ cdecl vkd3d_shader_scan(ptr ptr)
@ cdecl vkd3d_shader_serialize_dxbc(long ptr ptr ptr)
@ cdecl vkd3d_shader_serialize_root_signature(ptr ptr ptr)
