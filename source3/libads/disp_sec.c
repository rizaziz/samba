/* 
   Unix SMB/CIFS implementation.
   Samba utility functions. ADS stuff
   Copyright (C) Alexey Kotovich 2002
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful, 
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "includes.h"
#include "ads.h"
#include "libads/ldap_schema.h"
#include "../libcli/security/secace.h"
#include "../librpc/ndr/libndr.h"
#include "libcli/security/dom_sid.h"

/* for ADS */
#define SEC_RIGHTS_FULL_CTRL		0xf01ff

#ifdef HAVE_LDAP

static struct perm_mask_str {
	uint32_t  mask;
	const char   *str;
} perms[] = {
	{SEC_RIGHTS_FULL_CTRL,		"[Full Control]"},

	{SEC_ADS_LIST,			"[List Contents]"},
	{SEC_ADS_LIST_OBJECT,		"[List Object]"},

	{SEC_ADS_READ_PROP,		"[Read All Properties]"},
	{SEC_STD_READ_CONTROL,		"[Read Permissions]"},

	{SEC_ADS_SELF_WRITE,		"[All validate writes]"},
	{SEC_ADS_WRITE_PROP,		"[Write All Properties]"},

	{SEC_STD_WRITE_DAC,		"[Modify Permissions]"},
	{SEC_STD_WRITE_OWNER,		"[Modify Owner]"},

	{SEC_ADS_CREATE_CHILD,		"[Create All Child Objects]"},

	{SEC_STD_DELETE,		"[Delete]"},
	{SEC_ADS_DELETE_TREE,		"[Delete Subtree]"},
	{SEC_ADS_DELETE_CHILD,		"[Delete All Child Objects]"},

	{SEC_ADS_CONTROL_ACCESS,	"[Change Password]"},
	{SEC_ADS_CONTROL_ACCESS,	"[Reset Password]"},

	{0,				0}
};

/* convert a security permissions into a string */
static void ads_disp_perms(uint32_t type)
{
	int i = 0;
	int j = 0;

	printf("Permissions: ");
	
	if (type == SEC_RIGHTS_FULL_CTRL) {
		printf("%s\n", perms[j].str);
		return;
	}

	for (i = 0; i < 32; i++) {
		if (type & ((uint32_t)1 << i)) {
			for (j = 1; perms[j].str; j ++) {
				if (perms[j].mask == (((unsigned) 1) << i)) {
					printf("\n\t%s (0x%08x)", perms[j].str, perms[j].mask);
				}	
			}
			type &= ~(1 << i);
		}
	}

	/* remaining bits get added on as-is */
	if (type != 0) {
		printf("[%08x]", type);
	}
	puts("");
}

static const char *ads_interprete_guid_from_object(ADS_STRUCT *ads, 
						   TALLOC_CTX *mem_ctx, 
						   const struct GUID *guid)
{
	const char *ret = NULL;

	if (!ads || !mem_ctx) {
		return NULL;
	}

	ret = ads_get_attrname_by_guid(ads, ads->config.schema_path, 
				       mem_ctx, guid);
	if (ret) {
		return talloc_asprintf(mem_ctx, "LDAP attribute: \"%s\"", ret);
	}

	ret = ads_get_extended_right_name_by_guid(ads, ads->config.config_path,
						  mem_ctx, guid);

	if (ret) {
		return talloc_asprintf(mem_ctx, "Extended right: \"%s\"", ret);
	}

	return ret;
}

static void ads_disp_sec_ace_object(ADS_STRUCT *ads, 
				    TALLOC_CTX *mem_ctx, 
				    struct security_ace_object *object)
{
	if (object->flags & SEC_ACE_OBJECT_TYPE_PRESENT) {
		printf("Object type: SEC_ACE_OBJECT_TYPE_PRESENT\n");
		printf("Object GUID: %s (%s)\n", GUID_string(mem_ctx, 
			&object->type.type), 
			ads_interprete_guid_from_object(ads, mem_ctx, 
				&object->type.type));
	}
	if (object->flags & SEC_ACE_INHERITED_OBJECT_TYPE_PRESENT) {
		printf("Object type: SEC_ACE_INHERITED_OBJECT_TYPE_PRESENT\n");
		printf("Object GUID: %s (%s)\n", GUID_string(mem_ctx,
			&object->inherited_type.inherited_type),
			ads_interprete_guid_from_object(ads, mem_ctx, 
				&object->inherited_type.inherited_type));
	}
}

