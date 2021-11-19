	
	struct volume_group *vg_read(struct cmd_context *cmd, const char *vg_name,
				     const char *vgid, uint32_t read_flags, uint32_t lockd_state)
	{
		uint64_t status_flags = UINT64_C(0);
		uint32_t lock_flags = LCK_VG_READ;
	
		if (read_flags & READ_FOR_UPDATE) {
			status_flags |= EXPORTED_VG | LVM_WRITE;
			lock_flags = LCK_VG_WRITE;
		}
	
		if (read_flags & READ_ALLOW_EXPORTED)
			status_flags &= ~EXPORTED_VG;
	
		
		return _vg_lock_and_read(cmd, vg_name, vgid, lock_flags, status_flags, read_flags, lockd_state);
	}


static int _process_lv_vgnameid_list(struct cmd_context *cmd, uint32_t read_flags,
					     struct dm_list *vgnameids_to_process,
					     struct dm_list *arg_vgnames,
					     struct dm_list *arg_lvnames,
					     struct dm_list *arg_tags,
					     struct processing_handle *handle,
					     process_single_lv_fn_t process_single_lv)
	{
		log_report_t saved_log_report_state = log_get_report_state();
		char uuid[64] __attribute__((aligned(8)));
		struct volume_group *vg;
		struct vgnameid_list *vgnl;
		struct dm_str_list *sl;
		struct dm_list *tags_arg;
		struct dm_list lvnames;
		uint32_t lockd_state = 0;
		const char *vg_name;
		const char *vg_uuid;
		const char *vgn;
		const char *lvn;
		int ret_max = ECMD_PROCESSED;
		int ret;
		int skip;
		int notfound;
		int already_locked;
		int do_report_ret_code = 1;
	
		log_set_report_object_type(LOG_REPORT_OBJECT_TYPE_VG);
	
		// 遍历每个VG
		dm_list_iterate_items(vgnl, vgnameids_to_process) {
			vg_name = vgnl->vg_name;
			vg_uuid = vgnl->vgid;
			skip = 0;
			notfound = 0;
	
			uuid[0] = '\0';
			if (vg_uuid && !id_write_format((const struct id*)vg_uuid, uuid, sizeof(uuid)))
				stack;
	
			log_set_report_object_name_and_id(vg_name, uuid);
	
			if (sigint_caught()) {
				ret_max = ECMD_FAILED;
				goto_out;
			}
	
	
			tags_arg = arg_tags;
			dm_list_init(&lvnames);	/* LVs to be processed in this VG */
	
			dm_list_iterate_items(sl, arg_lvnames) {
				vgn = sl->str;
				lvn = strchr(vgn, '/');
	
				if (!lvn && !strcmp(vgn, vg_name)) {
					/* Process all LVs in this VG */
					tags_arg = NULL;
					dm_list_init(&lvnames);
					break;
				}
				
				if (lvn && !strncmp(vgn, vg_name, strlen(vg_name)) &&
				    strlen(vg_name) == (size_t) (lvn - vgn)) {
					if (!str_list_add(cmd->mem, &lvnames,
							  dm_pool_strdup(cmd->mem, lvn + 1))) {
						log_error("strlist allocation failed.");
						ret_max = ECMD_FAILED;
						goto out;
					}
				}
			}
	
			log_very_verbose("Processing VG %s %s", vg_name, vg_uuid ? uuid : "");
	
			if (!lockd_vg(cmd, vg_name, NULL, 0, &lockd_state)) {
				ret_max = ECMD_FAILED;
				report_log_ret_code(ret_max);
				continue;
			}
	
			already_locked = lvmcache_vgname_is_locked(vg_name);
	
			
			vg = vg_read(cmd, vg_name, vg_uuid, read_flags, lockd_state);
			if (_ignore_vg(vg, vg_name, arg_vgnames, read_flags, &skip, &notfound)) {
				stack;
				ret_max = ECMD_FAILED;
				report_log_ret_code(ret_max);
				goto endvg;
			}
			if (skip || notfound)
				goto endvg;
	
			
			ret = process_each_lv_in_vg(cmd, vg, &lvnames, tags_arg, 0,
						    handle, process_single_lv);
			if (ret != ECMD_PROCESSED)
				stack;
			report_log_ret_code(ret);
			if (ret > ret_max)
				ret_max = ret;
	
			if (!already_locked)
				unlock_vg(cmd, vg, vg_name);
	endvg:
			release_vg(vg);
			if (!lockd_vg(cmd, vg_name, "un", 0, &lockd_state))
				stack;
			log_set_report_object_name_and_id(NULL, NULL);
		}
		do_report_ret_code = 0;
	out:
		if (do_report_ret_code)
			report_log_ret_code(ret_max);
		log_restore_report_state(saved_log_report_state);
		return ret_max;
	}

	int process_each_lv(struct cmd_context *cmd,
			    int argc, char **argv,
			    const char *one_vgname, const char *one_lvname,
			    uint32_t read_flags,
			    struct processing_handle *handle,
			    process_single_lv_fn_t process_single_lv)
	{
		log_report_t saved_log_report_state = log_get_report_state();
		int handle_supplied = handle != NULL;
		struct dm_list arg_tags;		/* str_list */
		struct dm_list arg_vgnames;		/* str_list */
		struct dm_list arg_lvnames;		/* str_list */
		struct dm_list vgnameids_on_system;	/* vgnameid_list */
		struct dm_list vgnameids_to_process;	/* vgnameid_list */
		int enable_all_vgs = (cmd->command->flags & ALL_VGS_IS_DEFAULT);
		int process_all_vgs_on_system = 0;
		int ret_max = ECMD_PROCESSED;
		int ret;
	
		log_set_report_object_type(LOG_REPORT_OBJECT_TYPE_LV);
	
		
		cmd->vg_read_print_access_error = 0;
	
		dm_list_init(&arg_tags);
		dm_list_init(&arg_vgnames);
		dm_list_init(&arg_lvnames);
		dm_list_init(&vgnameids_on_system);
		dm_list_init(&vgnameids_to_process);
	
		// 根据命令行参数生成VG列表
		/*
		 * Find any LVs, VGs or tags explicitly provided on the command line.
		 */
		if ((ret = _get_arg_lvnames(cmd, argc, argv, one_vgname, one_lvname, &arg_vgnames, 
				&arg_lvnames, &arg_tags) != ECMD_PROCESSED)){
			ret_max = ret;
			goto_out;
		}
	
		if (!handle && !(handle = init_processing_handle(cmd, NULL))) {
			ret_max = ECMD_FAILED;
			goto_out;
		}
	
		if (handle->internal_report_for_select && !handle->selection_handle &&
		    !init_selection_handle(cmd, handle, LVS)) {
			ret_max = ECMD_FAILED;
			goto_out;
		}
	
		/*
		 * Process all VGs on the system when:
		 * . tags are specified and all VGs need to be read to
		 *   look for matching tags.
		 * . no VG names are specified and the command defaults
		 *   to processing all VGs when none are specified.
		 * . no VG names are specified and the select option needs
		 *   resolving.
		 */
		if (!dm_list_empty(&arg_tags))
			process_all_vgs_on_system = 1;
		else if (dm_list_empty(&arg_vgnames) && enable_all_vgs)
			process_all_vgs_on_system = 1;
		else if (dm_list_empty(&arg_vgnames) && handle->internal_report_for_select)
			process_all_vgs_on_system = 1;
	
		/*
		 * Needed for a current listing of the global VG namespace.
		 */
		if (process_all_vgs_on_system && !lockd_gl(cmd, "sh", 0)) {
			ret_max = ECMD_FAILED;
			goto_out;
		}
	
	
		
	
		
		if (!get_vgnameids(cmd, &vgnameids_on_system, NULL, 0)) {
			ret_max = ECMD_FAILED;
			goto_out;
		}
		
		if (!dm_list_empty(&arg_vgnames)) {
			
			ret = _resolve_duplicate_vgnames(cmd, &arg_vgnames, &vgnameids_on_system);
			if (ret > ret_max)
				ret_max = ret;
			if (dm_list_empty(&arg_vgnames) && dm_list_empty(&arg_tags)) {
				ret_max = ECMD_FAILED;
				goto out;
			}
		}
	
		if (dm_list_empty(&arg_vgnames) && dm_list_empty(&vgnameids_on_system)) {
			
			log_verbose("No volume groups found.");
			ret_max = ECMD_PROCESSED;
			goto out;
		}
	
		if (dm_list_empty(&arg_vgnames))
			read_flags |= READ_OK_NOTFOUND;
	
	
		if (process_all_vgs_on_system)	
			dm_list_splice(&vgnameids_to_process, &vgnameids_on_system);
		else
			_choose_vgs_to_process(cmd, &arg_vgnames, &vgnameids_on_system, &vgnameids_to_process);
	
		
		ret = _process_lv_vgnameid_list(cmd, read_flags, &vgnameids_to_process, &arg_vgnames, &arg_lvnames,
						&arg_tags, handle, process_single_lv);
	
		if (ret > ret_max)
			ret_max = ret;
	out:
		if (!handle_supplied)
			destroy_processing_handle(cmd, handle);
	
		log_restore_report_state(saved_log_report_state);
		return ret_max;
	}

