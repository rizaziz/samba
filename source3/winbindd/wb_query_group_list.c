/*
   Unix SMB/CIFS implementation.
   async query_group_list
   Copyright (C) Volker Lendecke 2009
   Copyright (C) Michael Adam 2015

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
#include "winbindd.h"
#include "librpc/gen_ndr/ndr_winbind_c.h"


struct wb_query_group_list_state {
	struct wbint_Principals groups;
};

static void wb_query_group_list_done(struct tevent_req *subreq);

struct tevent_req *wb_query_group_list_send(TALLOC_CTX *mem_ctx,
					    struct tevent_context *ev,
					    struct winbindd_domain *domain)
{
	struct tevent_req *req, *subreq;
	struct wb_query_group_list_state *state;

	req = tevent_req_create(mem_ctx, &state,
				struct wb_query_group_list_state);
	if (req == NULL) {
		return NULL;
	}

	D_INFO("WB command group_list start.\nQuery domain %s\n", domain->name);
	subreq = dcerpc_wbint_QueryGroupList_send(state, ev,
						  dom_child_handle(domain),
						  &state->groups);
	if (tevent_req_nomem(subreq, req)) {
		return tevent_req_post(req, ev);
	}

	tevent_req_set_callback(subreq, wb_query_group_list_done, req);
	return req;
}

static void wb_query_group_list_done(struct tevent_req *subreq)
{
	struct tevent_req *req = tevent_req_callback_data(
		subreq, struct tevent_req);
	struct wb_query_group_list_state *state = tevent_req_data(
		req, struct wb_query_group_list_state);
	NTSTATUS status, result;

	status = dcerpc_wbint_QueryGroupList_recv(subreq, state, &result);
	TALLOC_FREE(subreq);
	if (any_nt_status_not_ok(status, result, &status)) {
		tevent_req_nterror(req, status);
		return;
	}

	tevent_req_done(req);
}

NTSTATUS wb_query_group_list_recv(struct tevent_req *req, TALLOC_CTX *mem_ctx,
				  uint32_t *num_groups,
				  struct wbint_Principal **groups)
{
	struct wb_query_group_list_state *state = tevent_req_data(
		req, struct wb_query_group_list_state);
	NTSTATUS status;

	if (tevent_req_is_nterror(req, &status)) {
		D_WARNING("Failed with: %s\n", nt_errstr(status));
		return status;
	}

	*num_groups = state->groups.num_principals;
	*groups = talloc_move(mem_ctx, &state->groups.principals);

	D_INFO("WB command group_list end.\n"
	       "Returning %"PRIu32" group(s).\n", *num_groups);
	return NT_STATUS_OK;
}
