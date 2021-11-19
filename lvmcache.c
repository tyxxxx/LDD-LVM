int lvmcache_get_vgnameids(struct cmd_context *cmd, int include_internal,
				   struct dm_list *vgnameids)
	{
		struct vgnameid_list *vgnl;
		struct lvmcache_vginfo *vginfo;
	
		lvmcache_label_scan(cmd);
		
		
		dm_list_iterate_items(vginfo, &_vginfos) {
			if (!include_internal && is_orphan_vg(vginfo->vgname))
				continue;
	
			if (!(vgnl = dm_pool_alloc(cmd->mem, sizeof(*vgnl)))) {
				log_error("vgnameid_list allocation failed.");
				return 0;
			}
	
			vgnl->vgid = dm_pool_strdup(cmd->mem, vginfo->vgid);
			vgnl->vg_name = dm_pool_strdup(cmd->mem, vginfo->vgname);
	
			if (!vgnl->vgid || !vgnl->vg_name) {
				log_error("vgnameid_list member allocation failed.");
				return 0;
			}
	
			dm_list_add(vgnameids, &vgnl->list);
		}
	
		return 1;
	}