/* display ACE */
static void ads_disp_ace(ADS_STRUCT *ads, TALLOC_CTX *mem_ctx, struct security_ace *sec_ace)
{
	const char *access_type = "UNKNOWN";
	struct dom_sid_buf sidbuf;

	if (!sec_ace_object(sec_ace->type)) {
		printf("------- ACE (type: 0x%02x, flags: 0x%02x, size: 0x%02x, mask: 0x%x)\n", 
		  sec_ace->type,
		  sec_ace->flags,
		  sec_ace->size,
		  sec_ace->access_mask);			
	} else {
		printf("------- ACE (type: 0x%02x, flags: 0x%02x, size: 0x%02x, mask: 0x%x, object flags: 0x%x)\n", 
		  sec_ace->type,
		  sec_ace->flags,
		  sec_ace->size,
		  sec_ace->access_mask,
		  sec_ace->object.object.flags);
	}
	
	if (sec_ace->type == SEC_ACE_TYPE_ACCESS_ALLOWED) {
		access_type = "ALLOWED";
	} else if (sec_ace->type == SEC_ACE_TYPE_ACCESS_DENIED) {
		access_type = "DENIED";
	} else if (sec_ace->type == SEC_ACE_TYPE_SYSTEM_AUDIT) {
		access_type = "SYSTEM AUDIT";
	} else if (sec_ace->type == SEC_ACE_TYPE_ACCESS_ALLOWED_OBJECT) {
		access_type = "ALLOWED OBJECT";
	} else if (sec_ace->type == SEC_ACE_TYPE_ACCESS_DENIED_OBJECT) {
		access_type = "DENIED OBJECT";
	} else if (sec_ace->type == SEC_ACE_TYPE_SYSTEM_AUDIT_OBJECT) {
		access_type = "AUDIT OBJECT";
	}

	printf("access SID:  %s\naccess type: %s\n",
	       dom_sid_str_buf(&sec_ace->trustee, &sidbuf),
	       access_type);

	if (sec_ace_object(sec_ace->type)) {
		ads_disp_sec_ace_object(ads, mem_ctx, &sec_ace->object.object);
	}

	ads_disp_perms(sec_ace->access_mask);
}

/* display ACL */
static void ads_disp_acl(struct security_acl *sec_acl, const char *type)
{
        if (!sec_acl)
		printf("------- (%s) ACL not present\n", type);
	else {
		printf("------- (%s) ACL (revision: %d, size: %d, number of ACEs: %d)\n", 
    	    	       type,
        	       sec_acl->revision,
	               sec_acl->size,
    		       sec_acl->num_aces);			
	}
}

/* display SD */
void ads_disp_sd(ADS_STRUCT *ads, TALLOC_CTX *mem_ctx, struct security_descriptor *sd)
{
	uint32_t i;
	char *tmp_path = NULL;
	struct dom_sid_buf buf;

	if (!sd) {
		return;
	}

	if (ads && !ads->config.schema_path) {
		if (ADS_ERR_OK(ads_schema_path(ads, mem_ctx, &tmp_path))) {
			ads->config.schema_path = talloc_strdup(ads, tmp_path);
			if (ads->config.schema_path == NULL) {
				DBG_WARNING("Out of memory\n");
			}
		}
	}

	if (ads && !ads->config.config_path) {
		if (ADS_ERR_OK(ads_config_path(ads, mem_ctx, &tmp_path))) {
			ads->config.config_path = talloc_strdup(ads, tmp_path);
			if (ads->config.config_path == NULL) {
				DBG_WARNING("Out of memory\n");
			}
		}
	}

	printf("-------------- Security Descriptor (revision: %d, type: 0x%02x)\n", 
               sd->revision,
               sd->type);

	printf("owner SID: %s\n", sd->owner_sid ? 
	       dom_sid_str_buf(sd->owner_sid, &buf) : "(null)");
	printf("group SID: %s\n", sd->group_sid ?
	       dom_sid_str_buf(sd->group_sid, &buf) : "(null)");

	ads_disp_acl(sd->sacl, "system");
	if (sd->sacl) {
		for (i = 0; i < sd->sacl->num_aces; i ++) {
			ads_disp_ace(ads, mem_ctx, &sd->sacl->aces[i]);
		}
	}
	
	ads_disp_acl(sd->dacl, "user");
	if (sd->dacl) {
		for (i = 0; i < sd->dacl->num_aces; i ++) {
			ads_disp_ace(ads, mem_ctx, &sd->dacl->aces[i]);
		}
	}

	printf("-------------- End Of Security Descriptor\n");
}

#endif
